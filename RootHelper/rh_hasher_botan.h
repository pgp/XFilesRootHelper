#ifndef RH_HASHER_H
#define RH_HASHER_H

#include "path_utils.h"
#include "botan_all.h"
#include "desc/FileDescriptorFactory.h"
#include "diriterator/IdirIterator.h"

#include <unordered_set>

constexpr uint32_t HASH_BLOCK_SIZE = 1048576;

const std::vector<uint8_t> rh_emptyHash;

template<typename STR>
std::vector<uint8_t> rh_computeHash(const STR& filePath, const std::string& algo, std::shared_ptr<Botan::HashFunction> hash1 = nullptr) {

    bool externalState = !!hash1;
    if(!externalState)
        hash1 = Botan::HashFunction::create(algo);
	auto&& fd = fdfactory.create(filePath,"rb");

//	PRINTUNIFIEDERROR("@@@filepath is:\t%s\n", filePath.c_str());
//	PRINTUNIFIEDERROR("@@@algorithm is:\t%s\n", algo.c_str());

	// return value and errno
	if (!fd) {
		//~ printf("@@@Error opening file, errno is %d\n",errnum);
		return rh_emptyHash;
	}
	std::vector<uint8_t> buffer(HASH_BLOCK_SIZE);

	ssize_t readBytes;

	for(;;) {
		readBytes = fd.read(&buffer[0],HASH_BLOCK_SIZE);
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
    fd.close();
	if(!externalState) {
        auto result = hash1->final(); // botan secure vector
        return std::vector<uint8_t>(result.data(),result.data()+result.size());
    }
	else return {}; // just ignore return value and release state for caller to use it in subsequent invocations
}

template<typename STR>
std::vector<uint8_t> rh_computeHash_dir(
        const STR& filePath,
        const std::string& algo,
        uint8_t dirHashOpts) {
    bool withNames = b0(dirHashOpts); // should be false by default
    bool ignoreThumbsFiles = b1(dirHashOpts); // should be true by default
    bool ignoreUnixHiddenFiles = b2(dirHashOpts); // filenames starting with '.' // should be true by default
    bool ignoreEmptyDirs = BIT(dirHashOpts,3); // parameter used only if withNames is true // should be true by default // TODO to be implemented
    const std::unordered_set<STR> thumbsnames {FROMUTF("Thumbs.db"), FROMUTF(".DS_Store")};

    std::shared_ptr<Botan::HashFunction> dirHasher(Botan::HashFunction::create(algo));
    auto&& it = itf.createIterator(filePath,RELATIVE_WITHOUT_BASE,true,RECURSIVE,true,
                                   (ignoreUnixHiddenFiles?"^[^.].+":"")); // enforce dir ordering on every listing during DFS // TODO check regex
    if(it) {
        while(it.next()) {
            if(it.currentEfd == 1) {
                if(ignoreThumbsFiles && thumbsnames.count(it.getCurrentFilename()) != 0) continue;
                if(ignoreUnixHiddenFiles && TOUTF(it.getCurrentFilename())[0] == '.') continue;
                auto&& currentFile = pathConcat(filePath, it.getCurrent());
                rh_computeHash(currentFile,algo,dirHasher); // open file using absolute path
                // PRINTUNIFIEDERROR("current shared_ptr count: %d\n",dirHasher.use_count()); // should be constant, NOT increasing

                if(withNames) {
                    // allows to compare filenames on windows and unix not by OS encoding (useless) but using UTF-8  as reference encoding
                    auto&& currentRelPathName = TOUNIXPATH(it.getCurrent()); // accumulate relative pathnames into hash state
                    dirHasher->update(currentRelPathName);
                }
            }
            else if(withNames) { // include names of empty directories and non-accessible files, if withNames is enabled
                auto&& currentRelPathName = TOUNIXPATH(it.getCurrent()); // accumulate relative pathnames into hash state
                dirHasher->update(currentRelPathName);
            }
        }
        auto result = dirHasher->final();
        return std::vector<uint8_t>(result.data(),result.data()+result.size());
    }
    else return rh_emptyHash;
}

template<typename STR>
std::vector<uint8_t> rh_computeHash_wrapper(
        const STR& path,
        const std::string& algo,
        uint8_t dirHashOpts) {
    auto efd = IDirIterator<STR>::efdL(path);
    return (efd == 'd' || efd == 'L')?rh_computeHash_dir(path,algo,dirHashOpts):rh_computeHash(path,algo);
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
constexpr size_t rh_hashSizes[] {
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

constexpr size_t rh_hash_maxAlgoIndex = sizeof(rh_hashSizes)/sizeof(size_t);

#endif /* RH_HASHER_H */
