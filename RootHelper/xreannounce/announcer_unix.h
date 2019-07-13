#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int xre_announce() { // TODO add sleep priod and total time
    struct sockaddr_in send_addr, recv_addr;
    int trueflag = 1, count = 0;
    int fd;
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        return -1;

    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &trueflag, sizeof trueflag) < 0)
        return -2;

    memset(&send_addr, 0, sizeof send_addr);
    send_addr.sin_family = AF_INET;
    send_addr.sin_port = (in_port_t) htons(XRE_ANNOUNCE_SERVERPORT);
    // broadcasting address for unix (?)
    inet_aton("192.168.43.255", &send_addr.sin_addr);
    // send_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    for(;;) { // TODO parameterize total time
        if (sendto(fd, announce_message.c_str(), announce_message.size(), 0, (struct sockaddr*) &send_addr, sizeof send_addr) < 0) // WARNING: was strlen+1
            return -3;
        std::cout<<"Broadcast message sent..."<<std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(1)); // TODO parameterize sleep period
    }
    close(fd);
    return 0;
}
