#ifndef _FIND_RESP_H_
#define _FIND_RESP_H_

#include <vector>
#include "ls_resp.hpp"

typedef struct {
	ls_resp_t fileItem;
	std::vector<uint8_t> contentAround;
	unsigned long long offset;
} find_resp_t;

int writefind_resp(IDescriptor& fd, const find_resp_t& dataContainer) {
    // TODO eol to be sent explicitly by caller (helps composition of requests)
    
	if (writels_resp(fd,dataContainer.fileItem)) return -1;
	
    uint8_t len1 = dataContainer.contentAround.size();
    if (fd.write(&len1,sizeof(uint8_t)) < sizeof(uint8_t)) return -1;
    if (len1 != 0) {
		if (fd.write(&(dataContainer.contentAround[0]),len1) < len1) return -1;
		if (fd.write(&(dataContainer.offset),sizeof(uint64_t)) < sizeof(uint64_t)) return -1;
	}
    return 0;
}

#endif /* _FIND_RESP_H_ */
