#ifndef _AF_UNIX_UTILS_H_
#define _AF_UNIX_UTILS_H_

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <pwd.h>
#include <libgen.h>
#include "unifiedlogging.h"

int getServerUnixDomainSocket(struct sockaddr_un& addr, socklen_t& len, std::string socketPathname) {
	int unixSocketFd;
	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
#ifdef __linux__ // use abstract namespace socket
	addr.sun_path[0] = '\0';
	strncpy(addr.sun_path + 1, socketPathname.c_str(), socketPathname.size());
	len = offsetof(struct sockaddr_un, sun_path) + 1 + socketPathname.size();
#else // use standard filesystem-persisted socket
	strncpy(addr.sun_path, "/tmp/", 5);
	strncpy(addr.sun_path + 5, socketPathname.c_str(), socketPathname.size());
	len = offsetof(struct sockaddr_un, sun_path) + 5 + socketPathname.size();
	if (unlink(addr.sun_path) != 0) {
		if(errno != ENOENT) {
			PRINTUNIFIEDERROR("Unable to remove already-existing socket, errno is %d\n",errno);
			return -1;
		}
	}
#endif

	if ((unixSocketFd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		PRINTUNIFIEDERROR("socket error, errno is %d\n",errno);
		return -1;
	}

	if (bind(unixSocketFd, (struct sockaddr *)(&addr), len) == -1) {
		PRINTUNIFIEDERROR("bind error, errno is %d\n",errno);
		return -1;
	}

	if (listen(unixSocketFd, MAX_CLIENTS) == -1) {
		PRINTUNIFIEDERROR("listen error, errno is %d\n",errno);
		return -1;
	}
	return unixSocketFd;
}
#endif
#endif /* _AF_UNIX_UTILS_H_ */
