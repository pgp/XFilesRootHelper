#ifndef __RH_CLI_H__
#define __RH_CLI_H__

#include <unordered_map>
#include "common_uds.h"
#include "tls/https_requests.h"
#include "tls/ssh_keygen_ed25519.h"
#include "desc/SinkDescriptor.h"
#include "rh_hasher_botan.h"

#ifdef _WIN32
using cliFunction = int (*)(int argc, const wchar_t* argv[]);
#else
using cliFunction = int (*)(int argc, const char* argv[]);
#endif

template<typename C>
int downloadFromArgs(int argc, const C* argv[]) {
    // argv[1] already checked == "download"
    if(argc < 3) {
        std::string x = TOUTF(argv[0]);
        PRINTUNIFIED("Usage: %s download targeturl.com/path/to/remote/file.htm [dest-path] [output_file.bin]\n", x.c_str());
        _Exit(0);
    }
#ifdef _WIN32
    { // FIXME call common init method BEFORE invoking cli parser
        // Initialize Winsock
        WSADATA wsaData;
        auto iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
        if (iResult != 0) {
            printf("WSAStartup failed with error: %d\n", iResult);
            _Exit(572);
        }
    };
#endif
    RingBuffer inRb;
    std::string redirectUrl;
    SinkDescriptor cl;
    std::string targetUrl = TOUTF(argv[2]);
    if(targetUrl.find("https://")==0) targetUrl = targetUrl.substr(8); // strip leading https:// if present
    // use current (working) directory as destination if not provided
    auto destDir = STRNAMESPACE() + ((argc>=4)?argv[3]:FROMUTF(".")); // NOT TOUNIXPATH, user is expected to pass the correctly formatted path from cli
    // detect filename from Content-Disposition header if not passed as argument
    auto destFilename = STRNAMESPACE() + ((argc>=5)?argv[4]:STRNAMESPACE());
    int httpRet = -1;

    // HTTP redirect limit
    for(int i=0;i<5;i++) {
        httpRet = httpsUrlDownload_internal(cl,targetUrl,destDir,destFilename,inRb,redirectUrl,true,true);
        if(httpRet == 200) return 0;
        if(httpRet != 301 && httpRet != 302) {
            errno = httpRet;
            sendErrorResponse(cl);
            return httpRet;
        }
        inRb.reset();
        targetUrl = redirectUrl;
    }

    return httpRet;
}

template<typename C>
int upload_x0at_0x0st(int argc, const C* argv[]) {
    // argv[1] already checked == "upx0at"
    if(argc < 3) {
        std::string x = TOUTF(argv[0]);
        PRINTUNIFIED("Usage: %s <x0at|0x0st> /your/src/path/file.bin\n", x.c_str());
        _Exit(0);
    }
    std::string cliDomain = TOUTF(argv[1]);
#ifdef _WIN32
    { // FIXME call common init method BEFORE invoking cli parser
        // Initialize Winsock
        WSADATA wsaData;
        auto iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
        if (iResult != 0) {
            printf("WSAStartup failed with error: %d\n", iResult);
            _Exit(572);
        }
    };
#endif
    RingBuffer inRb;
    SinkDescriptor cl;
    auto&& srcPath = STRNAMESPACE(argv[2]);
    std::string domainOnly = cliDomain == "x0at" ? "x0.at" : "0x0.st";
    auto httpRet = httpsUrlUpload_internal(cl,domainOnly,srcPath,inRb,true);
    return httpRet==200?0:httpRet;
}

template<typename C>
int sshKeygenFromArgs(int argc, const C* argv[]) {
    std::string keyType;
    if(argc < 3) goto errorLabel;

    keyType = TOUTF(argv[2]);
    if(keyType == "rsa") {
        uint32_t keySize = 4096;
        if(argc >= 4) {
            std::string arg2 = TOUTF(argv[3]);
            keySize = std::atoi(arg2.c_str());
        }
        std::string destDir = (argc>=5)?TOUTF(argv[4]):".";
        std::string prvKeyName = (argc>=6)?TOUTF(argv[5]):"id_rsa";
        std::string pubKeyName = prvKeyName + ".pub";
        PRINTUNIFIED("Generating RSA keypair of size %d bits in path %s, name: %s\n", keySize, destDir.c_str(), prvKeyName.c_str());
        auto&& keyPair = ssh_keygen_internal(keySize);
        std::ofstream prv(destDir+"/"+prvKeyName, std::ios::binary);
        prv<<keyPair.first;
        prv.flush();
        prv.close();
        std::ofstream pub(destDir+"/"+pubKeyName, std::ios::binary);
        pub<<keyPair.second;
        pub.flush();
        pub.close();
        return 0;
    }
    else if(keyType == "ed25519") {
        std::string destDir = (argc>=4)?TOUTF(argv[3]):".";
        std::string prvKeyName = (argc>=5)?TOUTF(argv[4]):"id_ed25519";
        std::string filepath = destDir + "/" + prvKeyName;
        PRINTUNIFIED("Generating ed25519 keypair in path %s, name: %s\n", destDir.c_str(), prvKeyName.c_str());

        try {
			auto&& keyComment = prvKeyName + "@xfiles";
            generate_ed25519_keypair(filepath.c_str(),keyComment);
            PRINTUNIFIED("Keypair generated successfully\n");
            return 0;
        }
        catch(const std::runtime_error& e) {
            PRINTUNIFIEDERROR("Keypair generation error: %s\n",e.what());
            return -1;
        }
    }

    errorLabel:
    PRINTUNIFIEDERROR("Usage: %s %s [rsa|ed25519] ...\n",argv[0],argv[1]);
    PRINTUNIFIEDERROR("synopsis (rsa): ... [keySize (4096)] [destDir ('.')] [prvKeyName (id_rsa)]\n"
                      "synopsis (ed25519): ... [destDir ('.')] [prvKeyName (id_ed25519)]\n");
    return 0;
}

template<typename C>
int hashFromArgs(int argc, const C* argv[]) {
    // default dirHash configuration (will be customizable once argparse is added as dependency):
    // - ignore windows thumbs files: true
    // - ignore unix hidden files/folders: true
    // - ignore empty dirs (in case of hashNames): false

    std::string hl = "hashNames";
    uint8_t dirHashOpts = 0;
    std::string tmparg;
    std::unordered_map<std::string,uint8_t> tmpAlgoMap;
    uint8_t tmpIdx = 0;
    std::string exeName = TOUTF(argv[0]);

    if(argc < 4) {
        cliHashPrintUsage:
        PRINTUNIFIED("Usage: %s <hash|hashNames> algo filesOrDirectories...\n"
                     "Available algorithms: ",exeName.c_str());
        for(auto& s: cli_hashLabels) PRINTUNIFIED("%s ",s.c_str());
        PRINTUNIFIED("\n");
        _Exit(0);
    }

    tmparg = TOUTF(argv[1]);

    SETb0(dirHashOpts, ((int)(hl==tmparg))); // set withNames
    SETb1(dirHashOpts, 1); // ignore thumbs files: true
    SETb2(dirHashOpts, 1); // ignore unix hidden files: true
    SETBIT(dirHashOpts, 3, 0); // ignore empty dirs (if hashing also names): false

    PRINTUNIFIED("dirHashOpts: %u\n",dirHashOpts);

    tmparg = TOUTF(argv[2]);
    tmparg = toUpperCase(tmparg);
    for(auto& hl: cli_hashLabels) tmpAlgoMap[hl] = tmpIdx++;
	if(tmpAlgoMap.find(tmparg) == tmpAlgoMap.end())
        goto cliHashPrintUsage;

    for(int i=3; i<argc; i++) {
        auto&& s_arg_i = STRNAMESPACE(argv[i]);
        auto&& currentHash = rh_computeHash_wrapper(s_arg_i, tmpAlgoMap[tmparg], dirHashOpts);
        std::string arg_i = TOUTF(argv[i]);
        PRINTUNIFIED("%s: %s\n", Botan::hex_encode(currentHash).c_str(), arg_i.c_str());
    }
    return 0;
}

template<typename C>
int createFileFromArgs(int argc, const C* argv[]) {
    std::string exeName = TOUTF(argv[0]);
    if(argc < 3) {
        PRINTUNIFIED("Usage: %s <create|touch> filename [size in bytes]\n", exeName.c_str());
        _Exit(0);
    }

    auto&& filename = STRNAMESPACE(argv[2]);
    uint64_t fileSize = 0;
    if(argc >= 4) {
        auto&& sizeAsStr = STRNAMESPACE(argv[3]);
        fileSize = std::stoull(sizeAsStr);
    }

    // default creation strategy: random (equivalent to dd if=/dev/urandom ...)
    return (fileSize != 0)?createRandomFile(filename, fileSize):createEmptyFile(filename, fileSize);
}

template<typename C, typename STR>
void parseHttpCliArgs(int argc, const C* argv[],
                      std::string& method,
                      std::unordered_map<std::string,std::string>& headers,
                      std::string& requestBody,
                      uint32_t& maxRedirs,
                      bool& verifyCertificates,
                      bool& httpsOnly,
                      std::string& url,
                      STR& downloadPath) {
    std::string opt;
    std::string value;
    bool methodProvided = false, bodyProvided = false, maxRedirsProvided = false, verCertsProvided = false, httpsOnlyProvided = false;
    maxRedirs = 10; // if -1, a.k.a. 2**32 -1, is used, HTTPSClient.request for loop can be endless, due to type overflow generated by use of <= in comparison
    method = "GET";
    verifyCertificates = true;
    httpsOnly = true;
    // TODO do some refactoring here
    std::runtime_error e("Index out of bounds when parsing http options");
    std::runtime_error e1("HTTP method provided more than once");
    std::runtime_error e2("Body provided more than once");
    std::runtime_error e3("Malformed header");
    std::runtime_error e4("Redirection limit provided more than once");
    std::runtime_error e5("Verify certificates option provided more than once");
    std::runtime_error e6("Https-only option provided more than once");
    for(int i=2; i<argc; i+=2) {
        // expect -k, -X, -H, or -d, before final parameters url, and the optional download path
        opt = TOUTF(argv[i]);
        if(opt == "-k" || opt == "--insecure") {
            if(verCertsProvided) throw e5;
            verifyCertificates = false;
            verCertsProvided = true;
            i--; // i jumps by 2, so decrement it since here we are not treating a pair of cli args, but only one
        }
        else if(opt == "-H") { // headers, multiple ones allowed
            if(i+1 >= argc) throw e;
            std::string hh = TOUTF(argv[i+1]);
            auto idx = hh.find(": ");
            if(idx == std::string::npos) throw e3;
            headers[hh.substr(0,idx)] = hh.substr(idx+2);
            // TODO should we use a multi-map, are headers with repeated key allowed in HTTP?
        }
        else if(opt == "-d") { // body, only one
            if(bodyProvided) throw e2;
            if(i+1 >= argc) throw e;
            requestBody = TOUTF(argv[i+1]);
            bodyProvided = true;
        }
        else if(opt == "-X") { // http method, only one
            if(methodProvided) throw e1;
            if(i+1 >= argc) throw e;
            method = TOUTF(argv[i+1]);
            methodProvided = true;
        }
        else if(opt == "--max-redirs") {
            if(maxRedirsProvided) throw e4;
            if(i+1 >= argc) throw e;
            std::string ms = TOUTF(argv[i+1]);
            int32_t m = stoi(ms);
            maxRedirs = m; // -1 practically means follow all redirects
            maxRedirsProvided = true;
        }
        else if(opt == "--httpsOnly") { // defaults to true (i.e. equivalent to --httpsOnly 1)
            if(httpsOnlyProvided) throw e6;
            if(i+1 >= argc) throw e;
            std::string hs = TOUTF(argv[i+1]);
            int32_t h = stoi(hs);
            httpsOnly = h != 0; // abuse of notation, assume regular values to be 0,1
            httpsOnlyProvided = true;
        }
        else {
            // assume arg contains url, and next arg, if present, contains download path
            url = TOUTF(argv[i]);
            if(i+1 < argc) downloadPath = argv[i+1];
            break;
        }
    }

    if(url.empty()) throw std::runtime_error("Url is empty");
}

template<typename C>
int https1_FromArgs(int argc, const C* argv[]) {
    if(argc < 3) {
        std::string exeName = TOUTF(argv[0]);
        PRINTUNIFIED("Usage (curl-like syntax): %s <https1|httpsd> [-k | --insecure] [--httpsOnly {0|1}] [-X {GET|POST|PUT...}] [-H requestHeader1] [-H requestHeader2...] [-d requestBody] [--max-redirs N] https://my.domain.tld/my?querystring [/download/path/file.bin]\n"
                             "Be aware that, unlike curl, this program will follow redirects by default\n", exeName.c_str());
        _Exit(0);
    }
    std::string mode = TOUTF(argv[1]);

#ifdef _WIN32
    { // FIXME call common init method BEFORE invoking cli parser
        // Initialize Winsock
        WSADATA wsaData;
        auto iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
        if (iResult != 0) {
            printf("WSAStartup failed with error: %d\n", iResult);
            _Exit(572);
        }
    };
#endif
    HTTPSClient client;
    std::string url;
    auto downloadPath = STRNAMESPACE();
    std::string httpMethod;
    std::unordered_map<std::string,std::string> rqHdrs;
    std::string rqBody;
    uint32_t maxRedirs;
    bool verifyCertificates;
    bool httpsOnly;
    // test for 301/302 mixed HTTP/HTTPS redirects:
    // https://3.ly/afSg2
    // redirects to:
    // https://stackoverflow.com/questions/15261851/100x100-image-with-random-pixel-colour

    // auto ret = client.request(url, "GET", sd, 443, 0); // don't follow redirects
    int ret = -1;
    try {
        parseHttpCliArgs(argc, argv, httpMethod, rqHdrs, rqBody, maxRedirs, verifyCertificates, httpsOnly, url, downloadPath);
        if(mode == "https1") {
            // ret = client.request(url, "POST", {{"User-Agent", "ExampleAgent"}}, "Test\r\nTest\r\nTest", false, STRNAMESPACE());
            ret = client.request(url, httpMethod, rqHdrs, rqBody, false, downloadPath, verifyCertificates, httpsOnly, maxRedirs);
            auto&& respBody = client.responseBody.str();
            PRINTUNIFIED("||||||||HTTP Response Code: %d ||||||||\n", client.httpResponseCode);
            PRINTUNIFIED("||||||||HTTP Response Headers:||||||||\n%s\n||||||||\n", client.responseHeaders.c_str());
            PRINTUNIFIED("||||||||HTTP Response Body:||||||||\n%s\n||||||||\n", respBody.c_str());
        }
        else { // download response body to file
            // auto targetPath = argc < 4 ? STRNAMESPACE() : STRNAMESPACE(argv[3]);
            ret = client.request(url, httpMethod, rqHdrs, rqBody, true, downloadPath, verifyCertificates, httpsOnly, maxRedirs);
            PRINTUNIFIED("||||||||HTTP Response Code: %d ||||||||\n", client.httpResponseCode);
            PRINTUNIFIED("||||||||HTTP Response Headers:||||||||\n%s\n||||||||\n", client.responseHeaders.c_str());
        }
        return ret;
    }
    catch(std::exception& e) {
        std::cerr<<"HTTPS CLI EXCEPTION: "<<e.what()<<std::endl;
        return -1;
    }
    catch(...) { // threadExit and anything else
        PRINTUNIFIEDERROR("Unknown error\n");
        return -1;
    }
}

// enum ControlCodes in this map is not used at the current time, it's there just for readability
const std::unordered_map<std::string,std::pair<ControlCodes,cliFunction>> allowedFromCli = {
        {"download", {ControlCodes::ACTION_HTTPS_URL_DOWNLOAD, downloadFromArgs}},
        {"x0at", {ControlCodes::ACTION_CLOUD_SERVICES, upload_x0at_0x0st}},
        {"0x0st", {ControlCodes::ACTION_CLOUD_SERVICES, upload_x0at_0x0st}},
        {"https1", {ControlCodes::ACTION_CLOUD_SERVICES, https1_FromArgs}},
        {"httpsd", {ControlCodes::ACTION_CLOUD_SERVICES, https1_FromArgs}},
        {"ssh-keygen", {ControlCodes::ACTION_SSH_KEYGEN, sshKeygenFromArgs}},
        {"hashNames", {ControlCodes::ACTION_HASH, hashFromArgs}},
        {"hash", {ControlCodes::ACTION_HASH, hashFromArgs}},
        {"create", {ControlCodes::ACTION_CREATE, createFileFromArgs}},
        {"touch", {ControlCodes::ACTION_CREATE, createFileFromArgs}}
};



#endif /* __RH_CLI_H__ */
