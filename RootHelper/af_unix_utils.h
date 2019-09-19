#ifndef _AF_UNIX_UTILS_H_
#define _AF_UNIX_UTILS_H_

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <pwd.h>
#include <libgen.h>
#include "unifiedlogging.h"

#ifndef CMSG_ALIGN
#	ifdef __sun__
#		define CMSG_ALIGN _CMSG_DATA_ALIGN
#	else
#		define CMSG_ALIGN(len) (((len)+sizeof(long)-1) & ~(sizeof(long)-1))
#	endif
#endif

#ifndef CMSG_SPACE
#	define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr))+CMSG_ALIGN(len))
#endif

#ifndef CMSG_LEN
#	define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr))+(len))
#endif

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

/**
 * Web sources:
 * https://9fans.github.io/plan9port/man/man3/sendfd.html
 * https://github.com/RepositoryBackups/plan9port/blob/master/src/lib9/sendfd.c
 *
 * Additional web sources:
 * https://stackoverflow.com/questions/2358684/can-i-share-a-file-descriptor-to-another-process-on-linux-or-are-they-local-to-t
 * https://stackoverflow.com/questions/4489433/sending-file-descriptor-over-unix-domain-socket-and-select
 */

/**
 * Sends a file descriptor over a connected Unix Domain Socket
 */
int sendfd(int unixDomainSocket, int fdToSend) {
    char buf[1];
    struct iovec iov{};
    struct msghdr msg{};
    struct cmsghdr *cmsg;
    char cms[CMSG_SPACE(sizeof(int))];

    buf[0] = 0;
    iov.iov_base = buf;
    iov.iov_len = 1;

    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = (caddr_t)cms;
    msg.msg_controllen = CMSG_LEN(sizeof(int));

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    memmove(CMSG_DATA(cmsg), &fdToSend, sizeof(int));

    if((sendmsg(unixDomainSocket, &msg, 0)) != iov.iov_len)
        return -1;
    return 0;
}

/**
 * Receives a file descriptor over a connected Unix Domain Socket
 */
int recvfd(int unixDomainSocket) {
    int n;
    int receivedFd;
    char buf[1];
    struct iovec iov{};
    struct msghdr msg{};
    struct cmsghdr *cmsg;
    char cms[CMSG_SPACE(sizeof(int))];

    iov.iov_base = buf;
    iov.iov_len = 1;

    memset(&msg, 0, sizeof msg);
    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    msg.msg_control = (caddr_t)cms;
    msg.msg_controllen = sizeof cms;

    if((n=recvmsg(unixDomainSocket, &msg, 0)) < 0)
        return -1;
    if(n == 0){
        perror("unexpected EOF");
        return -1;
    }
    cmsg = CMSG_FIRSTHDR(&msg);
    memmove(&receivedFd, CMSG_DATA(cmsg), sizeof(int));
    return receivedFd;
}

#endif
#endif /* _AF_UNIX_UTILS_H_ */
