#ifndef __XRE_H__
#define __XRE_H__
#include "tls/botan_rh_tls_descriptor.h"
#include "tls/botan_rh_rserver.h"

#ifdef _WIN32
#include "desc/WinsockDescriptor.h"

SOCKET Accept(SOCKET &serv, struct sockaddr_in &client_info) {
    int size = sizeof(client_info);
    SOCKET recv = accept(serv, (sockaddr*)&client_info, &size);
    if ( recv == INVALID_SOCKET )
    {
        std::cout << "-[Invalid Socket.]-" << std::endl;
    }
    return recv;
}

#endif

#ifdef _WIN32
SOCKET rhss = INVALID_SOCKET; // acceptor socket
#else
int rhss = -1; // acceptor socket (assigned only in forked process or in xre mode)
#endif

// with standard #define directives, on 32 bit archs the string went out of scope, so dirty memory instead of actual filenames was read in TLS server ctor -> cert key access error
#ifdef ANDROID_NDK
	const std::string RH_TLS_CERT_STRING = "libdummycrt.so";
	const std::string RH_TLS_KEY_STRING = "libdummykey.so";
#else
	const std::string RH_TLS_CERT_STRING = "dummycrt.pem";
	const std::string RH_TLS_KEY_STRING = "dummykey.pem";
#endif

//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////////////////////////////////////////
// tlRemoteCl.close() not needed?
//~ void on_server_session_exit_func(IDescriptor& tlRemoteCl, std::string& clientIPAndPort) {
    //~ tlRemoteCl.close();
    //~ // PRINTUNIFIED("Server session ended, client disconnected: %s\n",getAndDeleteClientIPAndPortOfThisServerSession().c_str());
    //~ PRINTUNIFIED("Server session ended, client disconnected: %s\n",clientIPAndPort.c_str());
//~ }
//////////////***********************///////////////////////
int rhss_local = -1; // local socket of rh remote server, to communicate arrival/end of client connections (they communicate themselves their arrival/end)
constexpr uint16_t rhServerPort = 11111;

// DO NOT USE -1 or any other negative number as sentinel value here!!! (see man 2 kill)
pid_t rhss_pid = -2; // used by parent process to send INT signal to child if requested by user
// FIXME currently unused, if used again better convert to pid_t* or wrapper struct (negative values are converted to positive in kill call)
//////////////***********************///////////////////////

void on_server_session_exit_func(const std::string& clientIPAndPortOfThisServerSession) {
	PRINTUNIFIED("Server session ended, client disconnected: %s\n",clientIPAndPortOfThisServerSession.c_str());
#ifndef _WIN32
	if (rhss_local > 0) {
		PRINTUNIFIED("End of server session, sending disconnect information...\n");
		uint8_t x = 0xFF; // disconnect info flag
		uint8_t l = clientIPAndPortOfThisServerSession.size();
		std::vector<uint8_t> v;
		v.push_back(x);
		v.push_back(l);
		std::copy(clientIPAndPortOfThisServerSession.begin(),
				clientIPAndPortOfThisServerSession.end(),
				std::back_inserter(v));
		// different threads may write to the same local socket rhss_local
		// so only one write call is allowed in order for it to be atomic
		if (write(rhss_local,&v[0],v.size()) < v.size()) {
			PRINTUNIFIEDERROR("Unable to perform atomic write of session disconnect info");
			_Exit(23);
		}
	}
#endif
}
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

// compatible signature with TlsServerEventLoopFn function type declared in botan_rh_rserver.h
void tlsServerSessionEventLoop(RingBuffer& inRb, Botan::TLS::Server& server) {

    // in order to maintain common interface for actions in the switch,
    // a container of both a RingBuffer and a Botan::TLS::Server implements IDescriptor (rb for read, tls server for write)
    // for local interactions, PosixDescriptor wrapper is used instead
    TLSDescriptor rcl(inRb,server);
    try {
        for (;;) {
            // read request type (1 byte: 5 bits + 3 bits of flags)
            request_type rq = {};
            rcl.readAllOrExit(&rq, sizeof(rq));

            PRINTUNIFIED("request 5-bits received:\t%u\n", rq.request);
            PRINTUNIFIED("request flags (3-bits) received:\t%u\n", rq.flags);

            switch(rq.request) {
                case ACTION_LS:
                    listDir(rcl);
                    break;
                case ACTION_DOWNLOAD:
                    // client sends DOWNLOAD action, server has to UPLOAD data
                    server_download(rcl);
                    break;
                case ACTION_UPLOAD:
                    // client sends UPLOAD action, server has to DOWNLOAD data
                    downloadRemoteItems(rcl);
                    break;
                case ACTION_CREATE:
                    createFileOrDirectory(rcl,rq.flags);
                    break;
                case ACTION_LINK:
                    createHardOrSoftLink(rcl,rq.flags);
                    break;
                case ACTION_STATS:
                    stats(rcl,rq.flags);
                    break;
                case ACTION_HASH:
                    hashFile(rcl);
                    break;
                default: // unlike local ones do at the current time, remote sessions should serve more than one request... On wrong data received, close session and disconnect client
                    PRINTUNIFIED("unexpected data received, disconnected client\n");
                    threadExit();
            }
        }
    }
    // catch (threadExitThrowable& i) { // r.cpp used this, revert if necessary
    catch (...) {	
        PRINTUNIFIEDERROR("T2 ...\n");
    }
    PRINTUNIFIEDERROR("No housekeeping and return\n");
}

#ifdef _WIN32

SOCKET getServerSocket() {
    int iResult;
    WSADATA wsaData;
    SOCKET ListenSocket = INVALID_SOCKET;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        exit(571);
    }

    // Create a SOCKET for connecting to server
    ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ListenSocket == INVALID_SOCKET) {
        PRINTUNIFIEDERROR("socket failed with error: %ld\n", WSAGetLastError());
        WSACleanup();
        exit(572);
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(rhServerPort);

    // Setup the TCP listening socket
    iResult = bind(ListenSocket, (struct sockaddr *)&server, sizeof(server));
    if (iResult == SOCKET_ERROR) {
        PRINTUNIFIEDERROR("bind failed with error: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        exit(573);
    }

    iResult = listen(ListenSocket, 20);
    if (iResult == SOCKET_ERROR) {
        PRINTUNIFIEDERROR("listen failed with error: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        exit(574);
    }

    PRINTUNIFIED("remote rhServer acceptor started\n");

    return ListenSocket;
}

std::string getIPAndPortFromDesc(struct sockaddr_in& client) {
    char *connected_ip = inet_ntoa(client.sin_addr);
    int port = ntohs(client.sin_port);
    std::stringstream ss;

    if (client.sin_family == AF_INET) {
        ss << connected_ip << ":" << port;
    }
    else {
        ss << "[" << connected_ip << "]:" << port;
    }
    
    return ss.str();
}

void tlsServerSession(SOCKET remoteCl, std::string s) {
    WinsockDescriptor wsd(remoteCl);
    TLS_Server tlsServer(tlsServerSessionEventLoop,RH_TLS_CERT_STRING,RH_TLS_KEY_STRING,wsd);
    tlsServer.go();
    //~ on_server_session_exit_func(wsd,s);
    on_server_session_exit_func(s);
}

void acceptLoop(SOCKET rhss) {
    for(;;) {
        struct sockaddr_in client_info;
        memset(&client_info,0,sizeof(client_info));
        SOCKET ClientSocket = Accept(rhss, client_info);
        if (ClientSocket == INVALID_SOCKET) {
            printf("accept failed with error: %d\n", WSAGetLastError());
            closesocket(rhss);
            WSACleanup();
            exit(1);
        }
        
        std::string s = getIPAndPortFromDesc(client_info);
        PRINTUNIFIED("New client connected: %s\n",s.c_str());
        
        std::thread serverToClientThread(tlsServerSession,ClientSocket,s);
        serverToClientThread.detach();
    }
}
#else

int getServerSocket() {
    int rhss = socket(AF_INET, SOCK_STREAM, 0);
    if(rhss == -1) {
        PRINTUNIFIEDERROR("Unable to create TLS server socket\n");
        exit(-1);
    }

    struct sockaddr_in socket_info = {};
    socket_info.sin_family = AF_INET;
    socket_info.sin_port = htons(rhServerPort);

    socket_info.sin_addr.s_addr = INADDR_ANY;

    if(bind(rhss, reinterpret_cast<struct sockaddr*>(&socket_info), sizeof(struct sockaddr)) != 0) {
        close(rhss);
        PRINTUNIFIEDERROR("TLS server bind failed\n");
        exit(-1);
    }

    // up to 10 concurrent clients (both with 2 sessions) allowed
    if(listen(rhss, MAX_CLIENTS) != 0) {
        close(rhss);
        PRINTUNIFIEDERROR("TLS server listen failed\n");
        exit(-1);
    }

    PRINTUNIFIED("remote rhServer acceptor started\n");
    return rhss;
}

std::string getIPAndPortFromDesc(int desc) {
    socklen_t len;
    struct sockaddr_storage addr;
    memset(&addr,0,sizeof(struct sockaddr_storage));
    char ipstr[INET6_ADDRSTRLEN]={};
    int port;
    len = sizeof addr;
    getpeername(desc,(struct sockaddr*)&addr, &len);

    // deal with both IPv4 and IPv6:
    if (addr.ss_family == AF_INET) {
        auto s = (struct sockaddr_in *)&addr;
        port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
    } else { // AF_INET6
        auto s = (struct sockaddr_in6 *)&addr;
        port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
    }

    std::stringstream ss;

    if (addr.ss_family == AF_INET) {
        ss << ipstr << ":" << port;
    }
    else {
        ss << "[" << ipstr << "]:" << port;
    }

    return ss.str();
}

void tlsServerSession(int remoteCl) {
	PosixDescriptor pd_remoteCl(remoteCl);	
	// send client ip and port to java client over rhss_local
	std::string clientIPAndPortOfThisServerSession = getIPAndPortFromDesc(remoteCl);
	PRINTUNIFIED("New client connected: %s\n",clientIPAndPortOfThisServerSession.c_str());
	
	if(rhss_local > 0) {
		PRINTUNIFIED("Sending new connection info over local socket");
		std::vector<uint8_t> connectInfo;
		uint8_t x = 0; // connect flag
		uint8_t clientIPAndPortLen = clientIPAndPortOfThisServerSession.size();
		connectInfo.push_back(x);
		connectInfo.push_back(clientIPAndPortLen);
		std::copy(clientIPAndPortOfThisServerSession.begin(),
				clientIPAndPortOfThisServerSession.end(),
				std::back_inserter(connectInfo));

		// send connect info has been moved into botan_rh_rserver in session establishment callback

		// for tls, send handshake info (hash of shared secret) over rhss_local
	
		PosixDescriptor pd_rhss_local(rhss_local);
		TLS_Server tlsServer(tlsServerSessionEventLoop,RH_TLS_CERT_STRING,RH_TLS_KEY_STRING,pd_remoteCl,&pd_rhss_local,&connectInfo);
		tlsServer.go();
	}
	else {
		TLS_Server tlsServer(tlsServerSessionEventLoop,RH_TLS_CERT_STRING,RH_TLS_KEY_STRING,pd_remoteCl);
		tlsServer.go();
	}
	
	// at the end of the thread (after join of ringbuffer thread), send disconnect info
	on_server_session_exit_func(clientIPAndPortOfThisServerSession);
}

void acceptLoop(int& rhss) {
    const struct linger lo = {1, 0};
    for(;;) {
        struct sockaddr_in st{};
        socklen_t stlen{};
        int remoteCl = accept(rhss,(struct sockaddr *)&st,&stlen); // (#1) peer info retrieved and converted to string in spawned thread
        if (remoteCl == -1) {
            PRINTUNIFIEDERROR("accept error on remote server\n");
            PRINTUNIFIEDERROR("ERRNO is %d\n",errno);
            continue;
        }
        
        // this is needed in order for (client) write operations to fail when the remote (server) socket is closed
		// (e.g. client is performing unacked writing and server disconnects)
		// so that client does not hangs when server abruptly closes connections
		setsockopt(remoteCl, SOL_SOCKET, SO_LINGER, &lo, sizeof(lo));

        std::thread serverToClientThread(tlsServerSession,remoteCl);
        serverToClientThread.detach();
    }
}
#endif

#ifdef _WIN32
	BOOL WINAPI exitHr(DWORD unused) {
		_Exit(0);
		return true;
	}
#endif


void printNetworkInfo() {
#ifdef _WIN32
    system("ipconfig");
#else
    system("ifconfig");
#endif
}

inline void registerExitRoutines() {
#ifdef _WIN32
    SetConsoleCtrlHandler(exitHr,true);
#endif
}
#endif /* __XRE_H__ */
