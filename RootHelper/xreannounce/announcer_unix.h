#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifdef ANDROID_NDK
#include "ifaddrs-android.h"
#else
#include <ifaddrs.h>
#endif

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


std::string xreAnnouncedPath; // leave empty dir for announce for better security (will redir to home once connected)

std::vector<std::pair<std::string,std::string>> getIPAddressesWithBroadcasts() {
	std::vector<std::pair<std::string,std::string>> addresses;
    struct ifaddrs *interfaces;
    struct ifaddrs *temp_addr;
    struct sockaddr_in *sa;
    char* addr;
    // retrieve the current interfaces - returns 0 on success
    int success = getifaddrs(&interfaces);
    if (success == 0) {
		struct in_addr ips;
		// struct in_addr bas; // direct broadcast address not available in android ifaddrs-android.h, will be computed from ip and netmask
		struct in_addr nms;
		struct in_addr computed_bas;
		
        // Loop through linked list of interfaces
        for (temp_addr = interfaces; temp_addr != nullptr; temp_addr = temp_addr->ifa_next) {
			if(temp_addr->ifa_addr->sa_family == AF_INET) {
				
				sa = (struct sockaddr_in *) temp_addr->ifa_addr;
				memcpy(&ips,&(sa->sin_addr),sizeof(struct in_addr));
				addr = inet_ntoa(sa->sin_addr);
				std::string ipAddr(addr);
				
				if(ipAddr == "127.0.0.1" || ipAddr == "0.0.0.0") continue;
				memcpy(&computed_bas,&(sa->sin_addr),sizeof(struct in_addr)); // copy ip to bcast_ip, will be updated later
				
				sa = (struct sockaddr_in *) temp_addr->ifa_netmask;
				memcpy(&nms,&(sa->sin_addr),sizeof(struct in_addr));
				
				computed_bas.s_addr = ips.s_addr | (~(nms.s_addr));
				addr = inet_ntoa(computed_bas);
				std::string computedBroadcastAddr(addr);
					
				addresses.emplace_back(ipAddr,computedBroadcastAddr);
            }
        }
    }
    // Free memory
    freeifaddrs(interfaces);
    return addresses;
}

int xre_announce() { // TODO add sleep priod and total time
    struct sockaddr_in send_addr;
    int trueflag = 1, count = 0;
    int fd;
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        return -1;

    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &trueflag, sizeof trueflag) < 0) {
        PRINTUNIFIEDERROR("setsockopt error, errno is %d\n",errno);
        return -2;
    }

    memset(&send_addr, 0, sizeof send_addr);
    send_addr.sin_family = AF_INET;
    send_addr.sin_port = (in_port_t) htons(XRE_ANNOUNCE_SERVERPORT);

	auto&& ipAddresses = getIPAddressesWithBroadcasts();
	if(ipAddresses.empty()) {
		PRINTUNIFIEDERROR("No available IPs\n");
		return -4;
	}
	
    for(int i=0;i<15;i++) { // TODO parameterize total time
		for(auto& pair : ipAddresses) {
			inet_aton(pair.second.c_str(), &send_addr.sin_addr);
			auto&& announce = getPreparedAnnounce(XRE_ANNOUNCE_SERVERPORT,pair.first,xreAnnouncedPath);
			auto retval = sendto(fd, &announce[0], announce.size(), 0, (struct sockaddr*) &send_addr, sizeof send_addr);
			if (retval < announce.size()) {
				PRINTUNIFIEDERROR("sendto error, bytes or return value %zd\n",retval);
				continue;
			}
		}
        PRINTUNIFIED("Broadcast messages sent...\n");
		std::this_thread::sleep_for(std::chrono::seconds(2)); // TODO parameterize sleep period
    }
    PRINTUNIFIED("XRE server announce ended\n");
    close(fd);
    return 0;
}
