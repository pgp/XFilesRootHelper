#ifndef RH_HASHER_H
#define RH_HASHER_H

#include "botan_all.h"
#include "desc/FileDescriptorFactory.h"

#define HASH_BLOCK_SIZE 1048576

const std::vector<uint8_t> rh_emptyHash;

template<typename STR>
std::vector<uint8_t> rh_computeHash(const STR& filePath, const std::string& algo) {

	int errnum = 0;
	std::unique_ptr<Botan::HashFunction> hash1(Botan::HashFunction::create(algo));
	std::unique_ptr<IDescriptor> fd = fdfactory.create(filePath,"rb",errnum);

//	PRINTUNIFIEDERROR("@@@filepath is:\t%s\n", filePath.c_str());
//	PRINTUNIFIEDERROR("@@@algorithm is:\t%s\n", algo.c_str());

	// return value and errno
	if (errnum < 0) {
		//~ printf("@@@Error opening file, errno is %d\n",errnum);
		return rh_emptyHash;
	}
	std::vector<uint8_t> buffer(HASH_BLOCK_SIZE);

	ssize_t readBytes;

	for(;;) {
		readBytes = fd->read(&buffer[0],HASH_BLOCK_SIZE);
		if (readBytes < 0) {
			//~ printf("@@@Read error\n");
			return rh_emptyHash;
		}
		else if (readBytes == 0) { // end of file
			//~ printf("@@@EOF\n");
			break;
		}
		hash1->update(&buffer[0],readBytes);
	}
	auto result = hash1->final(); // botan secure vector
	fd->close();
	return std::vector<uint8_t>(result.data(),result.data()+result.size());
}
    
    // Botan-compatible labels
	const std::vector<std::string> rh_hashLabels {
		"CRC32",
		"MD5",
		"SHA-1",
		"SHA-256",
		"SHA-384",
		"SHA-512",
		"SHA-3(224)",
		"SHA-3(256)",
		"SHA-3(384)",
		"SHA-3(512)",
		"Blake2b(256)"
	};
	
	// only needed for remote hash computation
	const std::vector<size_t> rh_hashSizes {
		4,
		16,
		20,
		32,
		48,
		64,
		28,
		32,
		48,
		64,
		32
	};
	
	const size_t rh_hash_maxAlgoIndex = rh_hashLabels.size();

#endif /* RH_HASHER_H */
