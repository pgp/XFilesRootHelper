#include <winsock2.h>

std::string xreAnnouncedPath; // leave empty dir for announce for better security (will redir to home once connected)

/**
* Web source: http://www.cs.ubbcluj.ro/~dadi/compnet/labs/lab3/udp-broadcast.html
* linker flags for MinGW: -lwsock32
*/

std::vector<std::string> getIPAddresses() {
	std::vector<std::string> addresses;
	char name[255];
	PHOSTENT hostinfo;
	if(gethostname(name, sizeof(name)) == 0) {
		if((hostinfo = gethostbyname(name)) != nullptr) {
			int nCount = 0;
			while(hostinfo->h_addr_list[nCount]) {
				std::string addr = inet_ntoa(*(struct in_addr *)hostinfo->h_addr_list[nCount]);
				if (addr != "127.0.0.1" && addr != "::1" && addr != "0.0.0.0")
					addresses.emplace_back(addr);
				nCount++;
			}
		}
	}
	return addresses;
}
 
int xre_announce() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);
 
    SOCKET sock = socket(AF_INET,SOCK_DGRAM,0);
    if(sock==INVALID_SOCKET)
		return -1;
 
    char broadcast = '1';
 
    if(setsockopt(sock,SOL_SOCKET,SO_BROADCAST,&broadcast,sizeof(broadcast)) < 0) {
        closesocket(sock);
        return -2;
    }
 
    struct sockaddr_in Recv_addr{};
    struct sockaddr_in Sender_addr{};
 
    int len = sizeof(struct sockaddr_in);
 
    char recvbuff[50] = "";
    int recvbufflen = 50;
 
    Recv_addr.sin_family       = AF_INET;        
    Recv_addr.sin_port         = htons(XRE_ANNOUNCE_SERVERPORT);   
	Recv_addr.sin_addr.s_addr  = INADDR_BROADCAST; // equivalent to 255.255.255.255, on Windows a datagram for each interface should be sent
// better use subnet broadcast
//    Recv_addr.sin_addr.s_addr = inet_addr("192.168.43.255");
    
    std::vector<std::string> ipAddresses = getIPAddresses();
	if(ipAddresses.empty()) {
		PRINTUNIFIEDERROR("No available IPs\n");
		return -4;
	}
    
    for(int i=0;i<15;i++) { // TODO parameterize total time
		for(auto& addr : ipAddresses) {
			auto&& announce = getPreparedAnnounce(XRE_ANNOUNCE_SERVERPORT,addr,xreAnnouncedPath);
			if(sendto(sock,(const char*)(&announce[0]),announce.size(),0,(sockaddr *)&Recv_addr,sizeof(Recv_addr))==SOCKET_ERROR) {
				PRINTUNIFIEDERROR("sendto error\n");
				return -3;
			}
		}
		PRINTUNIFIED("Broadcast messages sent...\n");
		std::this_thread::sleep_for(std::chrono::seconds(2)); // TODO parameterize sleep period
	}
    PRINTUNIFIED("XRE server announce ended\n");
    closesocket(sock);
    return 0;
}
