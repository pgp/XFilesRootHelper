#ifndef _SINGLE_STATS_RESP_H_
#define _SINGLE_STATS_RESP_H_

#include "../desc/IDescriptor.h"

typedef struct {
    std::string group; // 1-byte len
    std::string owner; // 1-byte len
    uint32_t creationTime;
    uint32_t lastAccessTime;
    uint32_t modificationTime;
    char permissions[10];
    uint64_t size;
} singleStats_resp_t;

void readsingleStats_resp(IDescriptor& fd, singleStats_resp_t& dataContainer) {
    uint8_t len;
    fd.readAllOrExit(&len,sizeof(uint8_t));
    dataContainer.group.resize(len);
    fd.readAllOrExit((char*)(dataContainer.group.c_str()),len);

    fd.readAllOrExit(&len,sizeof(uint8_t));
    dataContainer.owner.resize(len);
    fd.readAllOrExit((char*)(dataContainer.owner.c_str()),len);

    fd.readAllOrExit(&(dataContainer.creationTime),sizeof(uint32_t));
    fd.readAllOrExit(&(dataContainer.lastAccessTime),sizeof(uint32_t));
    fd.readAllOrExit(&(dataContainer.modificationTime),sizeof(uint32_t));
    fd.readAllOrExit(dataContainer.permissions,sizeof(uint8_t)*10);
    fd.readAllOrExit(&(dataContainer.size),sizeof(uint64_t));
}

void writesingleStats_resp(IDescriptor& fd, singleStats_resp_t& dataContainer) {
    uint8_t len = dataContainer.group.size();
    fd.writeAllOrExit(&len,sizeof(uint8_t));
    fd.writeAllOrExit(dataContainer.group.c_str(),len);

    len = dataContainer.owner.size();
    fd.writeAllOrExit(&len,sizeof(uint8_t));
    fd.writeAllOrExit(dataContainer.owner.c_str(),len);

    fd.writeAllOrExit(&(dataContainer.creationTime),sizeof(uint32_t));
    fd.writeAllOrExit(&(dataContainer.lastAccessTime),sizeof(uint32_t));
    fd.writeAllOrExit(&(dataContainer.modificationTime),sizeof(uint32_t));
    fd.writeAllOrExit(dataContainer.permissions,sizeof(uint8_t)*10);
    fd.writeAllOrExit(&(dataContainer.size),sizeof(uint64_t));
}

#endif /* _SINGLE_STATS_RESP_H_ */
