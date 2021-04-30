#include "version.h"
#include "path_utils.h"

const char* help_args = R"HELP1(Usage:
- standard mode (interact with clients via unix-domain sockets):
  %s <valid_euid> [optional: <socket_name>]
- standalone mode (xre remote server only, the only mode implemented on windows platforms):
  unix: %s --xre
  win:  %s
  Standalone mode can also be enabled by default even on Unix, by renaming this executable to "xre".
- additional arguments for standalone mode:
  --homePath=<default (home) path>
  Clients providing an empty path will be redirected here. If this is not provided, OS defaults are the content of EXTERNAL_STORAGE env variable on Android, /tmp on Unix in general, and C:\Windows\Temp on Windows.
  --announcedPath=<path to be announced>
  The server will announce its IP addresses and this path via IGMP broadcast. By default an empty path is announced.
  --noAnnounce
  Disables IGMP announce (enabled by default).
  --exposedPath=<root path of allowed remote browsing>
  Clients will be allowed to browse only this path and its descendants.

- print this help:
  unix: %s --help
  win:  %s /?
)HELP1";

inline bool prog_is_xre(const std::string& arg) { // match *xre* and *XRE*
	return  arg.find("xre") != std::string::npos ||
			arg.find("XRE") != std::string::npos;
}

inline bool mode_is_help(const std::string& arg) {
	return  arg == "--help" ||
			arg == "/?";
}

inline bool mode_is_xre(const std::string& arg) {
	return 	arg == "--xre";
}

// TODO to be replaced with a proper argument parser
template<typename C,typename STR>
std::vector<STR> getXREPaths(int argc, C* argv[], bool& enableAnnounce, const STR& placeholder) {
    std::vector<STR> paths(3);
    
	STR&& homePrefix = FROMUTF("--homePath=");
    STR&& announcePrefix = FROMUTF("--announcedPath=");
    STR&& exposePrefix = FROMUTF("--exposedPath=");
    STR&& noAnnounceOption = FROMUTF("--noAnnounce");
    
	for (int i=0; i<argc; i++) {
        STR p = argv[i];

        if(noAnnounceOption==p) {
            PRINTUNIFIED("Announce is disabled\n");
            enableAnnounce = false;
            continue;
        }
        // TODO refactor with a for loop
        if(p.rfind(homePrefix,0)==0) {
            paths[0] = p.substr(homePrefix.length());
            if(!paths[0].empty()) {
                auto&& tmp = canonicalize_path(paths[0]);
                if(!tmp.empty()) paths[0] = tmp;
            }
            continue;
        }
        if(p.rfind(announcePrefix,0)==0) {
            paths[1] = p.substr(announcePrefix.length());
            if(!paths[1].empty()) {
                auto&& tmp = canonicalize_path(paths[1]);
                if(!tmp.empty()) paths[1] = tmp;
            }
            continue;
        }
        if(p.rfind(exposePrefix,0)==0) {
            paths[2] = p.substr(exposePrefix.length());
            if(!paths[2].empty()) {
                auto&& tmp = canonicalize_path(paths[2]);
                if(!tmp.empty()) paths[2] = tmp;
            }
            continue;
        }
    }
    return paths;
}

void print_help(const char* program_name) {
	PRINTUNIFIED(help_args,program_name,program_name,program_name,program_name,program_name);
	_Exit(0);
}

constexpr int rh_default_uid = 0;
const std::string rh_uds_default_name = "theroothelper";
