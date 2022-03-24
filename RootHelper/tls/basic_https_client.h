#ifndef __BASIC_HTTPS_CLIENT__
#define __BASIC_HTTPS_CLIENT__

#include <regex>
#include <algorithm>
#include <string>
#include <cctype>
#include "../Utils.h"
#include "../iowrappers_common.h"
#include "../desc/NetworkDescriptorFactory.h"
#include "../desc/FileDescriptorFactory.h"
#include "botan_rh_tls_descriptor.h"
#include "../progressHook.h"

typedef struct {
    bool isHttps;
    std::string domainOnly;
    int port;
    std::string queryString;
} httpUrlInfo;

// test non-standard TLS port 8443 here:
// https://clienttest.ssllabs.com:8443/ssltest/viewMyClient.html
int getHttpInfo(httpUrlInfo& info, std::string url, bool allowPlainHttp) {
    info.isHttps = true; // assume https by default (i.e. when scheme is not provided at all)
    if(url.find("http://")==0) {
        if(!allowPlainHttp) {
            PRINTUNIFIEDERROR("Plain HTTP not allowed\n");
            return -1;
        }
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
    return 0;
}

void rtrim(std::string& s) {
    int L = s.length();
    if(L==0) return;
    int i = L-1;
    for(; i >= 0; i--) {
        if(s[i] != ' ' && s[i] != '\r' && s[i] != '\n' && s[i] != '\t') break;
    }
    if(i+1 != L) s.resize(i+1);
}

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
        PRINTUNIFIED("First capture group found: %s\n",results[1].str().c_str());
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

std::string getHttpFilename_(const std::string& hdrs, const std::string& url) {
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
                if (rawIdx == std::string::npos) goto nocd;
                z = line.substr(rawIdx+1);
            }
            // sanitize for POSIX path compatibility
            std::replace(z.begin(),z.end(),'/','_');
            rtrim(z);
            return z;
        }
    }

    nocd:
    PRINTUNIFIED("Content-Disposition header is malformed or not present, using URL splitting...\n");
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
            if(!y.empty()) return y;
        }
    }

    PRINTUNIFIED("URL splitting didn't return a valid filename, assigning filename as domain name + .html\n");
    idx = url.find_first_of('/');
    if(idx != std::string::npos){
        auto x = url.substr(0,idx);
        if(!x.empty()) return x+".html";
    }

    return "file.bin";
}

// perform sanitizing for Windows paths as well (but only on Windows, clearly)
// web source:
// https://stackoverflow.com/questions/1976007/what-characters-are-forbidden-in-windows-and-linux-directory-names#31976060
#ifdef _WIN32
std::string getHttpFilename(const std::string& hdrs, const std::string& url) {
    auto&& s = getHttpFilename_(hdrs, url);
    char *p = (char*)(s.c_str());
    for(int i=0;i<s.length();i++) {
        switch(p[i]) {
            case '<':
            case '>':
            case ':':
            case '"':
            case '/':
            case '\\':
            case '|':
            case '?':
            case '*':
                p[i] = '_';
        }
    }
    return s;
}
#else
auto& getHttpFilename = getHttpFilename_;
#endif

// template<typename STR>
// void dumpToFile(const std::string& content, const STR& path) {
    // auto&& out = fdfactory.create(path, FileOpenMode::XCL);
    // out.writeAllOrExit(content.c_str(),content.size());
// }

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
    std::string tmpbody;
    
    std::stringstream bbuffer;
    auto headersEndIdx = std::string::npos;
    uint32_t headersCurrentSize = 0;

    // headers and, in case, part of or whole body
    for(;;) {
        std::string buffer(4096,0);
        auto readBytes=rcl.read((char*)(buffer.c_str()), 4096);
        if(readBytes<=0) {
            perror("EOF or error reading headers");
            return -1;
        }
        buffer.resize(readBytes);
        bbuffer<<buffer;
        headersCurrentSize += readBytes;
        if(headersCurrentSize>1048576)
            throw std::runtime_error("HTTP header too large");
        auto&& bbuffer_s = bbuffer.str();
        headersEndIdx = bbuffer_s.find("\r\n\r\n");
        if(headersEndIdx != std::string::npos) {
            headersEndIdx += 4;
            const uint8_t* bb_ptr = (const uint8_t*)bbuffer_s.c_str();
            auto bodySize = bbuffer_s.size() - headersEndIdx;
            
            hdrs.resize(headersEndIdx);
            tmpbody.resize(bodySize);
            
            memcpy((uint8_t*)(hdrs.c_str()), bb_ptr, headersEndIdx);
            memcpy((uint8_t*)(tmpbody.c_str()), bb_ptr+headersEndIdx, bodySize);
            currentProgress = bodySize;
            PRINTUNIFIEDERROR("headers size is %zu\n",headersEndIdx);
            break;
        }
    }

    // body
    body_tag:

    std::cout<<hdrs;

    PRINTUNIFIED("Retrieving HTTP response code...\n");
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
    STR httpFilename = targetFilename.empty()?FROMUTF(getHttpFilename(hdrs,url)):targetFilename;
    std::string tmp_ = TOUTF(httpFilename);
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

    auto& tmpbody_str = tmpbody;
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

    auto&& progressHook = getProgressHook(parsedContentLength, REMOTE_IO_CHUNK_SIZE);

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
            PRINTUNIFIEDERROR("Sent downloaded chunk of %zd bytes\n",readBytes);
        }

        if(currentProgress-last_progress>1000000) {
            last_progress = currentProgress;
            progressHook.publish(currentProgress);
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
            PRINTUNIFIEDERROR("Expected to download %" PRIu64 " bytes, %" PRIu64 " downloaded instead, broken download\n",parsedContentLength,currentProgress);
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

template<typename STR>
int httpsUrlDownload_internal(IDescriptor& cl,
                              std::string& targetUrl,
                              const STR& downloadPath,
                              const STR& targetFilename,
                              RingBuffer& inRb,
                              std::string& redirectUrl,
                              const bool downloadToFile,
                              const bool httpsOnly) {
    httpUrlInfo info{};
    int ret = getHttpInfo(info, targetUrl, !httpsOnly);
    if(ret) return ret;
    auto& domainOnly = info.domainOnly;
    auto& getString = info.queryString;
    auto& port = info.port;

    std::shared_ptr<IDescriptor> remoteCl(netfactory.createNew(domainOnly, port));
    if(!(*remoteCl)) {
        sendErrorResponse(cl);
        return -1;
    }

    if(domainOnly.empty() || downloadPath.empty()) {
        if(downloadToFile) {
            PRINTUNIFIEDERROR("sniHost field or download path is empty");
            sendErrorResponse(cl);
            return -1;
        }
    }

    Basic_Credentials_Manager defaultCreds; // don't use the custom credsManager, used for xre
    std::shared_ptr<IDescriptor> wrappedD;
    if(info.isHttps) {
        TLSDescriptor* tlsd = new TLSDescriptor(*remoteCl, inRb, port, defaultCreds, true, domainOnly);
        wrappedD.reset(tlsd);
        auto sharedHash = tlsd->setup();
        if(sharedHash.empty()) {
            PRINTUNIFIEDERROR("Error during TLS connection setup\n");
            sendErrorResponse(cl);
            return -1;
        }
        // on plain TCP connections, do not even send this part, end-of-redirects indication is enough later
        sendOkResponse(cl);
        if(cl.write(&sharedHash[0],sharedHash.size()) < sharedHash.size()) {
            PRINTUNIFIEDERROR("Unable to atomic write connect info to local socket");
            threadExit();
        }
    }
    else wrappedD = remoteCl; // short-circuit, use the TCP descriptor

    const char* transportScheme = info.isHttps ? "TLS" : "TCP";

    PRINTUNIFIED("%s connection established with server %s, port %d\n", transportScheme, domainOnly.c_str(),port);

    try {
        PRINTUNIFIED("Performing HTTPS request, getString is %s ...\n",getString.c_str());
        auto&& getString1 = (getString.empty() || getString[0] != '/') ? "/"+getString : getString;
        std::string request = "GET "+getString1+" HTTP/1.0\r\nHost: "+domainOnly+"\r\n"
                                                                    "User-Agent: XFilesHTTPSClient/1.0.0\r\n"
                                                                    "Accept: */*\r\n"
                                                                    "Connection: close\r\n\r\n";
        wrappedD->writeAllOrExit(request.c_str(),request.length());

        // read both header and body into file
        std::string hdrs;
        auto httpRet = parseHttpResponseHeadersAndBody(*wrappedD,
                                                       cl,
                                                       downloadPath,
                                                       targetFilename,
                                                       hdrs,
                                                       domainOnly+"/"+getString, // ~ url
                                                       downloadToFile);
        if (httpRet == 301 || httpRet == 302) {
            // get redirect domain
            const std::string locLabel = "\nLocation: ";
            auto redirectLocIdx = findStringIC(hdrs,locLabel);
            if(redirectLocIdx == std::string::npos) throw std::runtime_error("Malformed redirect response");
            auto locationtag = hdrs.substr(redirectLocIdx+locLabel.size());
            redirectLocIdx = locationtag.find("\r\n");
            if(redirectLocIdx == std::string::npos) throw std::runtime_error("Malformed redirect response after location tag");
            locationtag = locationtag.substr(0,redirectLocIdx);
            PRINTUNIFIED("Redirect location is %s\n",locationtag.c_str());
            redirectUrl = locationtag;
        }
        PRINTUNIFIED("[https-client] returned http 200\n");
        return httpRet;
    }
    catch (threadExitThrowable& i) {
        PRINTUNIFIEDERROR("T2 ...\n");
    }
    catch (std::exception& e) {
        PRINTUNIFIEDERROR("exception: %s\n",e.what());
    }
    PRINTUNIFIEDERROR("[https-client] generic error\n");
    remoteCl->shutdown();
    return -1;
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

const std::string CRLF = "\r\n";

std::string genRandomHexString() {
    // 16 random bytes
    std::vector<uint8_t> p1(16);
    botan_rng_t rng{};
    botan_rng_init(&rng, nullptr);

    botan_rng_get(rng,&p1[0],16);
    botan_rng_destroy(rng);

    // expand to 32 hex chars
    std::string BOUNDARY = Botan::hex_encode(p1);
    return BOUNDARY;
}

/***** multipart form data *****/
// body header
std::string bodyHeader(const std::string& filename, const std::string& BOUNDARY) {
    std::stringstream body;

    body << "--" << BOUNDARY << CRLF;
    body << "Content-Disposition: form-data; name=\"file\"; filename=\"" << filename << "\"" << CRLF;
    body << "Content-Type: application/octet-stream" << CRLF << CRLF;
    return body.str();
}

// body trailer
std::string bodyTrailer(const std::string& BOUNDARY) {
    std::stringstream body;
    body << CRLF << "--" << BOUNDARY << "--" << CRLF << CRLF;
    return body.str();
}

#ifdef _WIN32
const std::wstring dp1 = L"C:\\Windows\\Temp\\";
const std::wstring tfl = L"out_x0at.txt";
#else
const std::string dp1 = "/tmp/";
const std::string tfl = "out_x0at.txt";
#endif

template<typename STR>
int httpsUrlUpload_internal(IDescriptor& cl,
                                 const std::string& domainOnly,
                                 const STR& sourcePathForUpload,
                                 RingBuffer& inRb,
                                 bool uploadFromCli) {
    const auto port = 443;
    int httpRet = -1;
    auto&& remoteCl = netfactory.create(domainOnly, port);
    if(!remoteCl) {
        sendErrorResponse(cl);
        return -1;
    }

    Basic_Credentials_Manager defaultCreds; // don't use the custom credsManager, used for xre
    TLSDescriptor tlsd(remoteCl, inRb, port, defaultCreds, true, domainOnly);
    auto sharedHash = tlsd.setup();
    if(sharedHash.empty()) {
        PRINTUNIFIEDERROR("Error during TLS connection setup\n");
        sendErrorResponse(cl);
        return -1;
    }

    PRINTUNIFIED("TLS connection established with server %s, port %d\n",domainOnly.c_str(),port);
    sendOkResponse(cl);
    
    if(cl.write(&sharedHash[0],sharedHash.size()) < sharedHash.size()) {
		PRINTUNIFIEDERROR("Unable to atomic write connect info to local socket");
		threadExit();
	}

    try {
        PRINTUNIFIED("Posting file to %s\n", domainOnly.c_str());
        std::string fname = TOUTF(sourcePathForUpload); // downloadPath is actually the path of the file to be uploaded here
        std::string BOUNDARY = "--------" + genRandomHexString();
        PRINTUNIFIED("Using boundary string: %s\n", BOUNDARY.c_str());
        auto&& bh = bodyHeader(fname, BOUNDARY);
        auto&& bt = bodyTrailer(BOUNDARY);

        uint64_t fsize = osGetSize(sourcePathForUpload);
        cl.writeAllOrExit(&fsize,sizeof(uint64_t));
        auto wrappedBodyLen = bh.size() + fsize + bt.size();
        auto&& fileToUpload = fdfactory.create(sourcePathForUpload, FileOpenMode::READ);

        std::string postHeader = "POST / HTTP/1.1\r\n"
                              "Host: "+domainOnly+"\r\n"
                              "Content-Length: "+std::to_string(wrappedBodyLen)+"\r\n"
                              "User-Agent: XFilesHTTPClient/1.0.0\r\n"
                              "Accept: */*\r\n"
                              "Connection: close\r\n"
                              "Content-Type: multipart/form-data; boundary="+BOUNDARY+"\r\n\r\n";

        tlsd.writeAllOrExit(postHeader.c_str(),postHeader.size());
        tlsd.writeAllOrExit(bh.c_str(),bh.size());
        // FIXME duplicated code from downloadRemoteItems, refactor into a function
        constexpr auto UPLOAD_CHUNK_SIZE = REMOTE_IO_CHUNK_SIZE / 4; // 256k
        /********* quotient + remainder IO loop *********/
        uint64_t currentProgress = 0;
        uint64_t quotient = fsize / UPLOAD_CHUNK_SIZE;
        uint64_t remainder = fsize % UPLOAD_CHUNK_SIZE;
        auto&& progressHook = getProgressHook(fsize, UPLOAD_CHUNK_SIZE);

        PRINTUNIFIED("Chunk info: quotient is %" PRIu64 ", remainder is %" PRIu64 "\n",quotient,remainder);
        std::vector<uint8_t> buffer(UPLOAD_CHUNK_SIZE);
        uint8_t* v = &buffer[0];
        for(uint64_t i=0;i<quotient;i++) {
            fileToUpload.readAllOrExit(v,UPLOAD_CHUNK_SIZE);
            tlsd.writeAllOrExit(v,UPLOAD_CHUNK_SIZE);

            // send progress information back to local socket
            currentProgress += UPLOAD_CHUNK_SIZE;

            cl.writeAllOrExit(&currentProgress,sizeof(uint64_t));
            progressHook.publishDelta(UPLOAD_CHUNK_SIZE);
        }

        if(remainder != 0) {
            fileToUpload.readAllOrExit(v,remainder);
            tlsd.writeAllOrExit(v,remainder);

            // send progress information back to local socket
            currentProgress += remainder;

            cl.writeAllOrExit(&currentProgress,sizeof(uint64_t));
            progressHook.publishDelta(remainder);
        }
        /********* end quotient + remainder IO loop *********/
        tlsd.writeAllOrExit(bt.c_str(),bt.size());

        // end-of-upload indication
        cl.writeAllOrExit(&maxuint,sizeof(uint64_t));

        std::string hdrs;

        httpRet = parseHttpResponseHeadersAndBody(tlsd,
                                                  cl,
                                                  dp1,
                                                  tfl,
                                                  hdrs,
                                                  domainOnly+"/",
                                                  uploadFromCli); // uploadFromCli ia actually downloadToFile
    }
    catch (threadExitThrowable& i) {
        PRINTUNIFIEDERROR("T2 ...\n");
        return -1;
    }
    catch (std::exception& e) {
        PRINTUNIFIEDERROR("exception: %s\n",e.what());
        return -1;
    }
    
    if(uploadFromCli) {
        if(httpRet == 200) { // http ret from x0.at, after upload
            // read from downloaded link file
            auto generatedLinkPath = dp1 + tfl;
            auto&& generatedLinkFd = fdfactory.create(generatedLinkPath, FileOpenMode::READ);
            // read up to 4096 bytes, expect a download link to be brief
            if(generatedLinkFd) {
                char buffer[4096]{};
                generatedLinkFd.readAll(buffer, 4096);
                PRINTUNIFIED("Generated download link is: %s\n", buffer);
                return httpRet;
            }
            PRINTUNIFIED("Unable to open generated download link file\n");
        }
        else {
            PRINTUNIFIED("Upload failed, unable to generate a download link\n");
        }
    }

    remoteCl.shutdown();
    return httpRet;
}

#endif /* __BASIC_HTTPS_CLIENT__ */
