#ifndef COMMON_UDS_H
#define COMMON_UDS_H

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstdarg>
#include <cerrno>
#include <cstddef>
#include <cstring>

// request types
#include "resps/ls_resp.hpp"
#include "resps/find_resp.hpp"
#include "reqs/compress_rq_options.h"

#ifdef _WIN32
#include "diriterator/windirIterator.h"
windirIteratorFactory itf;
#else
#include "diriterator/readdirIterator.h"
readdirIteratorFactory itf;
#endif

constexpr int MAX_CLIENTS = 20; // 10 short ops, 10 longterm ops sessions

#ifndef _WIN32
#include "af_unix_utils.h"
#endif

// action table // TODO these are to be considered 5-bit indicative codes, choose if 3 bits are MSBs or LSBs
#define ACTION_LS 0x01 // 1 input string (directory pathname)
// COPY/MOVE: output directory pathname concatenated to array of input file pathnames TODO to be decided
#define ACTION_MOVE 0x02
#define ACTION_COPY 0x03
#define ACTION_DELETE 0x04 // array of strings as input
#define ACTION_STATS 0x05
#define ACTION_COMPRESS 0x06 // output file pathname concatenated to array of input files pathnames
#define ACTION_EXTRACT 0X07
#define ACTION_EXISTS 0X08 // exists? / is file ? is directory ? based upon flag bits (3 INDEPENDENT bits - semantically not independent, since is file true means file exists)
#define ACTION_CREATE 0X09 // create file or directory based upon flag bits
#define ACTION_HASH 0x0A
#define ACTION_FIND 0x0B
#define ACTION_KILL 0x0C
#define ACTION_GETPID 0X0D
//~ #define ACTION_FORK 0x0E
#define ACTION_FILE_IO 0x0F // flags: 000 - client sends stream (written to file), 111 - client receives stream (read from file)

#define ACTION_DOWNLOAD 0x10
constexpr uint8_t ACTION_UPLOAD = 0x11; // flags: 000 - receive list of path pairs, 111 - receive file descriptors over UDS (one by one)

#define REMOTE_SERVER_MANAGEMENT 0x12 // flags: 010 - get status, 111 - start, 000 - stop

#define REMOTE_CONNECT 0x14

#define ACTION_SETATTRIBUTES 0x15 // TODO can be merged into another related action code, eg. ACTION_CREATE

#define ACTION_SSH_KEYGEN 0x16

#define ACTION_LINK 0x17

#define ACTION_HTTPS_URL_DOWNLOAD 0x18

// use 1-string of 5 bits (0x1F = 31 = 11111b)
// action: exit or cancel (flags: 000 exit, 111: cancel)
#define ACTION_EXIT 0x1F // cannot use 0x00 as exit request code, since a client connecting and disconnecting without sending any request will appear as if it has sent 0x00 (FIN byte?)

#define NULL_OR_WRONG_PASSWORD 0x101010
#define CRC_FAILED 0x03 // from internal 7-zip error codes

/*
 * Request type:
 * 1    LS (3 bits - 1 bit used for children only vs whole subtree listing)
 * 2    MV (3 conflict bits)
 * 3    CP (3 conflict bits)
 * 4    DEL
 * 5    STATS (3 bits: file/dir/multi)
 * 6    COMPRESS (3 bits: init/add/close)
 * 7    EXTRACT
 * 8    EXISTS/ISFILE/ISDIR (3 flag bits)
 * 9    CREATE (FILE/DIR) (1 flag bit)
 * A    HASH
 * B    FIND IN FILENAMES AND/OR CONTENT
 * C    KILL ANOTHER PROCESS
 * D    GET PID OF THIS PROCESS
 * E	FORK ANOTHER ROOTHELPER PROCESS
 */

typedef struct {
    uint8_t request:5; // LSBits in native order
    uint8_t flags:3; // MSBits in native order
} request_type;

// getters and setters for 3 flag bits b0...b2 (LSB to MSB)
// general formula: n-th bit of x (right to left, starting bit position from 0) = ( x & (1 << n)
#define b0(x) (x & 1)
#define b1(x) ((x & 2) >> 1)
#define b2(x) ((x & 4) >> 2)
#define SETb0(x,v) (x = (x & (~(1))) | 1)
#define SETb1(x,v) (x = (x & (~(2))) | 2)
#define SETb2(x,v) (x = (x & (~(4))) | 4)

// get nth bit of x
// #define BIT(x,n) (x & (1<<n)) // not correct for array indexing, should return 1 or 0
#define BIT(x,n) ((x & (1<<n)) >> n)
#define SETBIT(x,n,v) (x = (x & (~(1 << n))) | (v << n))

#define COPY_CHUNK_SIZE 33554432 // 32 Mb chunk size for copy operations
#define REMOTE_IO_CHUNK_SIZE 1048576 // for upload/download

// total files and folders in a directory sub-tree
typedef struct {
	uint64_t tFiles;
	uint64_t tFolders;
} sts;

typedef struct {
	uint64_t tFiles;
	uint64_t tFolders;
	uint64_t tSize;
} sts_sz;

typedef struct {
	uint8_t flag;
	std::string file;
	uint64_t size;
} fileitem_sock_t;

#endif /* COMMON_UDS_H */
