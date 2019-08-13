#include "common_win.h"
#include "Utils.h"
#include "xre.h"
#include "version.h"


int main(int argc, char* argv[]) {
	initLogging();
    initDefaultHomePaths();
	registerExitRoutines();
	print_roothelper_version();
	rhss = getServerSocket();
	printNetworkInfo();
    // start announce loop (for now with default parameters)
    std::thread announceThread(xre_announce);
    announceThread.detach();
	acceptLoop(rhss);
	return 0;
}
