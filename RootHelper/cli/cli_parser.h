#ifndef __RH_CLI_PARSER_H__
#define __RH_CLI_PARSER_H__

#include <unordered_map>
#include "../common_uds.h"
#include "../tls/basic_https_client.h"
#include "../desc/SinkDescriptor.h"
#include "../rh_hasher_botan.h"

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
    std::string destDir = (argc>=4)?TOUTF(argv[3]):"."; // NOT TOUNIXPATH, user is expected to pass the correctly formatted path from cli
    // detect filename from Content-Disposition header if not passed as argument
    std::string destFilename = (argc>=5)?TOUTF(argv[4]):""; // TODO verify correctness on windows with utf8 chars
    int port = 443;
    auto httpRet = httpsUrlDownload_internal(cl,targetUrl,port,destDir,destFilename,inRb,redirectUrl);

    // HTTP redirect limit
    for(int i=0;i<5;i++) {
        if(httpRet == 200) break;
        if(httpRet != 301 && httpRet != 302) {
            errno = httpRet;
            sendErrorResponse(cl);
            break;
        }
        inRb.reset();
        std::string target = redirectUrl;
        httpRet = httpsUrlDownload_internal(cl,target,port,destDir,destFilename,inRb,redirectUrl);
    }

    return httpRet==200?0:httpRet;
}

template<typename C>
int sshKeygenFromArgs(int argc, const C* argv[]) {
    // synopsis (RSA only): r.exe ssh-keygen [keySize=4096] [destDir='.'] [prvKeyName=id_rsa]

    uint32_t keySize = 4096;
    if(argc >= 3) {
        std::string arg2 = TOUTF(argv[2]);
        keySize = std::atoi(arg2.c_str());
    }
    std::string destDir = (argc>=4)?TOUTF(argv[3]):".";
    std::string prvKeyName = (argc>=5)?TOUTF(argv[4]):"id_rsa";
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
        for(auto& s: rh_hashLabels) PRINTUNIFIED("%s ",s.c_str());
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
    for(auto& rhl: rh_hashLabels) tmpAlgoMap[rhl] = tmpIdx++;
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

const std::unordered_map<std::string,std::pair<ControlCodes,cliFunction>> allowedFromCli = {
        {"download", {ControlCodes::ACTION_HTTPS_URL_DOWNLOAD, downloadFromArgs}},
        {"ssh-keygen", {ControlCodes::ACTION_SSH_KEYGEN, sshKeygenFromArgs}},
        {"hashNames", {ControlCodes::ACTION_HASH, hashFromArgs}},
        {"hash", {ControlCodes::ACTION_HASH, hashFromArgs}},
        {"create", {ControlCodes::ACTION_CREATE, createFileFromArgs}},
        {"touch", {ControlCodes::ACTION_CREATE, createFileFromArgs}}
};



#endif /* __RH_CLI_PARSER_H__ */
