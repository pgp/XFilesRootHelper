#include <winsock2.h>

/**
* Web source: http://www.cs.ubbcluj.ro/~dadi/compnet/labs/lab3/udp-broadcast.html
* linker flags for MinGW: -lwsock32
*/
 
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
 
    char sendMSG[] ="Broadcast message from SLAVE TAG";
 
    char recvbuff[50] = "";
    int recvbufflen = 50;
 
    Recv_addr.sin_family       = AF_INET;        
    Recv_addr.sin_port         = htons(XRE_ANNOUNCE_SERVERPORT);   
//	Recv_addr.sin_addr.s_addr  = INADDR_BROADCAST; // this isq equiv to 255.255.255.255
// better use subnet broadcast
    Recv_addr.sin_addr.s_addr = inet_addr("192.168.43.255");
 
 
    for(;;) {
		if(sendto(sock,announce_message.c_str(),announce_message.size(),0,(sockaddr *)&Recv_addr,sizeof(Recv_addr))==SOCKET_ERROR) // WARNING: was strlen+1
			return -3;
		std::cout<<"Broadcast message sent..."<<std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(1)); // TODO parameterize sleep period
	}
 
    closesocket(sock);
    WSACleanup();
    return 0;
}