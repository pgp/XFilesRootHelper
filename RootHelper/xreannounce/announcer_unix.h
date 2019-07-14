#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

std::vector<std::string> getIPAddresses(){
	std::vector<std::string> addresses;
    struct ifaddrs *interfaces = nullptr;
    struct ifaddrs *temp_addr = nullptr;
    // retrieve the current interfaces - returns 0 on success
    int success = getifaddrs(&interfaces);
    if (success == 0) {
        // Loop through linked list of interfaces
        temp_addr = interfaces;
        while(temp_addr != nullptr) {
            if(temp_addr->ifa_addr->sa_family == AF_INET || temp_addr->ifa_addr->sa_family == AF_INET6) {
				std::string addr = inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr);
                if (addr != "127.0.0.1" && addr != "::1" && addr != "0.0.0.0")
					addresses.emplace_back(addr);
            }
            temp_addr = temp_addr->ifa_next;
        }
    }
    // Free memory
    freeifaddrs(interfaces);
    return addresses;
}

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

	std::vector<std::string> ipAddresses = getIPAddresses();
	if(ipAddresses.empty()) {
		std::cerr<<"No available IPs"<<std::endl;
		return -4;
	}
    for(;;) { // TODO parameterize total time
		for(auto& addr : ipAddresses)
			if (sendto(fd, addr.c_str(), addr.size(), 0, (struct sockaddr*) &send_addr, sizeof send_addr) < 0) // WARNING: was strlen+1
				return -3;
        std::cout<<"Broadcast messages sent..."<<std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(1)); // TODO parameterize sleep period
    }
    close(fd);
    return 0;
}
