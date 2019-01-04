#ifndef _BOTAN_RH_RSERVER_H_
#define _BOTAN_RH_RSERVER_H_

// adapted from my_server_2.cpp in botanTlsExample
#include <cstdlib>
#include <cerrno>
#include <list>
#include <thread>

#include "botan_credentials.h"
#include "../ReliableRingBuffer.h"

// FIXME X_PROTOCOL seems to become undefined
//#ifdef X_PROTOCOL
//#include "../gui/x11hashview.h"
//#endif

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

	IDescriptor& Gsock;
	const std::string& server_crt;
	const std::string& server_key;
	TlsServerEventLoopFn eventLoopFn;
	std::unique_ptr<RingBuffer> inRb;
	Basic_Credentials_Manager* creds;
	Botan::TLS::Server* server;

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
	TLS_Server (TlsServerEventLoopFn eventLoopFn_,
				const std::string& server_crt_,
				const std::string& server_key_,
				IDescriptor& Gsock_,
				IDescriptor* local_sock_fd_ = nullptr,
				std::vector<uint8_t>* serializedClientInfo_ = nullptr) :
			eventLoopFn(eventLoopFn_),
			server_crt(server_crt_),
			server_key(server_key_),
			Gsock(Gsock_),
			local_sock_fd(local_sock_fd_),
			creds(nullptr),
			server(nullptr) {
		if (local_sock_fd_ && serializedClientInfo_) { // NON-STANDALONE MODE
			serializedClientInfo = *serializedClientInfo_;
		}
	}

	void tls_emit_data(const uint8_t *data, size_t size) override {
		Gsock.writeAllOrExit(data,size);
	}

	void tls_record_received(uint64_t seq_no, const uint8_t buf[], size_t buf_size) override {
		if (inRb->writeAll(buf,buf_size) < buf_size) exit(9341);
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
		
		auto session_id = session.session_id();
		auto session_ticket = session.session_ticket();
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

            if(local_sock_fd->write(&serializedClientInfo[0],serializedClientInfo.size()) < serializedClientInfo.size()) {
                PRINTUNIFIEDERROR("Unable to atomic write connect info to local socket");
                exit(8563748);
            }
        }
		else {
			// use X11 (when available and enabled) or MFC to show hashview popup
#ifdef _WIN32
			std::thread hvThread(runMFCSessionWithColorGrid,sharedHash);
			hvThread.detach();
#else
#ifdef USE_X11
			std::thread hvThread(runX11SessionWithColorGrid,sharedHash);
			hvThread.detach();
#endif
#endif
		}

		return false; // returning true will cache session for later resumption
	}

	// non-callback, main event loop of server, interacts with ringbuffers and sends/receives TLS packet
	void go() {
		using namespace std::placeholders;

		inRb = std::unique_ptr<RingBuffer>(new RingBuffer);

		PRINTUNIFIED("Serving new client\n");

		Botan::System_RNG rng_;
		Botan::TLS::Session_Manager_In_Memory session_mgr(rng_);
		Botan::TLS::Policy policy;

		// TODO should not give exception, try with noexcept non-pointer local variable and test on all platforms with missing certificate files
		try {creds = new Basic_Credentials_Manager(rng_, server_crt, server_key);}
        catch(std::exception& e) {
			PRINTUNIFIED("Server cert path is: %s\n",server_crt.c_str());
			PRINTUNIFIED("Server key path is: %s\n",server_key.c_str());
            PRINTUNIFIEDERROR("Exception in loading TLS server crt/key: %s\n",e.what());
            exit(-1);
        }

		server = new Botan::TLS::Server(*this,
										session_mgr,
										*creds,
										policy,
										rng_);

		// create mainEventLoop thread passing as input parameters the two ringbuffers
		std::thread mainEventLoopThread(eventLoopFn,std::ref(*inRb),std::ref(*server));
//		mainEventLoopThread.detach();

		try {
			PRINTUNIFIED("In TLS server loop\n");
			while(!server->is_closed()) {
				try {
					uint8_t buf[4*1024] = {0};
					ssize_t got = Gsock.read(buf, sizeof(buf));
					if(got == -1) {
						PRINTUNIFIEDERROR("Error in socket read - %s\n",lastError().c_str());
						threadExit();
					}
					if(got == 0) {
						PRINTUNIFIED("EOF on socket\n");
						threadExit();
					}
					server->received_data(buf, got);
				}
				catch(std::exception& e) {
					PRINTUNIFIEDERROR("Connection problem: %s\n",e.what());
					cleanup();
					break;
				}
			}
		}
		catch(Botan::Exception& e) {
			PRINTUNIFIEDERROR("Security exception: %s\n",e.what());
			cleanup();
		}
		catch (threadExitThrowable& i) {
            PRINTUNIFIEDERROR("T1 Unconditional housekeeping and return");
            cleanup();
		}
		mainEventLoopThread.join();
	}
	
	void cleanup() noexcept {
		inRb->close();
		Gsock.close();
        // local_sock_fd MUST NOT BE CLOSED by SToC threads (it is process-level, any SToC thread can atomically write to it)
        // local_sock_fd MUST NOT BE DELETED as well, must be passed as pointer to local stack variable of caller
		if (server) {
            delete server;
            server = nullptr;
        }
		if (creds) {
            delete creds;
            creds = nullptr;
        }
	}
};

#endif /* _BOTAN_RH_RSERVER_H_ */
