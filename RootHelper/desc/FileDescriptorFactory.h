#ifndef _RH_FDFACTORY_
#define _RH_FDFACTORY_

#include "IDescriptor.h"
#include <string>
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
    WinfileDescriptor create(const STR& filename, const std::string& openMode) {
        return {filename,openMode};
    }
};

#else
#include "PosixDescriptor.h"
class FileDescriptorFactory {
public:
    PosixDescriptor create(const std::string& filename, const std::string& openMode) {
        return {filename,openMode};
    }
};
#endif

FileDescriptorFactory fdfactory;

#endif
