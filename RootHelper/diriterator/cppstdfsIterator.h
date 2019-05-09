#ifndef CPPSTDFSITERATOR_H
#define CPPSTDFSITERATOR_H

#include <fstream>
#include <iostream>
#include <string>
#include <experimental/filesystem>
#include "IdirIterator.h"

namespace fs = std::experimental::filesystem;

class cppstdfsIterator : public IDirIterator<std::string> {
private:
    size_t rootLen;
    fs::recursive_directory_iterator* it;
    fs::recursive_directory_iterator end;

    fs::directory_iterator* plainIt;
    fs::directory_iterator plainEnd;

    std::string getParentDir(std::string child) {
        size_t last = child.find_last_of("/");
        return child.substr(0,last);
    }
public:
    cppstdfsIterator(std::string& dir_,
                     IterationMode mode_,
                     bool provideFilenames_ = true,
                     ListingMode recursiveListing_ = RECURSIVE_FOLLOW_SYMLINKS) :
    IDirIterator(dir_,mode_,provideFilenames_,recursiveListing_) {

        std::string parentDir;
        switch (mode) {
            case RELATIVE_INCL_BASE:
                parentDir = getParentDir(dir);
                rootLen = parentDir.length() + 1;
                break;
            case RELATIVE_WITHOUT_BASE:
                rootLen = dir.length() + 1;
                break;
            case FULL:
                rootLen = 0;
                break;
            default:
                std::cerr<<"invalid enum value for dir iteration mode\n";
                error = errno = EINVAL;
                return;
        }

		// TODO discriminate between RECURSIVE and RECURSIVE_FOLLOW_SYMLINKS
        if (recursiveListing == RECURSIVE || recursiveListing == RECURSIVE_FOLLOW_SYMLINKS)
            it = new fs::recursive_directory_iterator(fs::path(dir),fs::directory_options::skip_permission_denied);
        else
            plainIt = new fs::directory_iterator(fs::path(dir),fs::directory_options::skip_permission_denied);
    }
    
    ~cppstdfsIterator() {
        if (recursiveListing == RECURSIVE || recursiveListing == RECURSIVE_FOLLOW_SYMLINKS)
			delete it;
        else
			delete plainIt;
    }

    bool next() {
        if (recursiveListing == RECURSIVE || recursiveListing == RECURSIVE_FOLLOW_SYMLINKS) {
            if ((*it) == end) return false;
            current = (*it)->path();
            if (mode != FULL) current = current.substr(rootLen);
            if (provideFilenames) currentName = (*it)->path().filename();
            (*it)++;
            return true;
        }
        else {
            if ((*plainIt) == plainEnd) return false;
            current = (*plainIt)->path();
            if (mode != FULL) current = current.substr(rootLen);
            if (provideFilenames) currentName = (*plainIt)->path().filename();
            (*plainIt)++;
            return true;
        }
    }
};

class cppstdfsIteratorFactory {
public:
    cppstdfsIterator createIterator(std::string dir_,
                                    IterationMode mode_,
                                    bool provideFilenames_ = true,
                                    ListingMode recursiveListing_ = RECURSIVE_FOLLOW_SYMLINKS) {
        return {dir_,mode_,provideFilenames_,recursiveListing_};
    }
};

#endif /* CPPSTDFSITERATOR_H */
