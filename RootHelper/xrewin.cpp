#include "common_win.h"
#include "Utils.h"
#include "xre.h"
#include "version.h"


int main(int argc, char* argv[]) {
	initLogging();
	registerExitRoutines();
	print_roothelper_version();
	rhss = getServerSocket();
	printNetworkInfo();
	acceptLoop(rhss);
	return 0;
}
