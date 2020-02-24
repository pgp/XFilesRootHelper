#ifndef __RH_CLI_PARSER_H__
#define __RH_CLI_PARSER_H__

#include <unordered_map>
#include "../common_uds.h"
#include "../tls/basic_https_client.h"
#include "../desc/SinkDescriptor.h"

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

const std::unordered_map<std::string,std::pair<ControlCodes,cliFunction>> allowedFromCli = {
        {"download", {ControlCodes::ACTION_HTTPS_URL_DOWNLOAD, downloadFromArgs}}
};



#endif /* __RH_CLI_PARSER_H__ */