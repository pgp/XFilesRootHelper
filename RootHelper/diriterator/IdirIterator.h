#ifndef IDIRITERATOR_H
#define IDIRITERATOR_H

#ifdef _WIN32
#include "../common_win.h"
#endif

#include <iostream>
#include <string>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <cstdlib>
#include <sstream>
#include <stack>
#include <memory>

typedef enum {
    FULL,
    RELATIVE_WITHOUT_BASE,
    RELATIVE_INCL_BASE
} IterationMode;

typedef enum {
	PLAIN,
	RECURSIVE,
	RECURSIVE_FOLLOW_SYMLINKS,
	SMART_SYMLINK_RESOLUTION
} ListingMode;

// JDBC ResultSet-style abstract class for iterating over directory trees

template<typename STR>
class IDirIterator {
protected:
    const STR dir;
    STR current; // current path from iterator (can be full, relative, relative with prepended base path)
    STR currentName; // current filename only, if enabled
    const IterationMode mode;
    const bool provideFilenames;
    //~ const bool recursiveListing;
    const ListingMode recursiveListing;
    const bool sortCurrentLevelByName;
public:
    int error = 0; // set to errno in case of directory opening error
    char currentEfd;
    virtual operator bool() {
        return error == 0;
    }

    // 0: not exists or access error
    // 1: existing file
    // 'd': existing directory
    // 'L': existing symlink to existing directory
    static char efdL(const STR& path) {
#ifdef _WIN32
        DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
        return (attrs & FILE_ATTRIBUTE_DIRECTORY)?'d':1;
#else
        struct stat st{};
        if (lstat(path.c_str(), &st) < 0) return 0; // non-existing or non-accessible
        if (S_ISLNK(st.st_mode)) {
            memset(&st,0,sizeof(struct stat));
            if (stat(path.c_str(), &st) < 0) return 0;
            if (S_ISDIR(st.st_mode)) return 'L'; // symlink to directory
            return 1; // symlink to file
        }
        else if (S_ISDIR(st.st_mode)) return 'd'; // actual directory
        else return 1;
#endif
    }

    IDirIterator(const IDirIterator& other) = delete;
    IDirIterator(IDirIterator&& other) = delete;

    // it may be better to add a default base constructor
    IDirIterator(STR dir_,
                 IterationMode mode_,
                 bool provideFilenames_ = true,
                 ListingMode recursiveListing_ = RECURSIVE_FOLLOW_SYMLINKS,
                 bool sortCurrentLevelByName_ = false
                 ) :
            dir(dir_),
            mode(mode_),
            provideFilenames(provideFilenames_),
            recursiveListing(recursiveListing_),
            sortCurrentLevelByName(sortCurrentLevelByName_) {}

    virtual ~IDirIterator() = default; // necessary, otherwise subclasses destructor won't be called (type resolution is on assigned pointer, which is of type IDirIterator)

    virtual bool next() = 0; // assigns current, and optionally currentName

    // pair members expected to be (full path, filename), since filename is shorter, order by filename
    static bool defaultPairComparator(const std::pair<STR,STR>& x, const std::pair<STR,STR>& y) {
        return x.second < y.second;
    }

    STR getCurrentFilename() {
        return currentName;
    }
    STR getCurrent() {
        return current;
    }
};

//template<typename STR>
//class IDirIteratorFactory {
//public:
//    virtual std::unique_ptr<IDirIterator<STR>> createIterator(
//            STR dir_,
//            IterationMode mode_,
//            bool provideFilenames_ = true,
//            ListingMode recursiveListing_ = RECURSIVE_FOLLOW_SYMLINKS) = 0;
//};

#endif /* IDIRITERATOR_H */
