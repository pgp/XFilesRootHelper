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
