#ifndef _RH_FILE_COPIER_CONSTANTS_H_
#define _RH_FILE_COPIER_CONSTANTS_H_

// adapted from cfl resp.h
constexpr uint8_t CD_SKIP = 0x00;
constexpr uint8_t CD_SKIP_ALL = 0x10;
constexpr uint8_t CD_OVERWRITE = 0x01;
constexpr uint8_t CD_OVERWRITE_ALL = 0x11;
constexpr uint8_t CD_REN_SRC = 0x02; // followed by filename_sock_t containing new name
constexpr uint8_t CD_REN_DEST = 0x03; // followed by filename_sock_t containing new name
constexpr uint8_t CD_REN_SRC_ALL = 0x12;
constexpr uint8_t CD_REN_DEST_ALL = 0x13;
// for folders over existing folders
constexpr uint8_t CD_MERGE = 0x04;
constexpr uint8_t CD_MERGE_RECURSIVE = 0x14;
constexpr uint8_t CD_MERGE_ALL = 0x24;

constexpr uint8_t CD_CANCEL = 0x05;

constexpr uint8_t NO_PREV_DEC = 0xFF;


// not sure if needed
constexpr uint8_t OK_TYPE = 3; // beware with != 0 checks, otherwise changing CONFLICT_TYPE_DIR and CONFLICT_TYPE_FILE to non-array-index values implies also changing index access to permanentDecs array
constexpr uint8_t UNSOLVABLE_ERROR_TYPE = 2;


// 8-byte indicators (progress,size & co)
constexpr uint64_t EOF_ind = -1; // FIXME redundant, already defined as maxuint in iowrappers.h
constexpr uint64_t EOFs_ind = -2; // FIXME redundant, already defined as maxuint_2 in iowrappers.h
constexpr uint64_t CFL_ind = -3; // conflict
constexpr uint64_t ERR_ind = -4; // unsolvable error
constexpr uint64_t SKIP_ind = -5; // skip folder indication (to receive outer progress advancement in SKIP_ALL for folder conflicts)

// conflict types
constexpr uint8_t CONFLICT_TYPE_FILE = 0;
constexpr uint8_t CONFLICT_TYPE_DIR = 1;

#endif /* _RH_FILE_COPIER_CONSTANTS_H_ */
