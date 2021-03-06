#ifndef __BASIC_HTTPS_CLIENT__
#define __BASIC_HTTPS_CLIENT__

#include <regex>
#include <algorithm>
#include <string>
#include <cctype>
#include "../iowrappers_common.h"
#include "../desc/NetworkDescriptorFactory.h"
#include "../desc/FileDescriptorFactory.h"
#include "botan_rh_rclient.h"

// Web source: https://stackoverflow.com/questions/3152241/case-insensitive-stdstring-find
size_t findStringIC(const std::string& strHaystack, const std::string& strNeedle) {
    auto it = std::search(
            strHaystack.begin(), strHaystack.end(),
            strNeedle.begin(),   strNeedle.end(),
            [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); }
    );
    if(it == strHaystack.end()) return std::string::npos;
    return it - strHaystack.begin();
}


// Web source for url encode/decode: https://stackoverflow.com/questions/154536/encode-decode-urls-in-c

// Web sources for parsing content disposition line:
// https://stackoverflow.com/questions/23054475/javascript-regex-for-extracting-filename-from-content-disposition-header
// https://regex101.com/r/UhCzyI/3
std::string parseContentDispLine(const std::string& line) {
    std::regex theRegex("Content-Disposition: attachment; filename\\*?=['\"]?(?:UTF-\\d['\"]*)?([^;\\r\\n\"']*)['\"]?;?");
    std::smatch results;
    std::regex_search(line, results, theRegex);
    if(results.size() >= 2) {
        PRINTUNIFIED("First capture group found: %s",results[1].str().c_str());
        return results[1].str(); // result 0 is full match, return 1st capture group instead
    }
    return "";
}

std::string urlEncode(const std::string& str){
    std::string new_str;
    char c;
    int ic;
    const char* chars = str.c_str();
    char bufHex[10];
    int len = strlen(chars);

    for(int i=0;i<len;i++){
        c = chars[i];
        ic = c;
        // uncomment this if you want to encode spaces with +
        /*if (c==' ') new_str += '+';
        else */if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') new_str += c;
        else {
            sprintf(bufHex,"%X",c);
            if(ic < 16)
                new_str += "%0";
            else
                new_str += "%";
            new_str += bufHex;
        }
    }
    return new_str;
}

std::string urlDecode(const std::string& str){
    std::string ret;
    char ch;
    int i, ii, len = str.length();

    for (i=0; i < len; i++){
        if(str[i] != '%'){
            if(str[i] == '+')
                ret += ' ';
            else
                ret += str[i];
        }else{
            sscanf(str.substr(i + 1, 2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            ret += ch;
            i = i + 2;
        }
    }
    return ret;
}

std::string getHttpFilename(const std::string& hdrs, const std::string& url) {
    std::istringstream ss(hdrs);
    std::string line;
    const std::string cdPrefix = "Content-Disposition: ";
    while(std::getline(ss,line)) {
        if(findStringIC(line,cdPrefix)==0) {
            PRINTUNIFIED("Content-Disposition header found\n");
            // try parsing using regex
            auto&& z = parseContentDispLine(line);
            if(z.empty()) {
                PRINTUNIFIED("Regex failed, using manual split\n");
                auto rawIdx = line.find('=');
                if (rawIdx != std::string::npos) {
                    auto&& y = line.substr(rawIdx+1);
                    // sanitize for POSIX path compatibility
                    std::replace(y.begin(),y.end(),'/','_');
                    return y;
                }
            }
            else {
                // sanitize for POSIX path compatibility
                std::replace(z.begin(),z.end(),'/','_');
                return z;
            }
        }
    }

    PRINTUNIFIED("Content-Disposition header is malformed or not present, using URL splitting...");
    auto idx = url.find_last_of('/');
    if(idx != std::string::npos) {
        auto&& x = url.substr(idx+1);
        if(!x.empty()) {
            auto&& y = urlDecode(x);

            // drop unwanted query-type parameters after provided filename
            idx = y.find_first_of('?');
            if(idx != std::string::npos)
                y = y.substr(0,idx);

            // sanitize for POSIX path compatibility
            std::replace(y.begin(),y.end(),'/','_');

            return y;
        }
    }

    PRINTUNIFIED("URL splitting didn't return a valid filename, assigning filename as domain name + .html");
    idx = url.find_first_of('/');
    if(idx != std::string::npos){
        auto x = url.substr(0,idx);
        if(!x.empty()) return x+".html";
    }

    return "file.bin";
}

// return value: HTTP response code or -1 for unsolvable error
// if downloadToFile is false, downloadPath is ignored, and downloaded body is sent back to local_fd as string with length
template<typename STR>
int parseHttpResponseHeadersAndBody(IDescriptor& rcl,
                                    IDescriptor& local_fd,
                                    const STR& downloadPath,
                                    const STR& targetFilename,
                                    std::string& hdrs,
                                    const std::string& url,
                                    const bool downloadToFile = true) {
    uint64_t currentProgress = 0, last_progress = 0;
    std::stringstream headers;
    std::stringstream tmpbody;

    // headers

    for(;;) {
        std::string buffer(4096,0);
        auto readBytes=rcl.read((char*)(buffer.c_str()), 4096);
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
            if(rcl.read(&c, 1) < 1 || c != '\n')
                throw std::runtime_error("header parsing error on 1-byte look-ahead");
            headers<<buffer;
            headers<<c;
            goto body_tag;
        }
        else if (::memcmp(buffer.c_str()+readBytes-2,"\r\n",2)==0) {
            char cc[2]{};
            if(rcl.read(cc, 2) < 2 || ::memcmp(cc,"\r\n",2) !=0 )
                throw std::runtime_error("header parsing error on 2-byte look-ahead");
            headers<<buffer;
            headers<<cc;
            goto body_tag;
        }
        else if (buffer[readBytes-1] == '\r') {
            int rb;
            char ccc[3]{};
            if((rb = rcl.read(ccc, 3)) < 3) {
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
                    tmpbody<<bodystart;
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
    auto httpFilename = targetFilename.empty()?FROMUTF(getHttpFilename(hdrs,url)):targetFilename;
    auto tmp_ = TOUTF(httpFilename);
    PRINTUNIFIED("Assigned download filename is %s\n",tmp_.c_str());
    auto destFullPath = downloadPath.empty() ? httpFilename : downloadPath + getSystemPathSeparator() + httpFilename;
    tmp_ = TOUTF(destFullPath);
    PRINTUNIFIED("Assigned download path is %s\n",tmp_.c_str());

    // will try to open (with no success) an empty-name file in case of download to memory
    auto definitiveDestPath = downloadToFile?destFullPath:STRNAMESPACE();
    auto&& body = fdfactory.create(definitiveDestPath, FileOpenMode::WRITE);
    if(!body && downloadToFile) {
        PRINTUNIFIEDERROR("Unable to open destination file for writing, error is %d\n", body.error);
        rcl.close();
        return -1;
    }

    auto&& tmpbody_str = tmpbody.str();
    if(downloadToFile) body.writeAllOrExit(tmpbody_str.c_str(), tmpbody_str.size());
    // in case of in-memory download, tmpbody_str will be written later

    PRINTUNIFIED("Finding content length... (part of body may have been already received)\n");

    std::string contentLengthPattern = "Content-Length: ";
    auto contentLengthIdx = findStringIC(hdrs,contentLengthPattern);
    uint64_t parsedContentLength = maxuint;
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
        PRINTUNIFIEDERROR("PARSED CONTENT LENGTH IS: %" PRIu64 "\n",parsedContentLength);
    }

    tmp_ = TOUTF(httpFilename);
    writeStringWithLen(local_fd,tmp_); // send guessed filename (or send back received one) in order for the GUI to locate it once completed
    local_fd.writeAllOrExit(&parsedContentLength,sizeof(uint64_t)); // send total size

    if(!downloadToFile)
        local_fd.writeAllOrExit(tmpbody_str.c_str(),tmpbody_str.length());

    uint8_t buf[4096]{};
    ssize_t readBytes;
    for(;;) {
        readBytes = rcl.read(buf, 4096);
        if(readBytes<=0) break; // once out of the loop, check further conditions in order to deduce valid or broken download
        currentProgress += readBytes;

        if(downloadToFile) body.writeAllOrExit(buf, readBytes);
        else {
            local_fd.writeAllOrExit(buf,readBytes); // send actual content only if in-memory download has been requested
            PRINTUNIFIEDERROR("Sent downloaded chunk of %d bytes\n",readBytes);
        }

        if(currentProgress-last_progress>1000000) {
            last_progress = currentProgress;
            SAMELINEPRINT("Progress: %" PRIu64 "\tPercentage: %.2f %%",currentProgress,((100.0*currentProgress)/parsedContentLength));
            if(downloadToFile)
                local_fd.writeAllOrExit(&currentProgress,sizeof(uint64_t)); // send progress only when writing to file
        }
    }
    if(downloadToFile) {
        body.close();
    }
    PRINTUNIFIEDERROR("\nEnd of download: %" PRIu64 " bytes downloaded\n",currentProgress);

    if(parsedContentLength != maxuint) {
        // do not check connection state, assume valid download only if all expected bytes were transferred
        if(currentProgress == parsedContentLength)
            PRINTUNIFIED("All expected bytes downloaded, download completed\n");
        else {
            PRINTUNIFIEDERROR("Expected to download %llu bytes, %llu downloaded instead, broken download\n",parsedContentLength,currentProgress);
            goto brokenDownload;
        }
    }
    else {
        // since we don't have an explicit download size here, check if TLS connection was terminated abruptly or not
        if(readBytes == 0)
            PRINTUNIFIED("Connection closed, no content size provided, assume download completed\n");
        else {
            PRINTUNIFIEDERROR("Connection reset, no content size provided, assume broken download\n");
            goto brokenDownload;
        }
    }

    if(downloadToFile)
        local_fd.writeAllOrExit(&maxuint,sizeof(uint64_t)); // send end-of-progress

    return httpRet;

brokenDownload:
    rcl.close();
    local_fd.close();
    return -1;
}

void tlsClientUrlDownloadEventLoop(TLS_Client& client_wrapper) {
    TLSDescriptor rcl(client_wrapper.inRb,*(client_wrapper.client));
    try {
        if(client_wrapper.sniHost.empty() || client_wrapper.downloadPath.empty()) {
            if(client_wrapper.downloadToFile) {
                PRINTUNIFIEDERROR("sniHost field or download path is empty");
                threadExit();
            }
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
                                                                 client_wrapper.targetFilename,
                                                                 hdrs,
                                                                 client_wrapper.sniHost+"/"+client_wrapper.getString, // ~ url
                                                                 client_wrapper.downloadToFile);
        if (client_wrapper.httpRet == 301 || client_wrapper.httpRet == 302) {
            // get redirect domain
            const std::string locLabel = "\nLocation: ";
            auto redirectLocIdx = findStringIC(hdrs,locLabel);
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

template<typename STR>
int httpsUrlDownload_internal(IDescriptor& cl,
                              std::string& targetUrl,
                              uint16_t port,
                              const STR& downloadPath,
                              const STR& targetFilename,
                              RingBuffer& inRb,
                              std::string& redirectUrl,
                              const bool downloadToFile) {
    if(targetUrl.find("http://")==0)
        throw std::runtime_error("Plain HTTP not allowed");
    else if(targetUrl.find("https://")==0)
        targetUrl = targetUrl.substr(8);

    auto slashIdx = targetUrl.find('/');
    auto domainOnly = slashIdx==std::string::npos?targetUrl:targetUrl.substr(0,slashIdx);
    std::string getString = slashIdx==std::string::npos?"":targetUrl.substr(slashIdx);

    auto&& remoteCl = netfactory.create(domainOnly, port);
    if(!remoteCl) {
        sendErrorResponse(cl);
        return -1;
    }

    PRINTUNIFIED("Remote client session connected to server %s, port %d\n",domainOnly.c_str(),port);
    sendOkResponse(cl); // OK, from now on java client can communicate with remote server using this local socket

    TLS_Client tlsClient(tlsClientUrlDownloadEventLoop,inRb,cl,remoteCl, true, domainOnly, getString, port, downloadPath, targetFilename, downloadToFile);
    tlsClient.go();
    redirectUrl = tlsClient.locationToRedirect;

    remoteCl.close();
    return tlsClient.httpRet;
}

// private key in OpenSSL PKCS8 format, will only work with OpenSSH (incompatible with other SSH clients/libraries)
std::pair<std::string,std::string> ssh_keygen_internal(uint32_t keySize) {
    PRINTUNIFIED("Generating key pair...");
    Botan::AutoSeeded_RNG rng;
    Botan::RSA_PrivateKey prv(rng,keySize);
    PRINTUNIFIED("Generation complete, encoding to PEM...");
    // RSA with traditional format, should work with all ssh clients/libraries
//    auto&& privateBits = prv.private_key_bits();
//    std::string prv_s = Botan::PEM_Code::encode(privateBits, "RSA PRIVATE KEY");
    std::string prv_s = Botan::PKCS8::PEM_encode(prv);
    std::string pub_s = Botan::X509::PEM_encode(prv);
    PRINTUNIFIED("Encoding complete");
    return std::make_pair(prv_s,pub_s);
}

#endif /* __BASIC_HTTPS_CLIENT__ */
