#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <cstring>

/*
 * Web source: http://www.garykessler.net/library/file_sigs.html
 */

typedef enum {
	_7Z,
	XZ,
	RAR,
	RAR5,
	ZIP,
	CAB,
	GZ,
	BZ2,
	TAR, // with offset
	UNKNOWN
} ArchiveType;

std::vector<std::string> archiveTypeLabels = {
		"7z",
		"xz",
		"rar",
		"rar5",
		"zip",
		"cab",
		"gz",
		"bz2",
		"tar",
		"unknown"
};

std::vector<std::vector<uint8_t>> headers = {
		{0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C}, // 7z
		{0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00}, // xz
		{0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00}, // rar
		{0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x01, 0x00}, // rar5
		{0x50, 0x4B, 0x03, 0x04}, // zip
		{0x4D, 0x53, 0x43, 0x46}, // cab
		{0x1F, 0x8B, 0x08}, // gz
		{0x42, 0x5A, 0x68} // bz2
};

std::vector<uint8_t> headers_lengths = {
		6,
		6,
		7,
		8,
		4,
		4,
		3,
		3
};

std::vector<uint8_t> tar_header = {0x75, 0x73, 0x74, 0x61, 0x72}; // tar
uint16_t tar_header_offset = 0x101;
uint8_t tar_header_length = 5;

// read a few bytes, tries to detect between most formats (except tar)
ArchiveType detectArchiveType(std::string archivePath) {
	ArchiveType ret = UNKNOWN;
	FILE* f = fopen(archivePath.c_str(),"rb");
	if (f == NULL) return ret;
	uint8_t h[512] = {};
	size_t readBytes = fread(h,1,512,f);
	if (readBytes < 8) {
		fclose(f);
		return ret;
	}

	int i;

	// test against offset-0 headers
	for (i=0;i<headers_lengths.size();i++) {
        uint8_t* cur = &(headers[i][0]);
		if (memcmp(h,cur,headers_lengths[i])==0)
			return (ArchiveType)i; // simply cast to enum type to get the desired enum label
	}

    if (readBytes >= tar_header_offset+tar_header_length) {
        uint8_t* cur = &(tar_header[0]);
        // test against tar header (non-zero offset)
        if (memcmp(h+tar_header_offset,cur,tar_header_length)==0)
            ret = TAR;
    }

	return ret;
}
