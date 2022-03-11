#ifndef _RH_FDFACTORY_
#define _RH_FDFACTORY_

#include "IDescriptor.h"
#include <cerrno>
#include <memory>

/**
 * Use copy list initialization over return value in order to avoid calling additional constructors
 * Web source: https://jonasdevlieghere.com/guaranteed-copy-elision/ (Addendum: Copy-List-Initialization)
 */

#ifdef _WIN32
#include "WinfileDescriptor.h"
class FileDescriptorFactory {
public:
    template<typename STR>
    WinfileDescriptor create(const STR& filename, FileOpenMode openMode) {
        return {filename,openMode};
    }

    template<typename STR>
    WinfileDescriptor* createNew(const STR& filename, FileOpenMode openMode) {
        return new WinfileDescriptor(filename,openMode);
    }
};

#else
#include "PosixDescriptor.h"
class FileDescriptorFactory {
public:
    PosixDescriptor create(const std::string& filename, FileOpenMode openMode) {
        return {filename,openMode};
    }

    PosixDescriptor* createNew(const std::string& filename, FileOpenMode openMode) {
        return new PosixDescriptor(filename,openMode);
    }
};
#endif

FileDescriptorFactory fdfactory;

#endif /* _RH_NETFACTORY_ */
