#ifndef _XRE_ANNOUNCER_H_
#define _XRE_ANNOUNCER_H_

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <thread>
#include <chrono>
#include "../unifiedlogging.h"

#define XRE_ANNOUNCE_SERVERPORT 11111

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
	memcpy(a,&crcOut[0],4);
	return announce;
}

#ifdef _WIN32
#include "announcer_windows.h"
#else
#include "announcer_unix.h"
#endif

#endif /* _XRE_ANNOUNCER_H_ */
