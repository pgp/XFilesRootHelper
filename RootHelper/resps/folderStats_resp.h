#ifndef _FOLDER_STATS_RESP_H_
#define _FOLDER_STATS_RESP_H_

#include "../unifiedlogging.h"
#include "../desc/IDescriptor.h"

typedef struct {
    uint64_t childrenDirs;
    uint64_t childrenFiles;
    uint64_t totalDirs;
    uint64_t totalFiles;
    uint64_t totalSize;
} folderStats_resp_t;

void readfolderStats_resp(IDescriptor& fd, folderStats_resp_t& dataContainer) {
    fd.readAllOrExit(&(dataContainer.childrenDirs),sizeof(uint64_t));
    fd.readAllOrExit(&(dataContainer.childrenFiles),sizeof(uint64_t));
    fd.readAllOrExit(&(dataContainer.totalDirs),sizeof(uint64_t));
    fd.readAllOrExit(&(dataContainer.totalFiles),sizeof(uint64_t));
    fd.readAllOrExit(&(dataContainer.totalSize),sizeof(uint64_t));
}

void writefolderStats_resp(IDescriptor& fd, folderStats_resp_t& dataContainer) {
    fd.writeAllOrExit(&(dataContainer.childrenDirs),sizeof(uint64_t));
    fd.writeAllOrExit(&(dataContainer.childrenFiles),sizeof(uint64_t));
    fd.writeAllOrExit(&(dataContainer.totalDirs),sizeof(uint64_t));
    fd.writeAllOrExit(&(dataContainer.totalFiles),sizeof(uint64_t));
    fd.writeAllOrExit(&(dataContainer.totalSize),sizeof(uint64_t));
}

#endif /* _FOLDER_STATS_RESP_H_ */
