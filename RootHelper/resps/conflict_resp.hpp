#ifndef _CONFLICT_RESP_H_
#define _CONFLICT_RESP_H_

#include <string>
#include <unistd.h>

// conflict decision responses (4 bit: 4 bit semantic splitting)
#define CD_SKIP 0x00
#define CD_SKIP_ALL 0x10
#define CD_OVERWRITE 0x01
#define CD_OVERWRITE_ALL 0x11
#define CD_REN_SRC 0x02 // followed by filename_sock_t containing new name
#define CD_REN_DEST 0x03 // followed by filename_sock_t containing new name
#define CD_REN_SRC_ALL 0x12
#define CD_REN_DEST_ALL 0x13
// for folders over existing folders
#define CD_MERGE 0x04
#define CD_MERGE_RECURSIVE 0x14
#define CD_MERGE_ALL 0x24

#define CD_CANCEL 0x05

#define NO_PREV_DEC 0xFF

// conflict types

// without merge options
#define CONFLICT_TYPE_FILE_OVER_FILE 0x01
#define CONFLICT_TYPE_FILE_OVER_DIR 0x02
#define CONFLICT_TYPE_DIR_OVER_FILE 0x03

// with merge options
#define CONFLICT_TYPE_DIR_OVER_DIR 0x04

class ConflictResp {
public:
	const uint8_t conflictType;
	// conflicting path
	const std::string srcPath;
	const std::string destPath;
	// state of previous conflict resolution, if any
	const uint8_t lastDecision;
	const int32_t lastErrno;
	
	ConflictResp(uint8_t conflictType_,
				std::string srcPath_,
				std::string destPath_,
				uint8_t lastDecision_,
				int32_t lastErrno_) :
				conflictType(conflictType_),
				srcPath(srcPath_),
				destPath(destPath_),
				lastDecision(lastDecision_),
				lastErrno(lastErrno_) {}
	
	int writeResp(int fd) {
		if (::write(fd,&conflictType,sizeof(uint8_t)) < sizeof(uint8_t)) return -1;
		uint16_t len = srcPath.length();
		if (::write(fd,&len,sizeof(uint16_t)) < sizeof(uint16_t)) return -1;
		if (::write(fd,srcPath.c_str(),len) < len) return -1;
		len = destPath.length();
		if (::write(fd,&len,sizeof(uint16_t)) < sizeof(uint16_t)) return -1;
		if (::write(fd,destPath.c_str(),len) < len) return -1;
		if (::write(fd,&lastDecision,sizeof(uint8_t)) < sizeof(uint8_t)) return -1;
		if (::write(fd,&lastErrno,sizeof(int32_t)) < sizeof(int32_t)) return -1;
		return 0;
	}
};

#endif /* _CONFLICT_RESP_H */
