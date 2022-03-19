#include "common_win.h"
#include <tchar.h>
#include <cstdio>
#include <vector>
#include <cstdint>
#include "Utils.h"
#include "xre.h"
#include "cli.h"

int wmain(int argc, const wchar_t* argv[]) {
    initLogging();
    CoInitialize(nullptr);
    console_hwnd = GetConsoleHwnd();
    if(!console_hwnd) {
        std::cout<<"Cannot get console HWND"<<std::endl;
        _Exit(-1);
    }
    SafeCoCreateInstance(console_pTaskbarList,nullptr,console_hwnd);
    registerExitRoutines();
    print_roothelper_version();

    if(argc >= 2 && mode_is_help(TOUTF(argv[1]))) {
        auto&& x = TOUTF(argv[0]);
        print_help(x.c_str());
        return 0;
    }
    else if(argc >= 2) {
        auto&& arg1 = TOUTF(argv[1]);
        if(allowedFromCli.find(arg1) != allowedFromCli.end()) {
            PRINTUNIFIED("cli mode\n");
            return allowedFromCli.at(arg1).second(argc,argv);
        }
        else if(argv[1][0] != L'-') { // allow --homePath, etc...
            PRINTUNIFIED("Cli usage: r.exe OP args...\n");
			std::stringstream ss;
			for(auto& pair : allowedFromCli) ss << pair.first << " ";
			auto&& s = ss.str();
			PRINTUNIFIED("Available OPs: %s\n",s.c_str());
            return 0;
        }
    }

    initDefaultHomePaths();
    return xreMain(argc,argv,getSystemPathSeparator());
}
