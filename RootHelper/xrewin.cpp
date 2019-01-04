#include "common_win.h"
#include "Utils.h"
#include "xre.h"


int main(int argc, char* argv[]) {
	initLogging();
	registerExitRoutines();
	rhss = getServerSocket();
	printNetworkInfo();
	acceptLoop(rhss);
	return 0;
}