#ifndef _BOTAN_RH_TLS_DESCRIPTOR1_H_
#define _BOTAN_RH_TLS_DESCRIPTOR1_H_

#include <chrono>

#include "botan_credentials.h"
#include "../ReliableRingBuffer.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// TODO remove ABC suffix
void incomingRbMemberFnABC(IDescriptor& netsock, Botan::TLS::Channel& channel, RingBuffer& inRb) {
	uint8_t buf[4096];
	for(;;) {
		ssize_t readBytes = netsock.read(buf,4096);
		if (readBytes <= 0) {
			PRINTUNIFIEDERROR(readBytes==0?"networkSocket EOF\n":"networkSocket read error\n");
			// netsock.close();
            // FIXME this won't work when tlsdesc destructor is called, inRb.close() is nilpotent, no modification of brokenConnection will happen after the first close
			inRb.close(readBytes<0); // propagate broken connection information to ringbuffer
			return;
		}
		try {
			channel.received_data(buf,readBytes); // -> tls_record_received writes into inRb
		}
		catch (Botan::Exception& e) {
			PRINTUNIFIEDERROR("Botan exception: %s",e.what());
//                netsock.close();
			inRb.close();
			return;
		}
	}
}

//~ class ClassicPolicy : public Botan::TLS::Policy {
//~ public:
    //~ bool acceptable_ciphersuite(const Botan::TLS::Ciphersuite &suite) const override {
        //~ // ECDHE_RSA_WITH_AES_256_GCM_SHA384 code = 0xC030
        //~ return suite.ciphersuite_code() == 0xC030;
    //~ }
//~ };

//~ class PostQuantumPolicy : public Botan::TLS::Policy {
//~ public:
    //~ bool acceptable_ciphersuite(const Botan::TLS::Ciphersuite &suite) const override {
//~ //        0x16B7,0x16B8,0x16B9,0x16BA CECPQ1
        //~ auto csc = suite.ciphersuite_code();
        //~ return (csc == 0x16B7 ||
                //~ csc == 0x16B8 ||
                //~ csc == 0x16B9 ||
                //~ csc == 0x16BA);
    //~ }
//~ };

// TODO remove ABC suffix
class TLS_CallbacksABC final : public Botan::TLS::Callbacks {
    using STR = decltype(STRNAMESPACE());
public:

    const bool verifyCertificates; // true for URL download, false for connection to XRE server

    RingBuffer& inRb;
    IDescriptor& Gsock;

    bool setupAborted = false;
    Botan::secure_vector<uint8_t> sharedHash;

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

            Botan::Path_Validation_Result result =
                    Botan::x509_path_validate(cert_chain,
                                              restrictions,
                                              trusted_roots,
                                              hostname,
                                              usage,
                                              std::chrono::system_clock::now(),
                                              std::chrono::milliseconds(1000), // ocsp_timeout
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
                Gsock.shutdown();
                inRb.close(true);
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
        sharedHash = sha256->final();

        PRINTUNIFIED("Master secret's hash for this session is:\n%s\n",Botan::hex_encode(sharedHash).c_str());

        // TODO is tls_session_established called within go() method? or before? In the first case, we have to notify some way

        return false;
    }

    void tls_emit_data(const uint8_t* data, size_t size) override {
        Gsock.writeAllOrExit(data,size);
    }

    void tls_alert(Botan::TLS::Alert alert) override {
        PRINTUNIFIED("Alert: %s\n",alert.type_string().c_str());
        if (alert.type() == Botan::TLS::Alert::Type::CLOSE_NOTIFY) {
            PRINTUNIFIED("TLS endpoint closed connection\n");
            inRb.close(false); // Assume close notify is graceful connection termination
        }
    }

    void tls_record_received(uint64_t seq_no, const uint8_t buf[], size_t buf_size) override {
        if (inRb.writeAll(buf,buf_size) < buf_size) _Exit(91); // RingBuffer is reliable, OK to call exit here
    }

public:
    // constructor using an already connected network socket
    TLS_CallbacksABC(RingBuffer& inRb_,
               IDescriptor& Gsock_,
               bool verifyCertificates_ = false
    ) :
            inRb(inRb_),
            Gsock(Gsock_),
            verifyCertificates(verifyCertificates_)
            {}
};

// TODO remove ABC suffix
class TLSDescriptorABC : public IDescriptor {
private:
    RingBuffer& inRb;
    IDescriptor& netsock;

	std::thread incomingRbThread; // initialized empty, to start use assignment operator=

	// |- converted to pointer
	Botan::TLS::Channel* channel; // Botan::TLS::Server or Botan::TLS::Client, built from Botan::TLS::Callbacks(subclass-> TLS_Client)
	
	// moved from TLS_Client instance variables
	const int serverPort; // 443 for URL download, 11111 for connecting to XRE server
    const bool verifyCertificates; // true for standard https (e.g. url download), false for connection to XRE server
    const std::string sniHost; // empty for connection to xre server
	
	// moved from TLS_Client.go() method
	Botan::System_RNG rng_;
	Basic_Credentials_Manager creds;
	const std::vector<std::string> protocols_to_offer; // empty, split_on ignored
	const Botan::TLS::Protocol_Version::Version_Code version;
	Botan::TLS::Policy policy; // also, PostQuantumPolicy and ClassicPolicy
	
	// |- converted to pointers, from local variables;
	Botan::TLS::Session_Manager_In_Memory* session_mgr;
	TLS_CallbacksABC* callbacks; // formerly, TLS_Client, subclass of Botan::TLS::Callbacks

public:
    TLSDescriptorABC(IDescriptor& netsock_,
                     RingBuffer& inRb_,
                     int serverPort_,
                     bool verifyCertificates_ = false,
                     std::string sniHost_ = "")
            : netsock(netsock_),
              inRb(inRb_),
              serverPort(serverPort_),
              verifyCertificates(verifyCertificates_),
              sniHost(std::move(sniHost_)),
              version(Botan::TLS::Protocol_Version::Version_Code::TLS_V12)
    {}
	
	~TLSDescriptorABC() {
		cleanup();
		if(channel != nullptr) {
			delete channel;
			channel = nullptr;
		}
		if(session_mgr != nullptr) {
			delete session_mgr;
			session_mgr = nullptr;
		}
		if(callbacks != nullptr) {
			delete callbacks;
			callbacks = nullptr;
		}
	}

    Botan::secure_vector<uint8_t> setup() {
		// moved from TLS_Client.go() method
		session_mgr = new Botan::TLS::Session_Manager_In_Memory(rng_);
		
		// ------------------------------------
		callbacks = new TLS_CallbacksABC(inRb, netsock, verifyCertificates);
		// ------------------------------------
		
		channel = new Botan::TLS::Client(*callbacks,
                                        *session_mgr,
                                        creds,
                                        policy,
                                        rng_,
                                        Botan::TLS::Server_Information(sniHost, serverPort),
                                        version,
                                        protocols_to_offer);

		incomingRbThread = std::thread{incomingRbMemberFnABC, std::ref(netsock), std::ref(*channel), std::ref(inRb)}; // thread is started here
		
		PRINTUNIFIED("Waiting for TLS channel to be ready");
        int i=0;
        while(!channel->is_active()) {
            PRINTUNIFIED(".");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if(callbacks->setupAborted || i>50) { // wait till 5 seconds for connection establishment
                PRINTUNIFIEDERROR("Client closed during connection setup, or connection timeout\n");
                return callbacks->sharedHash; // TODO ************************************************ join incomingRbThread, a.k.a. cleanup
            }
            i++;
        }
        PRINTUNIFIED("\nTLS channel ready\n");
        return callbacks->sharedHash;
	}

    void cleanup() {
        inRb.close();

        PRINTUNIFIED("Finished, waiting for TLS client to close...\n");
        if(channel != nullptr) {
			channel->close();
			int i=0;
			while(!channel->is_closed()) { // allow up to 5 seconds for a graceful TLS shutdown
				if(i>10) {
					PRINTUNIFIEDERROR("TLS shutdown taking too much time, closing TCP connection\n");
					goto finishClose;
				}
				PRINTUNIFIED(".");
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				i++;
			}
			PRINTUNIFIED("TLS client closed\n");
		}

        finishClose:
        netsock.shutdown();
        incomingRbThread.join();
    }

	inline ssize_t read(void* buf, size_t count) override { return inRb.read(buf,count); }
	inline ssize_t readAll(void* buf, size_t count) override { return inRb.readAll(buf,count); }
	inline void readAllOrExit(void* buf, size_t count) override { inRb.readAllOrExit(buf,count); }

	// TODO add if channel!=nullptr
	// the actual behaviour of these methods is decided by the emit_data callback, which depends on the Botan::TLS::Callbacks implementation used by the Botan::TLS::Server
	inline ssize_t write(const void* buf, size_t count) override { channel->send((uint8_t*)buf,count); return count; } // send return type is void, assume everything has been written
	ssize_t writeAll(const void* buf, size_t count) override { channel->send((uint8_t*)buf,count); return count; }
	void writeAllOrExit(const void* buf, size_t count) override { channel->send((uint8_t*)buf,count); }
	
	void close() override {inRb.close(); channel->close();}
};

#endif /* _BOTAN_RH_TLS_DESCRIPTOR_H_ */
