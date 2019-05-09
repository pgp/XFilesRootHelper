#ifndef _RH_IO_WRAPPERS_H_
#define _RH_IO_WRAPPERS_H_

#include <cstdlib>
#include <thread>
#include <vector>
#include <string>
#include "unifiedlogging.h"
#include "desc/IDescriptor.h"

constexpr uint8_t RESPONSE_OK = 0x00;
constexpr uint8_t RESPONSE_ERROR = 0xFF;

constexpr uint64_t maxuint = -1; // 2**64 - 1
constexpr uint64_t maxuint_2 = -2; // 2**64 - 2, for indicating termination of series of progresses (in copy dir)

// default: uint16_t

std::string readStringWithLen(IDescriptor& desc) {
    uint16_t len;
    desc.readAllOrExit(&len,sizeof(uint16_t));
    if (len == 0) return "";
    std::string s(len,0);
    desc.readAllOrExit((char*)(s.c_str()),len);
    return s;
}

// string with uint8_t len
std::string readStringWithByteLen(IDescriptor& desc) {
    uint8_t len;
    desc.readAllOrExit(&len,sizeof(uint8_t));
    if (len == 0) return "";
    std::string s(len,0);
    desc.readAllOrExit((char*)(s.c_str()),len);
    return s;
}

void writeStringWithLen(IDescriptor& desc, const std::string& s) {
    uint16_t len = (uint16_t)(s.size());
    desc.writeAllOrExit(&len,sizeof(uint16_t));
    desc.writeAllOrExit(s.c_str(),len);
}

// BEWARE of endianness when packing two shorts into an integer (one single length read of uint32_t)

std::vector<std::string> readPairOfStringsWithPairOfLens(IDescriptor& desc) {
    uint16_t lens[2] = {};
    desc.readAllOrExit(&lens[0],sizeof(uint16_t));
    desc.readAllOrExit(&lens[1],sizeof(uint16_t));

    if (lens[0] == 0) return {"",""};
    std::vector<std::string> v = {std::string(lens[0],0),std::string(lens[1],0)};

    desc.readAllOrExit((char*)(v[0].c_str()),lens[0]);
    desc.readAllOrExit((char*)(v[1].c_str()),lens[1]);

    return v;
}

void writePairOfStringsWithPairOfLens(IDescriptor& desc, std::vector<std::string>& v) {
    uint16_t lens[2] = {};
    lens[0] = v[0].size();
    lens[1] = v[1].size();
    
    desc.writeAllOrExit(&lens[0],sizeof(uint16_t));
    desc.writeAllOrExit(&lens[1],sizeof(uint16_t));
    
    if (lens[0] == 0) return;
    desc.writeAllOrExit((char*)(v[0].c_str()),lens[0]);
    desc.writeAllOrExit((char*)(v[1].c_str()),lens[1]);
}

int32_t receiveBaseResponse(IDescriptor& desc) {
    // ok or error response
    uint8_t resp;
    int errnum;
    desc.readAllOrExit(&resp,sizeof(uint8_t));
    if (resp == RESPONSE_OK) return 0;
    else {
		desc.readAllOrExit(&errnum,sizeof(int32_t));
		return errnum;
	}
}

void sendBaseResponse(int ret, IDescriptor& outDesc) {
    // ok or error response
    if (ret == 0) outDesc.writeAllOrExit(&RESPONSE_OK, sizeof(uint8_t));
    else {
        int r_errno = errno;
        outDesc.writeAllOrExit(&RESPONSE_ERROR, sizeof(uint8_t));
        outDesc.writeAllOrExit(&r_errno, sizeof(int));
    }
}

inline void sendOkResponse(IDescriptor& outDesc) {sendBaseResponse(0, outDesc);}
inline void sendErrorResponse(IDescriptor& outDesc) {sendBaseResponse(-1, outDesc);}

void sendEndProgressAndBaseResponse(int ret, IDescriptor& outDesc) {
    outDesc.writeAllOrExit(&maxuint,sizeof(uint64_t));
    sendBaseResponse(ret,outDesc);
}

inline void sendEndProgressAndOkResponse(IDescriptor& outDesc) {sendEndProgressAndBaseResponse(0,outDesc);}
inline void sendEndProgressAndErrorResponse(IDescriptor& outDesc) {sendEndProgressAndBaseResponse(-1,outDesc);}

// for p7zip callbacks
void writeAllOrExitProcess(IDescriptor& fd, const void* buf, size_t count) {
    ssize_t writtenBytes = fd.writeAll(buf,count);
    ssize_t count_ = count; // BEWARE UNSIGNED TO SIGNED COMPARISONS!!!
    if (writtenBytes < count_) {
        PRINTUNIFIED("Thread %s terminating process %d on write error\n",getThreadIdAsString().c_str(),getpid());
        fd.close();
        _Exit(-1);
    }
}

#endif /* _RH_IO_WRAPPERS_H_ */
