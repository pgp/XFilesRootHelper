#ifndef _RH_NETFACTORY_
#define _RH_NETFACTORY_

#include "IDescriptor.h"
#include <cerrno>
#include <memory>

/**
 Factory for creating TCP client sockets
 */

#ifdef _WIN32
#include "WinsockDescriptor.h"

SOCKET connect_with_timeout(const char* ipaddr, int port, unsigned timeout_seconds) {
    struct sockaddr_in server{};

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(ipaddr);
    server.sin_port = htons(port);

    // ipaddr valid?
    if(server.sin_addr.s_addr == INADDR_NONE) return INVALID_SOCKET;
    // std::cout<<"1..."<<std::endl;
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    // std::cout<<"2..."<<std::endl;
    // put socket in non-blocking mode...
    u_long block = 1;
    if(ioctlsocket(sock, FIONBIO, &block) == SOCKET_ERROR) {
        printf("Connect failed! Error: %d", WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    if(connect(sock, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        if(WSAGetLastError() != WSAEWOULDBLOCK) {
            // connection failed
            closesocket(sock);
            return INVALID_SOCKET;
        }

        // connection pending
        fd_set setW, setE;

        FD_ZERO(&setW);
        FD_SET(sock, &setW);
        FD_ZERO(&setE);
        FD_SET(sock, &setE);

        timeval time_out{};
        time_out.tv_sec = timeout_seconds;
        time_out.tv_usec = 0;

        int ret = select(0, nullptr, &setW, &setE, &time_out);

        if(ret <= 0) {
            // select() failed or connection timed out
            closesocket(sock);
            if (ret == 0)
                WSASetLastError(WSAETIMEDOUT);
            return INVALID_SOCKET;
        }

        if(FD_ISSET(sock, &setE)) {
            // connection failed
            int err = 0;
            int s_err = sizeof(err);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &s_err);
            closesocket(sock);
            WSASetLastError(err);
            return INVALID_SOCKET;
        }
    }

    // connection successful
    // std::cout<<"9..."<<std::endl;
    // put socked in blocking mode...
    block = 0;
    if(ioctlsocket(sock, FIONBIO, &block) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    // std::cout<<"10..."<<std::endl;
    return sock;
}

SOCKET resolve_and_connect_with_timeout(const std::string& domainOnly, int port = 443, int timeout_seconds = 2) {
    DWORD dwError;
    int i = 0;
    struct hostent *remoteHost;
    struct in_addr addr;
    char **pAlias;
    const char* host_name = domainOnly.c_str();

    printf("Calling gethostbyname with %s\n", host_name);
    remoteHost = gethostbyname(host_name);

    if (remoteHost == nullptr) {
        dwError = WSAGetLastError();
        if (dwError != 0) {
            if (dwError == WSAHOST_NOT_FOUND) {
                printf("Host not found\n");
                return INVALID_SOCKET;
            } else if (dwError == WSANO_DATA) {
                printf("No data record found\n");
                return INVALID_SOCKET;
            } else {
                printf("Function failed with error: %ld\n", dwError);
                return INVALID_SOCKET;
            }
        }
    } else {
        printf("Function returned:\n");
        printf("\tOfficial name: %s\n", remoteHost->h_name);
        for (pAlias = remoteHost->h_aliases; *pAlias != 0; pAlias++) {
            printf("\tAlternate name #%d: %s\n", ++i, *pAlias);
        }
        printf("\tAddress type: ");
        switch (remoteHost->h_addrtype) {
        case AF_INET:
            printf("AF_INET\n");
            break;
        case AF_NETBIOS:
            printf("AF_NETBIOS\n");
            break;
        default:
            printf(" %d\n", remoteHost->h_addrtype);
            break;
        }
        printf("\tAddress length: %d\n", remoteHost->h_length);

        i = 0;
        if (remoteHost->h_addrtype == AF_INET)
        {
            while (remoteHost->h_addr_list[i] != 0) {
                addr.s_addr = *(u_long *) remoteHost->h_addr_list[i++];
                char* currentAddr = inet_ntoa(addr);
                printf("\tTrying connecting to IP Address #%d: %s\n", i, currentAddr);
                auto currentSock = connect_with_timeout(currentAddr, port, timeout_seconds);
                if(currentSock != INVALID_SOCKET)
                    return currentSock;
            }
        }
        else if (remoteHost->h_addrtype == AF_NETBIOS)
        {
            printf("NETBIOS address was returned\n");
        }
    }

    return INVALID_SOCKET;
}

class NetworkDescriptorFactory {
public:
    WinsockDescriptor create(const std::string& host, int port = 443, int timeout = 2) {
        return {resolve_and_connect_with_timeout(host, port, timeout)};
    }

    WinsockDescriptor* createNew(const std::string& host, int port = 443, int timeout = 2) {
        return new WinsockDescriptor(resolve_and_connect_with_timeout(host, port, timeout));
    }
};

#else
#include "PosixDescriptor.h"

int connect_with_timeout(int& sock_fd, struct addrinfo* p, unsigned timeout_seconds) {
    int res;
    //~ struct sockaddr_in addr;
    long arg;
    fd_set myset;
    struct timeval tv{};
    int valopt;
    socklen_t lon;

    // Create socket
    // sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

    if (sock_fd < 0) {
        PRINTUNIFIEDERROR("Error creating socket (%d %s)\n", errno, strerror(errno));
        return -1;
    }

    // Set non-blocking
    if( (arg = fcntl(sock_fd, F_GETFL, nullptr)) < 0) {
        PRINTUNIFIEDERROR("Error fcntl(..., F_GETFL) (%s)\n", strerror(errno));
        return -2;
    }
    arg |= O_NONBLOCK;
    if( fcntl(sock_fd, F_SETFL, arg) < 0) {
        PRINTUNIFIEDERROR("Error fcntl(..., F_SETFL) (%s)\n", strerror(errno));
        return -3;
    }
    // Trying to connect with timeout
    // res = connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr));
    res = connect(sock_fd, p->ai_addr, p->ai_addrlen);
    if (res < 0) {
        if (errno == EINPROGRESS) {
            PRINTUNIFIEDERROR("EINPROGRESS in connect() - selecting\n");
            for(;;) {
                tv.tv_sec = timeout_seconds;
                tv.tv_usec = 0;
                FD_ZERO(&myset);
                FD_SET(sock_fd, &myset);
                res = select(sock_fd+1, nullptr, &myset, nullptr, &tv);
                if (res < 0 && errno != EINTR) {
                    PRINTUNIFIEDERROR("Error connecting %d - %s\n", errno, strerror(errno));
                    return -4;
                }
                else if (res > 0) {
                    // Socket selected for write
                    lon = sizeof(int);
                    if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) {
                        PRINTUNIFIEDERROR("Error in getsockopt() %d - %s\n", errno, strerror(errno));
                        return -5;
                    }
                    // Check the value returned...
                    if (valopt) {
                        PRINTUNIFIEDERROR("Error in delayed connection() %d - %s\n", valopt, strerror(valopt));
                        return -6;
                    }
                    break;
                }
                else {
                    PRINTUNIFIEDERROR("Timeout in select() - Cancelling!\n");
                    return -7;
                }
            }
        }
        else {
            PRINTUNIFIEDERROR("Error connecting %d - %s\n", errno, strerror(errno));
            return -8;
        }
    }
    // Set to blocking mode again...
    if( (arg = fcntl(sock_fd, F_GETFL, nullptr)) < 0) {
        PRINTUNIFIEDERROR("Error fcntl(..., F_GETFL) (%s)\n", strerror(errno));
        return -9;
    }
    arg &= (~O_NONBLOCK);
    if( fcntl(sock_fd, F_SETFL, arg) < 0) {
        PRINTUNIFIEDERROR("Error fcntl(..., F_SETFL) (%s)\n", strerror(errno));
        return -10;
    }
    return 0; // ok, at this point the socket is connected and again in blocking mode
}

int resolve_and_connect_with_timeout(const std::string& domainOnly, int port = 443, int timeout_seconds = 2) {
    int remoteCl = -1;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    PRINTUNIFIED("Populating hints...\n");
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // use AF_INET to force IPv4, AF_INET6 to force IPv6, AF_UNSPEC to allow both
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string port_s = std::to_string(port);

    PRINTUNIFIED("Invoking getaddrinfo for %s\n",domainOnly.c_str());
    if ((rv = getaddrinfo(domainOnly.c_str(), port_s.c_str(), &hints, &servinfo)) != 0) {
        PRINTUNIFIEDERROR("getaddrinfo error: %s\n", gai_strerror(rv));
        errno = 0x313131;
        return -1;
    }

    PRINTUNIFIED("Looping through getaddrinfo results...\n");
    // loop through all the results and connect to the first we can
    for(p = servinfo; p != nullptr; p = p->ai_next) {
        PRINTUNIFIED("getaddrinfo item\n");

        rv = connect_with_timeout(remoteCl, p, timeout_seconds);
        if (rv == 0) break;
        else {
            PRINTUNIFIEDERROR("Timeout or connection error %d\n",rv);
            close(remoteCl);
        }
    }
    PRINTUNIFIED("getaddrinfo end results\n");

    if (p == nullptr) {
        freeaddrinfo(servinfo);
        PRINTUNIFIED("Could not create socket or connect\n");
        errno = 0x323232;
        return -1;
    }
    PRINTUNIFIED("freeaddrinfo...\n");
    freeaddrinfo(servinfo);
    return remoteCl;
}


class NetworkDescriptorFactory {
public:
    PosixDescriptor create(const std::string& host, int port = 443, int timeout = 2) {
        return {resolve_and_connect_with_timeout(host, port, timeout)};
    }

    PosixDescriptor* createNew(const std::string& host, int port = 443, int timeout = 2) {
        return new PosixDescriptor(resolve_and_connect_with_timeout(host, port, timeout));
    }
};
#endif

NetworkDescriptorFactory netfactory;

#endif
