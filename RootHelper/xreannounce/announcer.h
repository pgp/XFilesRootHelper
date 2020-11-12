#ifndef _XRE_ANNOUNCER_H_
#define _XRE_ANNOUNCER_H_

#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <utility>
#include "../unifiedlogging.h"

#define XRE_ANNOUNCE_SERVERPORT 11111

//~ void broadcast_address_from_ip_and_netmask(uint32_t* addr, uint32_t* netmask, uint32_t* bAddr, bool ipv4) {
	//~ if(ipv4) {
		//~ bAddr[0] = addr[0] | (~netmask[0]);
	//~ }
	//~ else { // ipv6 (128 bit address)
		//~ bAddr[0] = addr[0] | (~netmask[0]);
		//~ bAddr[1] = addr[1] | (~netmask[1]);
		//~ bAddr[2] = addr[2] | (~netmask[2]);
		//~ bAddr[3] = addr[3] | (~netmask[3]);
	//~ }
//~ }

#ifndef _WIN32
typedef int SOCKET;
#endif

#ifdef _WIN32
typedef unsigned int in_addr_t;
#endif

/* Define IPV6_ADD_MEMBERSHIP for FreeBSD and Mac OS X */
#ifndef IPV6_ADD_MEMBERSHIP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif

SOCKET mcast_send_socket(const std::string& multicastIP, const std::string& multicastPort,  int multicastTTL, struct addrinfo **multicastAddr) {

    SOCKET sock;
    struct addrinfo hints{};    /* Hints for name lookup */

#ifdef WIN32
    WSADATA trash;
    if(WSAStartup(MAKEWORD(2,0),&trash)!=0) {
        PRINTUNIFIEDERROR("Couldn't init Windows Sockets\n");
        return -1;
    }
#endif

    /*
      Resolve destination address for multicast datagrams
    */
    hints.ai_family   = PF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags    = AI_NUMERICHOST;
    int status;
    if ((status = getaddrinfo(multicastIP.c_str(), multicastPort.c_str(), &hints, multicastAddr)) != 0 )
    {
        PRINTUNIFIEDERROR("getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    /*
       Create socket for sending multicast datagrams
    */
    if ( (sock = socket((*multicastAddr)->ai_family, (*multicastAddr)->ai_socktype, 0)) < 0 ) {
        perror("socket() failed");
        freeaddrinfo(*multicastAddr);
        return -1;
    }

    /*
       Set TTL of multicast packet
    */
    if ( setsockopt(sock,
                    (*multicastAddr)->ai_family == PF_INET6 ? IPPROTO_IPV6        : IPPROTO_IP,
                    (*multicastAddr)->ai_family == PF_INET6 ? IPV6_MULTICAST_HOPS : IP_MULTICAST_TTL,
                    (char*) &multicastTTL, sizeof(multicastTTL)) != 0 ) {
        perror("setsockopt() failed");
        freeaddrinfo(*multicastAddr);
        return -1;
    }

    /*
       set the sending interface
    */
    if((*multicastAddr)->ai_family == PF_INET) {
        in_addr_t iface = INADDR_ANY; /* well, yeah, any */
        if(setsockopt (sock,
                       IPPROTO_IP,
                       IP_MULTICAST_IF,
                       (char*)&iface, sizeof(iface)) != 0) {
            perror("interface setsockopt() sending interface");
            freeaddrinfo(*multicastAddr);
            return -1;
        }
    }
    if((*multicastAddr)->ai_family == PF_INET6) {
        unsigned int ifindex = 0; /* 0 means 'default interface'*/
        if(setsockopt (sock,
                       IPPROTO_IPV6,
                       IPV6_MULTICAST_IF,
                       (char*)&ifindex, sizeof(ifindex)) != 0) {
            perror("interface setsockopt() sending interface");
            freeaddrinfo(*multicastAddr);
            return -1;
        }
    }

    return sock;
}

std::vector<uint8_t> getPreparedAnnounce(uint16_t port, const std::string& ip, const std::string& path) {
	const uint16_t ipLength = ip.size();
	const uint16_t pathLength = path.size();
	const uint32_t totalPayloadSize = 3*sizeof(uint16_t)+ipLength+pathLength;
	std::vector<uint8_t> announce(4+totalPayloadSize);
	uint8_t* a = &announce[0];
	memcpy(a+4,&port,sizeof(uint16_t));
	memcpy(a+4+sizeof(uint16_t),&ipLength,sizeof(uint16_t));
	memcpy(a+4+2*sizeof(uint16_t),ip.c_str(),ipLength);
	memcpy(a+4+2*sizeof(uint16_t)+ipLength,&pathLength,sizeof(uint16_t));
	memcpy(a+4+3*sizeof(uint16_t)+ipLength,path.c_str(),pathLength);
	
	std::unique_ptr<Botan::HashFunction> crc32(Botan::HashFunction::create("CRC32"));
	crc32->update(a+4,totalPayloadSize);
	auto&& crcOut = crc32->final();
	auto* crcOut_ = &crcOut[0];
	// CRC is big-endian, convert to little
	a[0] = crcOut_[3];
	a[1] = crcOut_[2];
	a[2] = crcOut_[1];
	a[3] = crcOut_[0];

	return announce;
}

#ifdef _WIN32
#include "announcer_windows.h"
#else
#include "announcer_unix.h"
#endif

#endif /* _XRE_ANNOUNCER_H_ */
