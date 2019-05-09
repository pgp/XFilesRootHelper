#ifndef IDIRITERATOR_H
#define IDIRITERATOR_H

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
public:
    int error = 0; // set to errno in case of directory opening error

    virtual operator bool() {
        return error == 0;
    }

    IDirIterator(const IDirIterator& other) = delete;
    IDirIterator(IDirIterator&& other) = delete;

    // it may be better to add a default base constructor
    IDirIterator(STR dir_,
                 IterationMode mode_,
                 bool provideFilenames_ = true,
                 ListingMode recursiveListing_ = RECURSIVE_FOLLOW_SYMLINKS) :
            dir(dir_),
            mode(mode_),
            provideFilenames(provideFilenames_),
            recursiveListing(recursiveListing_) {}

    virtual ~IDirIterator() = default; // necessary, otherwise subclasses destructor won't be called (type resolution is on assigned pointer, which is of type IDirIterator)

    virtual bool next() = 0; // assigns current, and optionally currentName

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
