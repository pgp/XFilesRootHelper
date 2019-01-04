#ifndef _BOTAN_RH_RCLIENT_H_
#define _BOTAN_RH_RCLIENT_H_

#include <thread>
#include <chrono>

#include "botan_credentials.h"
#include "../ReliableRingBuffer.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

class ClassicPolicy : public Botan::TLS::Policy {
public:
    bool acceptable_ciphersuite(const Botan::TLS::Ciphersuite &suite) const override {
        // ECDHE_RSA_WITH_AES_256_GCM_SHA384 code = 0xC030
        return suite.ciphersuite_code() == 0xC030;
    }
};

class PostQuantumPolicy : public Botan::TLS::Policy {
public:
    bool acceptable_ciphersuite(const Botan::TLS::Ciphersuite &suite) const override {
//        0x16B7,0x16B8,0x16B9,0x16BA CECPQ1
	auto csc = suite.ciphersuite_code();
        return (csc == 0x16B7 ||
                csc == 0x16B8 ||
                csc == 0x16B9 ||
                csc == 0x16BA);
    }
};

typedef void (*TlsClientEventLoopFn) (RingBuffer& inRb, Botan::TLS::Client& client, IDescriptor& local_sock_fd);

class TLS_Client final : public Botan::TLS::Callbacks {
private:
    static constexpr int defaultServerPort = 11111;

    RingBuffer inRb;
    TlsClientEventLoopFn eventLoopFn;
    IDescriptor& Gsock;
    IDescriptor& local_sock_fd; // for communicating shared session hash to RH client once session establishment is complete
    Botan::TLS::Client* client;

	// local_socket passed for closing it by here when this thread terminates before the other, in so avoiding deadlock
    static void incomingRbMemberFn(IDescriptor& networkSocket, Botan::TLS::Client& client, IDescriptor& local_socket) {
        for(;;) {
            std::vector<uint8_t> buf(16384,0);
            ssize_t readBytes = networkSocket.read(&buf[0],16384);
            if (readBytes < 0) {
				PRINTUNIFIEDERROR("networkSocket read error\n");
                local_socket.close();
                return;
			}
            else if (readBytes == 0) {
				PRINTUNIFIEDERROR("networkSocket EOF\n");
				local_socket.close();
				return;
			}
            buf.resize(readBytes);
            client.received_data(buf); // -> tls_record_received writes into ringbuffer
        }
    }

    void tls_verify_cert_chain(
            const std::vector<Botan::X509_Certificate>& cert_chain,
            const std::vector<std::shared_ptr<const Botan::OCSP::Response>>& ocsp,
            const std::vector<Botan::Certificate_Store*>& trusted_roots,
            Botan::Usage_Type usage,
            const std::string& hostname,
            const Botan::TLS::Policy& policy) override {}

    bool tls_session_established(const Botan::TLS::Session& session) override {
        PRINTUNIFIED("Handshake complete, %s using %s\n",session.version().to_string().c_str(),
							session.ciphersuite().to_string().c_str());
		auto master_secret = session.master_secret();
		
		// NEW: SHA256 -> 32 bytes binary data ////////////////////
		std::unique_ptr<Botan::HashFunction> sha256(Botan::HashFunction::create("SHA-256"));
		sha256->update(master_secret.data(),master_secret.size());
		Botan::secure_vector<uint8_t> sharedHash = sha256->final();
		
		PRINTUNIFIED("Master secret's hash for this session is:\n%s\n",Botan::hex_encode(sharedHash).c_str());
		
		if(local_sock_fd.write(&sharedHash[0],sharedHash.size()) < sharedHash.size()) {
			PRINTUNIFIEDERROR("Unable to atomic write connect info to local socket");
			threadExit();
		}

        return false;
    }

    void tls_emit_data(const uint8_t* data, size_t size) override {
        Gsock.writeAllOrExit(data,size);
    }

    void tls_alert(Botan::TLS::Alert alert) override {
        PRINTUNIFIED("Alert: %s\n",alert.type_string().c_str());
        if (alert.type() == Botan::TLS::Alert::Type::CLOSE_NOTIFY) {
            PRINTUNIFIED("TLS endpoint closed connection");
            threadExit();
        }
    }

    void tls_record_received(uint64_t seq_no, const uint8_t buf[], size_t buf_size) override {
        if (inRb.writeAll(buf,buf_size) < buf_size) exit(9341); // RingBuffer is reliable, OK to call exit here
    }

public:
	// constructor from IP and port that tries to connect
    // is removed for now, needs abstract class reference as input
            
    // constructor using an already connected socket
    TLS_Client(TlsClientEventLoopFn eventLoopFn_,
               IDescriptor& local_sock_fd_,
               IDescriptor& Gsock_) :
            eventLoopFn(eventLoopFn_),
            local_sock_fd(local_sock_fd_),
            Gsock(Gsock_),
            client(nullptr) {}

    void go() {
        Botan::System_RNG rng_;
        Botan::TLS::Session_Manager_In_Memory session_mgr(rng_);
        const std::string next_protos = "";
        Basic_Credentials_Manager creds;
        const std::vector<std::string> protocols_to_offer = Botan::split_on(next_protos, ',');
        auto version = Botan::TLS::Protocol_Version::TLS_V12;
        using namespace std::placeholders;

		// PostQuantumPolicy policy; // no shared cipher if non-Botan-based XRE server is used
		// ClassicPolicy policy;
        Botan::TLS::Policy policy;

        client = new Botan::TLS::Client(*this,
                                  session_mgr,
                                  creds,
                                  policy,
                                  rng_,
                                  Botan::TLS::Server_Information("", defaultServerPort),
                                  version,
                                  protocols_to_offer);

        // here start helper network thread (filling incomingRb)
        std::thread incomingRbThread(incomingRbMemberFn,
                                     std::ref(Gsock),
                                     std::ref(*client),
                                     std::ref(local_sock_fd));

        PRINTUNIFIED("Waiting for TLS channel to be ready");
        while(!client->is_active()) {
            PRINTUNIFIED(".");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        PRINTUNIFIED("\nTLS channel ready\n");

        try {
            eventLoopFn(inRb,std::ref(*client),local_sock_fd); // TLS client interacts with local socket
        }
        catch (threadExitThrowable& i) {
            PRINTUNIFIEDERROR("T1 Unconditional housekeeping and return");
            cleanup();
        }

        incomingRbThread.join();
    }

    void cleanup() {
        inRb.close();

        PRINTUNIFIED("Finished, waiting for TLS client to close...\n");
        client->close();
        while(!client->is_closed()) {
            PRINTUNIFIED(".");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        PRINTUNIFIED("TLS client closed");

        Gsock.close();

        if(client) {
            delete client;
            client = nullptr;
        }
    }
};

#endif /* _BOTAN_RH_RCLIENT_H_ */
