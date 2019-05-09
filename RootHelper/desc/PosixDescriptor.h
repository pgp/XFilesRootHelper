#ifdef _WIN32
#error "POSIX required"
#endif

#ifndef _WIN32

#ifndef POSIXDESCRIPTOR_H
#define POSIXDESCRIPTOR_H

#include "IDescriptor.h"

class PosixDescriptor : public IDescriptor {
public:
    int desc;
public:

    // OK with std::string, won't work with std::wstring, just to try templates
    template <typename STR>
    PosixDescriptor(const STR& path, const std::string& openFormat) {
        if (openFormat == "rb") {
            desc = ::open(path.c_str(), O_RDONLY);
        }
        else if (openFormat == "wb") {
            //~ desc = ::open(path.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0644);
            desc = ::open(path.c_str(), O_WRONLY|O_CREAT|O_EXCL, 0644);
        }
        else {
            desc = -1;
            error = errno = EINVAL;
        }
    }

    explicit PosixDescriptor(int desc_) : desc(desc_) {}

    PosixDescriptor(const IDescriptor& other) = delete;
    PosixDescriptor(const PosixDescriptor& other) = delete;

    PosixDescriptor(IDescriptor&& other) = delete;
    PosixDescriptor(PosixDescriptor&& other) = delete; /*noexcept {
        desc = other.desc;
        other.desc = -1;
    }*/

    virtual inline ssize_t read(void* buf, size_t count) override {return ::read(desc,buf,count);}

    virtual inline ssize_t write(const void* buf, size_t count) override {return ::write(desc,buf,count);}

    virtual inline void close() override {::close(desc);}
};

#endif /* POSIXDESCRIPTOR */

#endif
