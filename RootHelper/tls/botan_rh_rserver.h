#ifndef _BOTAN_RH_RSERVER_H_
#define _BOTAN_RH_RSERVER_H_

#include <cerrno>
#include <list>

#include "botan_credentials.h"
#include "../ReliableRingBuffer.h"

#ifdef _WIN32
#include "../gui/mfchashview.h"
#else
#ifdef USE_X11
#include "../gui/x11hashview.h"
#endif
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

typedef void (*TlsServerEventLoopFn) (RingBuffer& inRb, Botan::TLS::Server& server);

class TLS_Server final : public Botan::TLS::Callbacks {
private:
	IDescriptor* local_sock_fd; // UNUSED FOR STANDALONE RHSS, for communicating shared session hash to RH client once session establishment is complete
	std::vector<uint8_t> serializedClientInfo; // UNUSED FOR STANDALONE RHSS, pre-populated by constructor caller, filled with shared secret hash and sent to local_sock_fd

	Botan::TLS::Policy policy;
	Botan::System_RNG rng_;
	Botan::TLS::Session_Manager_In_Memory session_mgr;

	IDescriptor& Gsock;
	const std::string& server_crt;
	const std::string& server_key;
	TlsServerEventLoopFn eventLoopFn;
	RingBuffer inRb;
	Basic_Credentials_Manager& creds; // server_crt and server_key are ignored, if this is supplied in constructor
	Botan::TLS::Server server;

	std::string lastError() {
#ifdef _WIN32
		char*s = nullptr;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					   NULL, WSAGetLastError(),
					   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					   (LPTSTR)&s, 0, NULL);
        std::string ret = std::string("error code: ")+std::to_string(WSAGetLastError())+"\tError msg: "+s;
		LocalFree(s);
		return ret;
#else
        return std::string("error code: ")+std::to_string(errno)+"\tError msg: "+strerror(errno);
#endif
	}

public:
	TLS_Server(TlsServerEventLoopFn eventLoopFn_,
			   const std::string& server_crt_,
			   const std::string& server_key_,
			   IDescriptor& Gsock_,
			   Basic_Credentials_Manager& creds_,
			   IDescriptor* local_sock_fd_ = nullptr,
			   std::vector<uint8_t> serializedClientInfo_ = {}) :
			eventLoopFn(eventLoopFn_),
			server_crt(server_crt_),
			server_key(server_key_),
			Gsock(Gsock_),
			local_sock_fd(local_sock_fd_),
			creds(creds_),
			serializedClientInfo(serializedClientInfo_),
			session_mgr(rng_),
			server(*this,
				   session_mgr,
				   creds,
				   policy,
				   rng_) {}

	void tls_emit_data(const uint8_t *data, size_t size) override {
		Gsock.writeAllOrExit(data,size);
	}

	void tls_record_received(uint64_t seq_no, const uint8_t buf[], size_t buf_size) override {
		if(inRb.writeAll(buf,buf_size) < buf_size) _Exit(131);
	}

	void tls_alert(Botan::TLS::Alert alert) override {
		PRINTUNIFIED("Alert: %s\n",alert.type_string().c_str());
		if (alert.type() == Botan::TLS::Alert::Type::CLOSE_NOTIFY) {
			PRINTUNIFIED("TLS endpoint closed connection");
			threadExit();
		}
	}

	bool tls_session_established(const Botan::TLS::Session &session) override {
		PRINTUNIFIED("Handshake complete, %s using %s\n",session.version().to_string().c_str(),
							session.ciphersuite().to_string().c_str());
		// auto&& session_id = session.session_id();
		// auto&& session_ticket = session.session_ticket();
		auto master_secret = session.master_secret();

		// NEW: SHA256 -> 32 bytes binary data ////////////////////
		std::unique_ptr<Botan::HashFunction> sha256(Botan::HashFunction::create("SHA-256"));
		sha256->update(master_secret.data(),master_secret.size());
		Botan::secure_vector<uint8_t> sharedHash = sha256->final();
		
		PRINTUNIFIED("Master secret's hash for this session is:\n%s\n",Botan::hex_encode(sharedHash).c_str());

        if(local_sock_fd) { // NON-STANDALONE MODE
            std::copy(sharedHash.begin(),
                      sharedHash.end(),
                      std::back_inserter(serializedClientInfo));
			auto size = serializedClientInfo.size();
            if(local_sock_fd->write(&serializedClientInfo[0], size) < size) {
                PRINTUNIFIEDERROR("Unable to atomic write connect info to local socket");
                _Exit(127);
            }
        }
        else {
			// use X11 (when available and enabled) - or MFC on Windows - to show hashview popup
            // beware, runSessionWithColorGrid resolves to
            // two different functions with same name depending on the OS
#if defined(_WIN32) || defined(USE_X11)
			std::thread hvThread(runSessionWithColorGrid,sharedHash);
			hvThread.detach();
#endif
		}

		return false; // returning true will cache session for later resumption
	}

	// non-callback, main event loop of server, interacts with ringbuffers and sends/receives TLS packet
	void go() {
		PRINTUNIFIED("Serving new client\n");
		std::thread mainEventLoopThread(eventLoopFn,std::ref(inRb),std::ref(server));

		try {
			PRINTUNIFIED("In TLS server loop\n");
			while(!server.is_closed()) {
				try {
					uint8_t buf[4096]{};
					ssize_t got = Gsock.read(buf, sizeof(buf));
					if(got == -1) {
						PRINTUNIFIEDERROR("Error in socket read - %s\n",lastError().c_str());
						threadExit();
					}
					if(got == 0) {
						PRINTUNIFIED("EOF on socket\n");
						threadExit();
					}
					server.received_data(buf, got);
				}
				catch(std::exception& e) {
					PRINTUNIFIEDERROR("Connection problem: %s\n",e.what());
					break;
				}
			}
		}
		catch(Botan::Exception& e) {
			PRINTUNIFIEDERROR("Security exception: %s\n",e.what());
		}
		catch (threadExitThrowable& i) {
            PRINTUNIFIEDERROR("T1 Unconditional housekeeping and return\n");
		}
		cleanup();
		mainEventLoopThread.join();
	}

    void cleanup() noexcept {
        inRb.close();
        Gsock.shutdown();
        // local_sock_fd MUST NOT BE CLOSED by SToC threads (it is process-level, any SToC thread can atomically write to it)
        // local_sock_fd MUST NOT BE DELETED as well, must be passed as pointer to local stack variable of caller
    }
};

#endif /* _BOTAN_RH_RSERVER_H_ */
