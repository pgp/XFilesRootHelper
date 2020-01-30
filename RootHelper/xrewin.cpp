#include "common_win.h"
#include <tchar.h>
#include <cstdio>
#include <vector>
#include <cstdint>
#include "Utils.h"
#include "xre.h"

int wmain(int argc, wchar_t* argv[]) {
    initLogging();

    CoInitialize(nullptr);

    console_hwnd = GetConsoleHwnd();
    if(!console_hwnd) {
        std::cout<<"Cannot get console HWND"<<std::endl;
        exit(-1);
    }
    SafeCoCreateInstance(console_pTaskbarList,nullptr,console_hwnd);

    initDefaultHomePaths();
    registerExitRoutines();
    print_roothelper_version();

    return xreMain(argc,argv,getSystemPathSeparator());
}
