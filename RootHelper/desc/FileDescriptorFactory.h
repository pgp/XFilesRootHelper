#ifndef _RH_FDFACTORY_
#define _RH_FDFACTORY_

#include "IDescriptor.h"
#include <string>
#include <cerrno>
#include <memory>

#ifdef _WIN32
#include "WinfileDescriptor.h"
class FileDescriptorFactory {
public:
    template<typename STR>
    WinfileDescriptor create(const STR& filename, const std::string& openMode, int& errCode) {
        WinfileDescriptor wfd(filename,openMode);
        if (wfd.hFile == INVALID_HANDLE_VALUE) errCode = errno;
        return wfd;
    }
};

#else
#include "PosixDescriptor.h"
class FileDescriptorFactory {
public:
    PosixDescriptor create(const std::string& filename, const std::string& openMode, int& errCode) {
        PosixDescriptor pd(filename,openMode);
        if (pd.desc <= 0) errCode = errno;
        return pd;
    }
};
#endif

FileDescriptorFactory fdfactory;

#endif
