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
    std::unique_ptr<IDescriptor> create(const STR& filename, const std::string& openMode, int& errCode) {
        WinfileDescriptor* wfd = new WinfileDescriptor(filename,openMode);
        if (wfd->hFile == INVALID_HANDLE_VALUE) errCode = errno;
        return std::unique_ptr<IDescriptor>(wfd);
    }
};

#else
#include "PosixDescriptor.h"
class FileDescriptorFactory {
public:
    std::unique_ptr<IDescriptor> create(const std::string& filename, const std::string& openMode, int& errCode) {
        PosixDescriptor* pd = new PosixDescriptor(filename,openMode);
        if (pd->desc <= 0) errCode = errno;
        return std::unique_ptr<IDescriptor>(pd);
    }
};
#endif

FileDescriptorFactory fdfactory;

#endif