#ifndef __HTTPS_REQUESTS__
#define __HTTPS_REQUESTS__

#include <unordered_map>
#include <utility>
#include "basic_https_client.h"

std::pair<std::string,std::string> domainAndQueryStringFromFullUrl(std::string url) {
    if(url.find("http://")==0)
        throw std::runtime_error("Plain HTTP not allowed");
    else if(url.find("https://")==0)
        url = url.substr(8);
    auto slashIdx = url.find('/');
    auto domainOnly = slashIdx==std::string::npos?url:url.substr(0,slashIdx);
    std::string queryString = slashIdx==std::string::npos?"":url.substr(slashIdx);
    return std::make_pair(domainOnly, queryString);
}

int parseResponseCode(const std::string& responseHeaders) {
    PRINTUNIFIED("Retrieving HTTP response code...\n");
    auto tmpIdx = responseHeaders.find("\r\n");
    if (tmpIdx == std::string::npos) throw std::runtime_error("Cannot find CRLF token in headers");
    auto firstLine = responseHeaders.substr(0,tmpIdx); // "HTTP/1.1 301 Moved Permanently"
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
    return httpRet;
}

uint64_t parseContentLength(const std::string& responseHeaders) {
    PRINTUNIFIED("Finding content length... (part of body may have been already received)\n");
    uint64_t parsedContentLength = maxuint;
    std::string contentLengthPattern = "Content-Length: ";
    auto contentLengthIdx = findStringIC(responseHeaders,contentLengthPattern);
    if(contentLengthIdx == std::string::npos) {
        PRINTUNIFIEDERROR("No content-length provided, progress won't be available\n");
    }
    else {
        auto substrAfterContentLength = responseHeaders.substr(contentLengthIdx+contentLengthPattern.length());
        // find first \r
        auto crIdx = substrAfterContentLength.find('\r');
        if(crIdx == std::string::npos)
            throw std::runtime_error("Protocol error, expected \\r somewhere after content-length");
        auto substrContentLengthField = substrAfterContentLength.substr(0,crIdx);
        parsedContentLength = ::strtoll(substrContentLengthField.c_str(),nullptr,10);
        PRINTUNIFIEDERROR("PARSED CONTENT LENGTH IS: %" PRIu64 "\n",parsedContentLength);
    }
    return parsedContentLength;
}

std::string getRedirectLocation(const std::string& responseHeaders) {
    const std::string locLabel = "\nLocation: ";
    auto redirectLocIdx = findStringIC(responseHeaders,locLabel);
    if(redirectLocIdx == std::string::npos) throw std::runtime_error("Malformed redirect response");
    auto locationtag = responseHeaders.substr(redirectLocIdx+locLabel.size());
    redirectLocIdx = locationtag.find("\r\n");
    if(redirectLocIdx == std::string::npos) throw std::runtime_error("Malformed redirect response after location tag");
    locationtag = locationtag.substr(0,redirectLocIdx);
    PRINTUNIFIED("Redirect location is %s\n",locationtag.c_str());
    return locationtag;
}

std::string headerMapToStr(const std::unordered_map<std::string,std::string>& headerMap) {
    std::stringstream ss;
    for(auto& pair : headerMap)
        ss << pair.first << ": " << pair.second << "\r\n";
    return ss.str();
}

class HTTPSClient {
public:
    Basic_Credentials_Manager defaultCreds;
    RingBuffer inRb;
    std::string responseHeaders;
    std::stringstream responseBody;
    std::string currentUrl;
    std::string detectedFilename;
    IDescriptor* bodyDesc;
    std::shared_ptr<IDescriptor> tcpd; // WinsockDescriptor or PosixDescriptor
    std::shared_ptr<TLSDescriptor> tlsd;
    int httpResponseCode;

    std::unordered_map<std::string,std::string> defaultHeaders{
            {"User-Agent", "XFilesHTTPSClient/1.0.0"},
            {"Accept","*/*"}
    };

    int getResponseHeadersAndPartOfBody(IDescriptor& desc, uint64_t& currentProgress) {
        std::string tmpBody;
        std::stringstream bbuffer;
        auto headersEndIdx = std::string::npos;
        uint32_t headersCurrentSize = 0;

        // headers and, in case, part of or whole body
        for(;;) {
            std::string buffer(4096,0);
            auto readBytes=desc.read((char*)(buffer.c_str()), 4096);
            if(readBytes<=0) {
                PRINTUNIFIEDERROR("EOF or error reading headers, errno is %d\n",errno);
                return -1;
            }
            buffer.resize(readBytes);
            bbuffer<<buffer;
            headersCurrentSize += readBytes;
            if(headersCurrentSize>1048576)
                throw std::runtime_error("HTTP response header too large");
            auto&& bbuffer_s = bbuffer.str();
            headersEndIdx = bbuffer_s.find("\r\n\r\n");
            if(headersEndIdx != std::string::npos) {
                headersEndIdx += 4;
                const uint8_t* bb_ptr = (const uint8_t*)bbuffer_s.c_str();
                auto bodySize = bbuffer_s.size() - headersEndIdx;

                responseHeaders.resize(headersEndIdx);
                tmpBody.resize(bodySize);

                memcpy((uint8_t*)(responseHeaders.c_str()), bb_ptr, headersEndIdx);
                memcpy((uint8_t*)(tmpBody.c_str()), bb_ptr+headersEndIdx, bodySize);
                responseBody << tmpBody;
                currentProgress = bodySize;
                PRINTUNIFIEDERROR("headers size is %u\n",headersEndIdx);
                return 0;
            }
        }
    }

    // adapted from parseHttpResponseHeadersAndBody
    int parseHttpResponseHeadersAndBody1(IDescriptor& desc, IDescriptor& bodyDesc) {
        uint64_t currentProgress = 0, last_progress = 0;

        if(getResponseHeadersAndPartOfBody(desc, currentProgress) < 0) return -1;

        //std::cout<<responseHeaders;

        auto httpRet = parseResponseCode(responseHeaders);
        if (httpRet != 200) return httpRet;

        // flush the internal stringstream into the output descriptor...
        auto&& tmpbody_str = responseBody.str();
        bodyDesc.writeAllOrExit(tmpbody_str.c_str(), tmpbody_str.size());

        PRINTUNIFIED("Extracting a valid filename from content disposition or querystring...\n");
        detectedFilename = getHttpFilename(responseHeaders,currentUrl);

        auto parsedContentLength = parseContentLength(responseHeaders);

        auto&& progressHook = getProgressHook(parsedContentLength);

        uint8_t buf[4096]{};
        ssize_t readBytes;
        for(;;) {
            readBytes = desc.read(buf, 4096);
            if(readBytes<=0) break; // once out of the loop, check further conditions in order to deduce valid or broken download
            currentProgress += readBytes;

            bodyDesc.writeAllOrExit(buf, readBytes);

            if(currentProgress-last_progress>1000000) {
                last_progress = currentProgress;
                progressHook.publish(currentProgress);
            }
        }
        PRINTUNIFIEDERROR("\nEnd of download: %" PRIu64 " bytes downloaded\n",currentProgress);

        if(parsedContentLength != maxuint) {
            // do not check connection state, assume valid download only if all expected bytes were transferred
            if(currentProgress == parsedContentLength)
                PRINTUNIFIED("All expected bytes downloaded, download completed\n");
            else {
                PRINTUNIFIEDERROR("Expected to download %llu bytes, %llu downloaded instead, broken download\n",parsedContentLength,currentProgress);
                return -1;
            }
        }
        else {
            // since we don't have an explicit download size here, check if TLS connection was terminated abruptly or not
            if(readBytes == 0)
                PRINTUNIFIED("Connection closed, no content size provided, assume download completed\n");
            else {
                PRINTUNIFIEDERROR("Connection reset, no content size provided, assume broken download\n");
                return -1;
            }
        }

        return httpRet;
    }

    // TODO enable/disable progressHook for downloading response body
    int request(const std::string& url_,
                const std::string& method,
                // const std::unordered_map& headers,
                // const std::string& requestBody,
                IDescriptor& bodyDesc, // any kind, even stringstreamDescriptor
                int port = 443,
                int maxRedirects = 5) {
        currentUrl = url_;
        for(int i=0;i<=maxRedirects;i++) {
            inRb.reset();
            responseHeaders = "";
            std::stringstream tmprb;
            responseBody.swap(tmprb);

            auto&& splittedUrl = domainAndQueryStringFromFullUrl(url);
            auto domainOnly = splittedUrl.first;
            auto queryString = splittedUrl.second;

            // create plain TCP socket
            tcpd.reset(netfactory.createNew(domainOnly, port));
            if(!(*tcpd)) {
                PRINTUNIFIEDERROR("Error during TCP connection setup\n");
                return -1;
            }

            // wrap the TLS client socket
            tlsd.reset(new TLSDescriptor(*tcpd, inRb, port, defaultCreds, true, domainOnly));
            auto sharedHash = tlsd->setup();
            if(sharedHash.empty()) {
                PRINTUNIFIEDERROR("Error during TLS connection setup\n");
                return -2;
            }

            PRINTUNIFIED("TLS connection established with server %s, port %d\n",domainOnly.c_str(),port);

            // send the HTTPS request
            try {
                PRINTUNIFIED("Performing HTTPS request, querystring is %s ...\n",queryString.c_str());
                auto&& qs1 = (queryString.empty() || queryString[0] != '/') ? "/"+queryString : queryString;
                std::stringstream request;
                request << method << " " << qs1 << " HTTP/1.0\r\nHost: " << domainOnly << "\r\n" << headerMapToStr(defaultHeaders) << "\r\n";
                // TODO add request headers (create temporary map joining key-values, then headerMapToStr)
                auto rq = request.str();
                tlsd->writeAllOrExit(rq.c_str(),rq.length());
                // tlsd->writeAllOrExit(rqBody.c_str(),rqBody.length()); // TODO add request body

                httpResponseCode = parseHttpResponseHeadersAndBody1(*tlsd, bodyDesc);
                if(httpResponseCode == 301 || httpResponseCode == 302) {
                    currentUrl = getRedirectLocation(responseHeaders);
                    tlsd.reset(); // force disconnect, so we can reset the ringbuffer
                }
                else if(httpResponseCode != 200) {
                    PRINTUNIFIED("[https-client] error response http code: %d\n", httpResponseCode);
                    return -3;
                }
                else {
                    PRINTUNIFIED("[https-client] returned http 200\n");
                    return 0;
                }
            }
            catch (threadExitThrowable& i) {
                PRINTUNIFIEDERROR("T2 ...\n");
                return -4;
            }
            catch (std::exception& e) {
                PRINTUNIFIEDERROR("exception: %s\n",e.what());
                return -5;
            }
        }
        PRINTUNIFIED("Redirect limit reached without a http 200 response\n");
        return -6;
    }

    // for downloading to file
    template<typename STR>
    int request(const std::string& url_,
                const std::string& method,
                // const std::unordered_map& headers,
                // const std::string& requestBody,
                const STR& downloadDirectoryOrFullPath, // if directory or empty string, detect filename (and concat to dir if present), else use full path
                int port = 443,
                int maxRedirects = 5) {
        // default conf: randomly generated file name, in the current directory
        STR parentDir = FROMUTF(".");
        STR filename_ = FROMUTF(genRandomHexString()".bin");
        STR fullPath_ = parentDir + getSystemPathSeparator() + filename;
        bool detectFilename = true;

        int efd = 0;
        if(!downloadDirectoryOrFullPath.empty()) {
            efd = existsIsFileIsDir_(downloadDirectoryOrFullPath);
            if(efd == 2) {
                parentDir = downloadDirectoryOrFullPath;
                fullPath_ = parentDir + getSystemPathSeparator() + filename;
            }
            else { // not a directory (hopefully a regular, accessible file)
                fullPath_ = downloadDirectoryOrFullPath;
                detectFilename = false;
            }
        }
        // else (downloadDirectoryOrFullPath.empty() is true) -> leave default conf unchanged

        auto&& destDesc = fdfactory.create(fullPath_,FileOpenMode::XCL);
        if(!destDesc) {
            PRINTUNIFIEDERROR("Unable to open destination file for downloading, errno is: %d\n", errno);
            return -1;
        }
        int ret = client.request(url_, method, destDesc);
        destDesc.close();
        if(ret == 0 && detectFilename) // TODO use detectedFilename, smart concat paths, rename


    }

    // http response code, response body
    std::pair<int, std::string> request(const std::string& url_,
                                        const std::string& method,
                                        // const std::unordered_map& headers,
                                        // const std::string& requestBody,
                                        int port = 443,
                                        int maxRedirects = 5) {
        SstreamDescriptor ss;
        int ret = client.request(url_, method, /* headers, requestBody, */ ss, port, maxRedirects);
        return std::make_pair(ret, ss.str());
    }

};

#endif /* __HTTPS_REQUESTS__ */