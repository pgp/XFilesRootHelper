#ifndef RH_HASHER_H
#define RH_HASHER_H

#include "path_utils.h"
#include "botan_all.h"
#include "desc/FileDescriptorFactory.h"
#include "diriterator/IdirIterator.h"
#include "progressHook.h"

#include <unordered_set>
#include "ctype.h"

std::string toUpperCase(std::string& src) {
    std::string dest;
    std::transform(src.begin(), src.end(), std::back_inserter(dest), toupper);
    return dest;
}

template<typename T>
int vecIndexOf(const std::vector<T>& v, const T& K) {
    auto it = std::find(v.begin(), v.end(), K);
    if(it != v.end()) return it - v.begin();
    return -1;
}

constexpr uint32_t HASH_BLOCK_SIZE = 1048576;

const std::vector<uint8_t> rh_emptyHash{};
const std::vector<uint8_t> rh_errorHash{0xFF};


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
        "Blake2b(256)",
        "SHA-224",
};

const std::vector<std::string> cli_hashLabels {
        "CRC32",
        "MD5",
        "SHA1",
        "SHA256",
        "SHA384",
        "SHA512",
        "SHA3-224",
        "SHA3-256",
        "SHA3-384",
        "SHA3-512",
        "BLAKE2B-256",
        "SHA224",
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
        32,
        28
};

typedef int (*rh_qsort_comp_fn_t)(const void* p1, const void* p2);

// TODO find a more elegant way of using comparators for qsort
const rh_qsort_comp_fn_t rh_hashComparators[] = {
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,4);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,16);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,20);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,32);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,48);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,64);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,28);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,32);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,48);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,64);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,32);},
    [](const void* p1, const void* p2) {return ::memcmp(p1,p2,28);},
};

constexpr size_t rh_hash_maxAlgoIndex = sizeof(rh_hashSizes)/sizeof(size_t);

template<typename STR>
std::vector<uint8_t> rh_computeHash(const STR& filePath,
                    const uint8_t algo,
                    uint8_t* output = nullptr,
                    std::shared_ptr<Botan::HashFunction> hash1 = nullptr,
                    const bool finalizeHash = true,
                    ProgressHook* progressHook = nullptr) {

    if(!hash1)
        hash1 = Botan::HashFunction::create(rh_hashLabels[algo]);
	auto&& fd = fdfactory.create(filePath,FileOpenMode::READ);

//	PRINTUNIFIEDERROR("@@@filepath is:\t%s\n", filePath.c_str());
//	PRINTUNIFIEDERROR("@@@algorithm is:\t%s\n", algo.c_str());

	if (!fd) {
		// perror("Error opening file");
		return rh_errorHash;
	}
	std::vector<uint8_t> buffer(HASH_BLOCK_SIZE);

	ssize_t readBytes;

	for(;;) {
		readBytes = fd.read(&buffer[0],HASH_BLOCK_SIZE);
		if (readBytes < 0) {
			// perror("Read error");
			return rh_errorHash;
		}
		else if (readBytes == 0) { // end of file
			// perror("EOF");
			break;
		}
		hash1->update(&buffer[0],readBytes);
        if(progressHook != nullptr) progressHook->publishDelta(readBytes);
	}
    fd.close();
	if(finalizeHash) {
        auto result = hash1->final(); // botan secure vector
        if(output != nullptr) {
            memcpy(output,&result[0],result.size());
            return rh_emptyHash;
        }
        else return std::vector<uint8_t>(result.data(),result.data()+result.size()); // just ignore return value and release state for caller to use it in subsequent invocations;
    }
	return rh_emptyHash;
}

template<typename STR>
std::vector<uint8_t> rh_computeHash_dir(
        const STR& filePath,
        const uint8_t algo,
        uint8_t dirHashOpts,
        ProgressHook* progressHook = nullptr) {
    bool withNames = b0(dirHashOpts); // should be false by default
    bool ignoreThumbsFiles = b1(dirHashOpts); // should be true by default
    bool ignoreUnixHiddenFiles = b2(dirHashOpts); // filenames starting with '.' // should be true by default
    bool ignoreEmptyDirs = BIT(dirHashOpts,3); // parameter used only if withNames is true // should be true by default
    const std::unordered_set<STR> thumbsnames {FROMUTF("Thumbs.db"), FROMUTF(".DS_Store")};

    std::shared_ptr<Botan::HashFunction> dirHasher(Botan::HashFunction::create(rh_hashLabels[algo]));

//    const size_t hashSize = dirHasher->output_length();
    const auto& hashSize = rh_hashSizes[algo];

    auto&& it = itf.createIterator(filePath,RELATIVE_WITHOUT_BASE,true,RECURSIVE,withNames, // sorting on every level is necessary only with dir hashing filenames enabled
                                   (ignoreUnixHiddenFiles?"^[^.].+":"")); // enforce dir ordering on every listing during DFS
    if(withNames) {
        if(it) {
            while(it.next()) {
                if(it.currentEfd == 1) {
                    if(ignoreThumbsFiles && thumbsnames.count(it.getCurrentFilename()) != 0) continue;
                    if(ignoreUnixHiddenFiles && TOUTF(it.getCurrentFilename())[0] == '.') continue;
                    auto&& currentFile = pathConcat(filePath, it.getCurrent());
                    rh_computeHash(currentFile,algo,nullptr,dirHasher,false,progressHook); // open file using absolute path
                    // PRINTUNIFIEDERROR("current shared_ptr count: %d\n",dirHasher.use_count()); // should be constant, NOT increasing

                    // allows to compare filenames on windows and unix not by OS encoding (useless) but using UTF-8  as reference encoding
                    auto&& currentRelPathName = TOUNIXPATH2(it.getCurrent()); // accumulate relative pathnames into hash state
                    dirHasher->update(currentRelPathName);
                }
                else if(ignoreEmptyDirs && (it.currentEfd == '@')) continue;
                else { // include names of empty directories and non-accessible files, if withNames is enabled
                    auto&& currentRelPathName = TOUNIXPATH2(it.getCurrent()); // accumulate relative pathnames into hash state
                    dirHasher->update(currentRelPathName);
                }
            }
            auto result = dirHasher->final();
            return std::vector<uint8_t>(result.data(),result.data()+result.size());
        }
        else return rh_emptyHash;
    }
    else {
        /**
         * Content-based directory hashing: traverse the directory tree, accumulate file hashes in a vector,
         * then sort them and hash the vector itself to obtain the dir checksum.
         */
        if(it) {
            size_t CURRENTMAXSIZE = 1048576/4;
            std::vector<uint8_t> hashes(CURRENTMAXSIZE); // allow up to 256k file hashes (8M memory footrprint for 256-bit hashes) before a possible realloc
            uint32_t hashIdx = 0;
            while(it.next()) {
                if(it.currentEfd == 1) { // process only regular files, we are not taking into account filenames here
                    if(ignoreThumbsFiles && thumbsnames.count(it.getCurrentFilename()) != 0) continue;
                    if(ignoreUnixHiddenFiles && TOUTF(it.getCurrentFilename())[0] == '.') continue;
                    auto&& currentFile = pathConcat(filePath, it.getCurrent());

                    rh_computeHash(currentFile,algo,&hashes[hashIdx],dirHasher,true,progressHook); // open file using absolute path // TODO check return value
                    hashIdx += hashSize; // TODO handle realloc
                    if (hashIdx == CURRENTMAXSIZE) {
                        CURRENTMAXSIZE += hashSize;
                        hashes.resize(CURRENTMAXSIZE);
                    } // FIXME not optimal, once passed 256k files, one resize per file will happen (even if reserve is actually the heavy operation)
                    dirHasher->clear();
                }
            }

            ////////////////////////////////// sort hashes by lexicographic ordering
            qsort(&hashes[0],hashes.size()/hashSize,hashSize,rh_hashComparators[algo]);
            dirHasher->update(hashes);

            auto result = dirHasher->final();
            return std::vector<uint8_t>(result.data(),result.data()+result.size());
        }
        else return rh_emptyHash;
    }
}

// predeclaration (defined in Utils.h)
#ifdef _WIN32
int64_t osGetSize(const std::wstring& filepath, bool getDirTotalSize);
#else
int64_t osGetSize(const std::string& filepath, bool getDirTotalSize);
#endif

template<typename STR>
std::vector<uint8_t> rh_computeHash_wrapper(
        const STR& path,
        const uint8_t algo,
        const uint8_t dirHashOpts) {
    auto efd = IDirIterator<STR>::efdL(path);
    auto totalSize = osGetSize(path, true);
    auto&& progressHook = getProgressHook(totalSize);
    return (efd == 'd' || efd == 'L')?rh_computeHash_dir(path,algo,dirHashOpts,&progressHook):rh_computeHash(path,algo,nullptr,nullptr,true,&progressHook);
}

#endif /* RH_HASHER_H */
