#ifndef __RH_ARCHIVE_TYPE_DETECTOR_H__
#define __RH_ARCHIVE_TYPE_DETECTOR_H__

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

const std::unordered_map<std::string,ArchiveType> archiveExtsToTypes {
        {"7z",_7Z},
        {"xz",XZ},
        {"rar",RAR5},
        {"zip",ZIP},
        {"cab",CAB},
        {"gz",GZ},
        {"bz2",BZ2},
        {"tar",TAR}
};

const std::vector<std::vector<uint8_t>> rh_archive_headers {
		{0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C}, // 7z
		{0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00}, // xz
		{0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00}, // rar
		{0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x01, 0x00}, // rar5
		{0x50, 0x4B, 0x03, 0x04}, // zip
		{0x4D, 0x53, 0x43, 0x46}, // cab
		{0x1F, 0x8B, 0x08}, // gz
		{0x42, 0x5A, 0x68} // bz2
};

constexpr uint8_t headers_lengths[] {
		6,
		6,
		7,
		8,
		4,
		4,
		3,
		3
};

constexpr uint8_t tar_header[] {0x75, 0x73, 0x74, 0x61, 0x72}; // tar
const uint16_t tar_header_offset = 0x101;
const uint8_t tar_header_length = 5;

ArchiveType archiveTypeFromExtension(const std::string& ext) {
    try {
        return archiveExtsToTypes.at(ext);
    }
    catch(const std::out_of_range& e) {
        return UNKNOWN;
    }
}

// read a few bytes, tries to detect between most formats (except tar)
ArchiveType detectArchiveType(const std::string& archivePath) {
	ArchiveType ret = UNKNOWN;
	FILE* f = fopen(archivePath.c_str(),"rb");
	if (f == nullptr) return ret;
	uint8_t h[512]{};
	size_t readBytes = fread(h,1,512,f);
	fclose(f);
	if (readBytes < 8) {
		return ret;
	}

	int i;

	// test against offset-0 headers
	for (i=0;i<(sizeof(headers_lengths)/(sizeof(uint8_t)));i++) {
        const uint8_t* cur = &(rh_archive_headers[i][0]);
		if (memcmp(h,cur,headers_lengths[i])==0)
			return (ArchiveType)i; // simply cast to enum type to get the desired enum label
	}

    if (readBytes >= tar_header_offset+tar_header_length) {
        // test against tar header (non-zero offset)
        if (memcmp(h+tar_header_offset,tar_header,tar_header_length)==0)
            ret = TAR;
    }

	return ret;
}

#endif /* __RH_ARCHIVE_TYPE_DETECTOR_H__ */