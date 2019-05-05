#ifndef __BASIC_HTTPS_CLIENT__
#define __BASIC_HTTPS_CLIENT__

#include <sstream>
#include "../iowrappers_common.h"
#include "../desc/PosixDescriptor.h"
#include "botan_rh_rclient.h"

std::string getHttpFilename(const std::string& hdrs, const std::string& url) {
    std::istringstream ss(hdrs);
    std::string line;
    const std::string cdPrefix = "Content-Disposition: ";
    while(std::getline(ss,line)) {
        if(line.find(cdPrefix)==0) {
            auto rawIdx = line.find('=');
            if (rawIdx != std::string::npos)
                return line.substr(rawIdx+1);
        }
    }

    auto lastIdx = url.find_last_of('/');
    if(lastIdx != std::string::npos){
        auto x = url.substr(lastIdx+1);
        if(!x.empty()) return x;
    }
    return "file.bin";
}

// FIXME this should go in a separate header, it is used by XRE TLS client as well
// defaults to 5 seconds timeout
int connect_with_timeout(int& sock_fd, struct addrinfo* p, unsigned timeout_seconds = 5) {
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

// return value: HTTP response code or -1 for unsolvable error
int parseHttpResponseHeadersAndBody(IDescriptor& fd, IDescriptor& local_fd, const std::string& downloadPath, const std::string& targetFilename, std::string& hdrs, const std::string& url) {
    uint64_t currentProgress = 0, last_progress = 0;
    std::stringstream headers;
    std::ofstream body(downloadPath);

    // headers

    for(;;) {
        std::string buffer(4096,0);
        auto readBytes=fd.read((char*)(buffer.c_str()), 4096);
        if(readBytes<=0) {
            PRINTUNIFIEDERROR("EOF or error reading headers, errno is %d\n",errno);
            return -1;
        }

        // don't expect this to happen on first received packet (must contain at least HTTP/*.* header, longer than 4 bytes)
        // anyway, it could happen on last packet
        if(readBytes<4) {
            PRINTUNIFIEDERROR("WARNING: Read less than 4 byte on current chunk");
            if (headers.str().size()+readBytes < 4)
                throw std::runtime_error("Header buffer still empty, and not enough bytes to detect double CRLF");
            else {
                // borrow 4-readBytes bytes from headers accumulated so far
                buffer.resize(readBytes);
                auto currentHeader = headers.str();
                // reset headers
                std::stringstream tmpss;
                headers.swap(tmpss);

                auto splittingLength = currentHeader.size()-4+readBytes;
                headers<<currentHeader.substr(0,splittingLength);
                buffer = currentHeader.substr(splittingLength) + buffer;
                // here, it is guaranteed buffer length will be == 4
            }
        }
        else buffer.resize(readBytes);

        if(::memcmp(buffer.c_str()+readBytes-4,"\r\n\r\n",4)==0) {
            headers<<buffer;
            goto body_tag;
        }
        else if (::memcmp(buffer.c_str()+readBytes-3,"\r\n\r",3)==0) {
            char c;
            if(fd.read(&c, 1) < 1 || c != '\n')
                throw std::runtime_error("header parsing error on 1-byte look-ahead");
            headers<<buffer;
            headers<<c;
            goto body_tag;
        }
        else if (::memcmp(buffer.c_str()+readBytes-2,"\r\n",2)==0) {
            char cc[2]{};
            if(fd.read(cc, 2) < 2 || ::memcmp(cc,"\r\n",2) !=0 )
                throw std::runtime_error("header parsing error on 2-byte look-ahead");
            headers<<buffer;
            headers<<cc;
            goto body_tag;
        }
        else if (buffer[readBytes-1] == '\r') {
            int rb;
            char ccc[3]{};
            if((rb = fd.read(ccc, 3)) < 3) {
                throw std::runtime_error("header parsing error on 3-byte look-ahead -- not enough bytes read: "+std::to_string(rb));
            }
            if(::memcmp(ccc,"\n\r\n",3) !=0) {
                throw std::runtime_error("header parsing error on 3-byte look-ahead (memcmp failed)");
            }
            headers<<buffer;
            headers<<ccc;
            goto body_tag;
        }
        else {
            // header boundary may already have been passed, check for \r\n\r\n
            for(int i=0;i<readBytes-4;i++) {
                if(::memcmp(buffer.c_str()+i,"\r\n\r\n",4)==0) {
                    headers<<buffer.substr(0,i+4);
                    auto bodystart = buffer.substr(i+4);
                    currentProgress += bodystart.length();
                    body<<bodystart;
                    goto body_tag;
                }
            }
            headers<<buffer;
        }
    }

    // body
    body_tag:

    hdrs = headers.str();
    std::cout<<hdrs;

    PRINTUNIFIED("Retrieving HTTP response code...");
    auto tmpIdx = hdrs.find("\r\n");
    if (tmpIdx == std::string::npos) throw std::runtime_error("Cannot find CRLF token in headers");
    auto firstLine = hdrs.substr(0,tmpIdx); // "HTTP/1.1 301 Moved Permanently"
    PRINTUNIFIED("[0]%s\n",firstLine.c_str());
    tmpIdx = firstLine.find(' ');
    if (tmpIdx == std::string::npos) throw std::runtime_error("No first space found in first header line");
    firstLine = firstLine.substr(tmpIdx+1); // "301 Moved Permanently"
    PRINTUNIFIED("[1]%s\n",firstLine.c_str());
    tmpIdx = firstLine.find(' ');
    if (tmpIdx == std::string::npos) throw std::runtime_error("No second space found in first header line");
    firstLine = firstLine.substr(0,tmpIdx); // "301"
    PRINTUNIFIED("[2]%s\n",firstLine.c_str());
    int httpRet = ::strtol(firstLine.c_str(),nullptr,10);
    if (httpRet < 100) {
        PRINTUNIFIEDERROR("Error parsing HTTP response code, firstLine now is %s\n",firstLine.c_str());
    }
    if (httpRet != 200) return httpRet;

    constexpr uint8_t endOfRedirects = 0x11;
    local_fd.writeAllOrExit(&endOfRedirects,sizeof(uint8_t));

    PRINTUNIFIED("Finding a valid filename to download to, if not explicitly provided...\n");
    auto httpFilename = targetFilename.empty()?getHttpFilename(hdrs,url):targetFilename;
    PRINTUNIFIED("Assigned download filename is %s\n",httpFilename.c_str());

    PRINTUNIFIED("Finding content length... (part of body may have been already received)\n");

    std::string contentLengthPattern = "Content-Length: ";
    auto contentLengthIdx = hdrs.find(contentLengthPattern);
    uint64_t parsedContentLength = -1;
    if(contentLengthIdx == std::string::npos) {
        PRINTUNIFIEDERROR("No content-length provided, progress won't be available\n");
    }
    else {
        auto substrAfterContentLength = hdrs.substr(contentLengthIdx+contentLengthPattern.length());
        // find first \r
        auto crIdx = substrAfterContentLength.find('\r');
        if(crIdx == std::string::npos) {
            throw std::runtime_error("Protocol error, expected \\r somewhere after content-length");
        }
        auto substrContentLengthField = substrAfterContentLength.substr(0,crIdx);
        parsedContentLength = ::strtoll(substrContentLengthField.c_str(),nullptr,10);
        PRINTUNIFIEDERROR("PARSED CONTENT LENGTH IS: %" PRIu64 ,parsedContentLength);
    }
    local_fd.writeAllOrExit(&parsedContentLength,sizeof(uint64_t)); // send total size

    for(;;) {
        std::string buf(4096,0);
        auto readBytes = fd.read((char*)(buf.c_str()), 4096);
        if(readBytes<=0) break;
        buf.resize(readBytes);
        currentProgress += readBytes;
        body<<buf;
        if(currentProgress-last_progress>1000000) {
            last_progress = currentProgress;
            PRINTUNIFIEDERROR("Progress: %" PRIu64 "\tPercentage: %.2f %%\n",currentProgress,((100.0*currentProgress)/parsedContentLength));
            local_fd.writeAllOrExit(&currentProgress,sizeof(uint64_t)); // send progress
        }
    }
    body.flush();
    body.close();
    PRINTUNIFIEDERROR("End of download: %" PRIu64 " bytes downloaded\n",currentProgress);
    local_fd.writeAllOrExit(&maxuint,sizeof(uint64_t)); // send end-of-progress

    return httpRet;
}

//void tlsClientUrlDownloadEventLoop(RingBuffer& inRb, Botan::TLS::Client& client, IDescriptor& cl) {
void tlsClientUrlDownloadEventLoop(TLS_Client& client_wrapper) {
    TLSDescriptor rcl(client_wrapper.inRb,*(client_wrapper.client));
    try {
        if(client_wrapper.sniHost.empty() || client_wrapper.downloadPath.empty()) {
            PRINTUNIFIEDERROR("sniHost field or download path is empty");
            threadExit();
        }
        PRINTUNIFIED("In TLS URL Download event loop, getString is %s ...\n",client_wrapper.getString.c_str());
        auto&& getString1 = (client_wrapper.getString.empty() || client_wrapper.getString[0] != '/')?
                            "/"+client_wrapper.getString:
                            client_wrapper.getString;
        std::string request = "GET "+getString1+" HTTP/1.0\r\n"
                                                "Host: "+client_wrapper.sniHost+"\r\n"
                                                                                "User-Agent: XFilesHTTPClient/1.0.0\r\n"
                                                                                "Accept: */*\r\n"
                                                                                "Connection: close\r\n\r\n";
        rcl.writeAllOrExit(request.c_str(),request.length());
        // read both header and body into file
        std::string hdrs;
        client_wrapper.httpRet = parseHttpResponseHeadersAndBody(rcl,
                                                                 client_wrapper.local_sock_fd,
                                                                 client_wrapper.downloadPath,
                                                                 "", // TODO receive targetFilename
                                                                 hdrs,
                                                                 client_wrapper.sniHost+"/"+client_wrapper.getString // ~ url
                                                                 );
        if (client_wrapper.httpRet == 301 || client_wrapper.httpRet == 302) {
            // get redirect domain
            const std::string locLabel = "Location: ";
            auto redirectLocIdx = hdrs.find(locLabel);
            if(redirectLocIdx == std::string::npos) throw std::runtime_error("Malformed redirect response");
            auto locationtag = hdrs.substr(redirectLocIdx+locLabel.size());
            redirectLocIdx = locationtag.find("\r\n");
            if(redirectLocIdx == std::string::npos) throw std::runtime_error("Malformed redirect response after location tag");
            locationtag = locationtag.substr(0,redirectLocIdx);
            PRINTUNIFIED("Redirect location is %s\n",locationtag.c_str());
            client_wrapper.locationToRedirect = locationtag;
        }
    }
    catch (threadExitThrowable& i) {
        PRINTUNIFIEDERROR("T2 ...\n");
    }
    catch (std::exception& e) {
        PRINTUNIFIEDERROR("exception: %s\n",e.what());
    }
    PRINTUNIFIEDERROR("[tlsClientUrlDownloadEventLoop] No housekeeping and return\n");
}

int httpsUrlDownload_internal(IDescriptor& cl, std::string& target, uint16_t port, std::string& downloadPath, RingBuffer& inRb, std::string& redirectUrl) {
    if(target.find("http://")==0) {
        throw std::runtime_error("Plain HTTP not allowed");
    }
    else if(target.find("https://")==0) {
        target = target.substr(8);
    }

    auto slashIdx = target.find('/');
    auto domainOnly = slashIdx==std::string::npos?target:target.substr(0,slashIdx);
    std::string getString = slashIdx==std::string::npos?"":target.substr(slashIdx);

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
        return -1;
    }

    PRINTUNIFIED("Looping through getaddrinfo results...\n");
    // loop through all the results and connect to the first we can
    for(p = servinfo; p != nullptr; p = p->ai_next) {
        PRINTUNIFIED("getaddrinfo item\n");

        // NEW, with timeout
        rv = connect_with_timeout(remoteCl, p);
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
        sendErrorResponse(cl);
        return -1;
    }
    PRINTUNIFIED("freeaddrinfo...\n");
    freeaddrinfo(servinfo);
    PRINTUNIFIED("Remote client session connected to server %s, port %d\n",domainOnly.c_str(),port);
    sendOkResponse(cl); // OK, from now on java client can communicate with remote server using this local socket

    PosixDescriptor pd_remoteCl(remoteCl);
    TLS_Client tlsClient(tlsClientUrlDownloadEventLoop,inRb,cl,pd_remoteCl, true, domainOnly, getString, port, downloadPath);
    tlsClient.go();
    redirectUrl = tlsClient.locationToRedirect;

    pd_remoteCl.close();
    return tlsClient.httpRet;
}

#endif /* __BASIC_HTTPS_CLIENT__ */