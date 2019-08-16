#include "common_win.h"
#include "Utils.h"
#include "xre.h"


int wmain(int argc, wchar_t* argv[]) {
    initLogging();
    initDefaultHomePaths();
    registerExitRoutines();
    print_roothelper_version();

    return xreMain(argc,argv,getSystemPathSeparator());
}
