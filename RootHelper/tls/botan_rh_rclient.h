#ifndef _BOTAN_RH_RCLIENT_H_
#define _BOTAN_RH_RCLIENT_H_

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


class TLS_Client;

//typedef void (*TlsClientEventLoopFn) (RingBuffer& inRb, Botan::TLS::Client& client, IDescriptor& local_sock_fd);
typedef void (*TlsClientEventLoopFn) (TLS_Client& client_wrapper);

class TLS_Client final : public Botan::TLS::Callbacks {
public:

    const int serverPort; // 443 for URL download, 11111 for connecting to XRE server

    const bool verifyCertificates; // true for URL download, false for connection to XRE server
    const std::string sniHost; // only for URL download
    const std::string getString; // only for URL download
    const std::string downloadPath; // only for URL download
    const std::string targetFilename; // only for URL download
    int httpRet; // onyl for URL download, to be read from caller in order to decide whether follow redirect or not
    std::string locationToRedirect; // onyl for URL download, to be read from caller
    const bool downloadToFile;

    RingBuffer& inRb;
    TlsClientEventLoopFn eventLoopFn;
    IDescriptor& Gsock;
    IDescriptor& local_sock_fd; // for communicating shared session hash to RH client once session establishment is complete
    Botan::TLS::Client* client;

    bool setupAborted = false;

    // returns socket or -1
    int connect_tcp_socket(std::string& domain, int port=443) {
        // TODO add socket factory in desc folder (client network sockets for Posix and Windows)
        return -1;
    }

	// local_socket passed for closing it by here when this thread terminates before the other, in so avoiding deadlock
    static void incomingRbMemberFn(IDescriptor& networkSocket, Botan::TLS::Client& client, IDescriptor& local_socket, IDescriptor& ringBuffer) {
        for(;;) {
            std::vector<uint8_t> buf(16384,0);
            ssize_t readBytes = networkSocket.read(&buf[0],16384);
            if (readBytes <= 0) {
                PRINTUNIFIEDERROR(readBytes==0?"networkSocket EOF\n":"networkSocket read error\n");
                networkSocket.close();
                ringBuffer.close();
                return;
            }
            buf.resize(readBytes);
            try {
                client.received_data(buf); // -> tls_record_received writes into ringbuffer
            }
            catch (Botan::Exception& e) {
                PRINTUNIFIEDERROR("Botan exception: %s",e.what());
                networkSocket.close();
                ringBuffer.close();
                return;
            }
        }
    }

    void tls_verify_cert_chain(
            const std::vector<Botan::X509_Certificate>& cert_chain,
            const std::vector<std::shared_ptr<const Botan::OCSP::Response>>& ocsp,
            const std::vector<Botan::Certificate_Store*>& trusted_roots,
            Botan::Usage_Type usage,
            const std::string& hostname,
            const Botan::TLS::Policy& policy) override {
        if(verifyCertificates) {
            if(cert_chain.empty())
                throw std::invalid_argument("Certificate chain was empty");

            Botan::Path_Validation_Restrictions restrictions(policy.require_cert_revocation_info(),
                                                             policy.minimum_signature_strength());

            auto ocsp_timeout = std::chrono::milliseconds(1000);

            Botan::Path_Validation_Result result =
                    Botan::x509_path_validate(cert_chain,
                                              restrictions,
                                              trusted_roots,
                                              hostname,
                                              usage,
                                              std::chrono::system_clock::now(),
                                              ocsp_timeout,
                                              ocsp);

            std::cout << "Certificate validation status: " << result.result_string() << "\n";
            if(result.successful_validation()) {
                auto status = result.all_statuses();
                std::cout << "Cert chain validation OK" << std::endl;

                if(!status.empty() && status[0].count(Botan::Certificate_Status_Code::OCSP_RESPONSE_GOOD))
                    std::cout << "Valid OCSP response for this server\n";
            }
            else {
                PRINTUNIFIEDERROR("Certificate verification failed\n");
                Gsock.close();
                inRb.close();
                setupAborted = true;
            }
        }
    }

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
            PRINTUNIFIED("TLS endpoint closed connection\n");
            Gsock.close();
            inRb.close();
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
               RingBuffer& inRb_,
               IDescriptor& local_sock_fd_,
               IDescriptor& Gsock_,
               bool verifyCertificates_ = false,
               std::string sniHost_ = "",
               std::string getString_ = "",
               int serverPort_=11111,
               std::string downloadPath_ = "",
               std::string targetFilename_ = "",
               const bool downloadToFile_ = true
    ) :
            eventLoopFn(eventLoopFn_),
            inRb(inRb_),
            local_sock_fd(local_sock_fd_),
            Gsock(Gsock_),
            verifyCertificates(verifyCertificates_),
            sniHost(std::move(sniHost_)),
            getString(std::move(getString_)),
            serverPort(serverPort_),
            downloadPath(std::move(downloadPath_)),
            targetFilename(std::move(targetFilename_)),
            downloadToFile(downloadToFile_),
            httpRet(-1),
            client(nullptr) {}

    void go() {
        Botan::System_RNG rng_;
        Botan::TLS::Session_Manager_In_Memory session_mgr(rng_);
        const std::string next_protos;
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
                                  Botan::TLS::Server_Information(sniHost, serverPort),
                                  version,
                                  protocols_to_offer);

        // here start helper network thread (filling incomingRb)
        std::thread incomingRbThread(incomingRbMemberFn,
                                     std::ref(Gsock),
                                     std::ref(*client),
                                     std::ref(local_sock_fd),
                                     std::ref(inRb)
        );

        PRINTUNIFIED("Waiting for TLS channel to be ready");
        while(!client->is_active()) {
            PRINTUNIFIED(".");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if(setupAborted) {
                PRINTUNIFIEDERROR("Client closed during connection setup\n");
                cleanup();
                goto joinThread;
            }
        }
        PRINTUNIFIED("\nTLS channel ready\n");

        try {
//            eventLoopFn(inRb,std::ref(*client),local_sock_fd); // TLS client interacts with local socket
            eventLoopFn(std::ref(*this)); // TLS client interacts with local socket
        }
        catch (threadExitThrowable& i) {
            PRINTUNIFIEDERROR("T1 Unconditional housekeeping and return\n");
            cleanup();
        }

        joinThread:
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
