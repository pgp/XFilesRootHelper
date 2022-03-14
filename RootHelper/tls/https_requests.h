#ifndef __HTTPS_REQUESTS__
#define __HTTPS_REQUESTS__

#include <unordered_map>
#include <utility>
#include "basic_https_client.h"

typedef struct {
    bool isHttps;
    std::string domainOnly;
    int port;
    std::string queryString;
} httpUrlInfo;

// test non-standard TLS port 8443 here:
// https://clienttest.ssllabs.com:8443/ssltest/viewMyClient.html
httpUrlInfo getHttpInfo(std::string url, bool allowPlainHttp = false) {
    httpUrlInfo info;
    info.isHttps = true; // assume https by default (i.e. when scheme is not provided at all)
    if(url.find("http://")==0) {
        if(!allowPlainHttp) throw std::runtime_error("Plain HTTP not allowed");
        url = url.substr(7);
        info.isHttps = false;
    }
    else if(url.find("https://")==0)
        url = url.substr(8);
    auto idx = url.find('/');
    info.domainOnly = idx==std::string::npos?url:url.substr(0,idx);
    info.queryString = idx==std::string::npos?"":url.substr(idx);

    // extract port, if present, otherwise use 80 for http and 443 for https
    info.port = info.isHttps ? 443 : 80;
    idx = info.domainOnly.find(':');
    if(idx != std::string::npos) {
        std::string p = info.domainOnly.substr(idx+1);
        info.port = stoi(p);
        info.domainOnly = info.domainOnly.substr(0,idx);
    }
    return info;
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
    std::shared_ptr<IDescriptor> tcpd; // WinsockDescriptor or PosixDescriptor
    std::shared_ptr<IDescriptor> tlsd; // WinsockDescriptor or PosixDescriptor for HTTP, TLSDescriptor for HTTPS connections // TODO rename
    int httpResponseCode;

    std::unordered_map<std::string,std::string> defaultHeaders{
            {"User-Agent", "XFilesHTTPSClient/1.0.0"},
            {"Accept","*/*"}
    };

    int getResponseHeadersAndPartOfBody(IDescriptor& desc, uint64_t& currentProgress) {
        std::string tmpBody;
        std::stringstream bb;
        auto headersEndIdx = std::string::npos;
        uint32_t headersCurrentSize = 0;

        // headers and, in case, part of or whole body
        char buffer[4096];
        for(;;) {
            auto readBytes=desc.read(buffer, 4096);
            if(readBytes<=0) {
                PRINTUNIFIEDERROR("EOF or error reading headers, errno is %d\n",errno);
                return -1;
            }
            bb.write(buffer,readBytes);
            headersCurrentSize += readBytes;
            if(headersCurrentSize>1048576)
                throw std::runtime_error("HTTP response header too large");
            auto&& bbuffer_s = bb.str();
            headersEndIdx = bbuffer_s.find("\r\n\r\n");
            if(headersEndIdx != std::string::npos) {
                headersEndIdx += 4;
                const uint8_t* bbP = (const uint8_t*)bbuffer_s.c_str();
                auto bodySize = bbuffer_s.size() - headersEndIdx;

                responseHeaders.resize(headersEndIdx);
                tmpBody.resize(bodySize);

                memcpy((uint8_t*)(responseHeaders.c_str()), bbP, headersEndIdx);
                memcpy((uint8_t*)(tmpBody.c_str()), bbP+headersEndIdx, bodySize);
                responseBody << tmpBody;
                currentProgress = bodySize;
                PRINTUNIFIEDERROR("headers size is %u\n",headersEndIdx);
                return 0;
            }
        }
    }

    // adapted from parseHttpResponseHeadersAndBody
    /*
    when downloadToFile is true, targetPath:
        - empty string -> download to current dir, detect filename
        - non-empty string, is a dir -> download to that directory, detect filename
        - non-empty string, else -> treat as file path, download to that path (do not use detected filename)
    */
    template<typename STR>
    int parseResponseHeadersAndDownloadBody(IDescriptor& desc, bool downloadToFile, const STR& targetPath) {
        uint64_t currentProgress = 0, last_progress = 0;

        if(getResponseHeadersAndPartOfBody(desc, currentProgress) < 0) return -1;

        auto httpRet = parseResponseCode(responseHeaders);
        if (httpRet == 301 || httpRet == 302) return httpRet;

        PRINTUNIFIED("Extracting a valid filename from content disposition or querystring...\n");
        detectedFilename = getHttpFilename(responseHeaders,currentUrl);
        PRINTUNIFIED("Detected filename is: %s\n",detectedFilename.c_str());
        STR df1 = FROMUTF(detectedFilename);
        std::unique_ptr<IDescriptor> bodyDesc;

        // init bodyDesc from conditions upon targetPath
        if(downloadToFile) {
            STR downloadPath;
            if(targetPath.empty()) downloadPath = FROMUTF(".") + getSystemPathSeparator() + df1; // current directory, use detected filename
            else {
                int efd = existsIsFileIsDir_(targetPath);
                if(efd == 2) downloadPath = targetPath + getSystemPathSeparator() + df1; // existing directory
                else downloadPath = targetPath;
            }
            std::string dp1 = TOUTF(downloadPath);
            PRINTUNIFIED("Assigned download path is: %s\n",dp1.c_str());
            bodyDesc.reset(fdfactory.createNew(downloadPath,FileOpenMode::XCL));

            // flush the internal stringstream into the output descriptor
            auto&& tmpbody_str = responseBody.str();
            bodyDesc->writeAllOrExit(tmpbody_str.c_str(), tmpbody_str.size());
        }
        // else don't do anything, continue writing into responseBody later

        auto parsedContentLength = parseContentLength(responseHeaders);

        auto&& progressHook = getProgressHook(parsedContentLength);

        uint8_t buf[4096]{};
        ssize_t readBytes;
        for(;;) {
            readBytes = desc.read(buf, 4096);
            if(readBytes<=0) break; // once out of the loop, check further conditions in order to deduce valid or broken download
            currentProgress += readBytes;

            if(downloadToFile) bodyDesc->writeAllOrExit(buf, readBytes);
            else responseBody.write((char*)buf, readBytes);

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
    template<typename STR>
    int request(const std::string& url,
                const std::string& method,
                const std::unordered_map<std::string, std::string>& headers,
                std::string requestBody,
                bool downloadToFile,
                const STR& targetPath, // (use if downloadToFile is true) empty: detect filename, non-empty: download to path (folder or file)
                bool verifyCertificates = true,
                bool httpsOnly = true,
                uint32_t maxRedirects = 5) {
        currentUrl = url;
        for(int i=0;i<=maxRedirects;i++) {
            inRb.reset();
            responseHeaders = "";
            std::stringstream tmprb;
            responseBody.swap(tmprb);

            auto&& info = getHttpInfo(currentUrl, !httpsOnly);
            auto& domainOnly = info.domainOnly;
            auto& queryString = info.queryString;
            auto& port = info.port;

            // create plain TCP socket
            tcpd.reset(netfactory.createNew(domainOnly, port));
            if(!(*tcpd)) {
                PRINTUNIFIEDERROR("Error during TCP connection setup\n");
                return -1;
            }

            // wrap the TLS client socket
            if(info.isHttps) {
                tlsd.reset(new TLSDescriptor(*tcpd, inRb, port, defaultCreds, verifyCertificates, domainOnly));
                TLSDescriptor& tlsd1 = (TLSDescriptor&)(*tlsd);
                auto sharedHash = tlsd1.setup();
                if(sharedHash.empty()) {
                    PRINTUNIFIEDERROR("Error during TLS connection setup\n");
                    return -2;
                }
            }
            else tlsd = tcpd; // short-circuit, use the TCP descriptor
            const char* transportScheme = info.isHttps ? "TLS" : "TCP";

            PRINTUNIFIED("%s connection established with server %s, port %d\n", transportScheme, domainOnly.c_str(),port);

            // send the HTTPS request
            try {
                PRINTUNIFIED("Performing HTTP(S) request, querystring is %s ...\n",queryString.c_str());
                auto&& qs1 = (queryString.empty() || queryString[0] != '/') ? "/"+queryString : queryString;
                std::stringstream request;
                auto totalHeaders = headers;
                totalHeaders.insert(defaultHeaders.begin(),defaultHeaders.end()); // keys already existing in headers are not overwritten by those in defaultHeaders
                request << method << " " << qs1 << " HTTP/1.0\r\nHost: " << domainOnly << "\r\n" << headerMapToStr(totalHeaders);
                if(!requestBody.empty()) {
                    request << "Content-Length: " << requestBody.length() << "\r\n";
                }
                request << "\r\n";
                auto rq = request.str();
                tlsd->writeAllOrExit(rq.c_str(),rq.length());
                if(!requestBody.empty()) {
                    requestBody = requestBody + "\r\n\r\n";
                    tlsd->writeAllOrExit(requestBody.c_str(),requestBody.length());
                }

                httpResponseCode = parseResponseHeadersAndDownloadBody(*tlsd, downloadToFile, targetPath);
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
        PRINTUNIFIED("Redirect limit reached\n");
        return -6;
    }

};

#endif /* __HTTPS_REQUESTS__ */