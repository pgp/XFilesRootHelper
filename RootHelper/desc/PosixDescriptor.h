#ifdef _WIN32
#error "POSIX required"
#endif

#ifndef _WIN32

#ifndef POSIXDESCRIPTOR_H
#define POSIXDESCRIPTOR_H

#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/types.h>
#include <fcntl.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include "../unifiedlogging.h"
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
        else desc = -1;
    }

    explicit PosixDescriptor(int desc_) : desc(desc_) {}
    inline ssize_t read(void* buf, size_t count) {return ::read(desc,buf,count);}

    ssize_t readAll(void* buf_, size_t count) {
        auto buf = (uint8_t*)buf_;
        size_t alreadyRead = 0;
        size_t remaining = count;
        for(;;) {
            ssize_t curr = read(buf+alreadyRead,remaining);
            if (curr <= 0) return curr; // EOF

            remaining -= curr;
            alreadyRead += curr;

            if (remaining == 0) return count; // all expected bytes read
        }
    }

    void readAllOrExit(void* buf, size_t count) {
        ssize_t readBytes = readAll(buf,count);
        ssize_t count_ = count;
        if (readBytes < count_) {
            //~ PRINTUNIFIED("Exiting thread %d on read error\n",syscall(SYS_gettid));
            PRINTUNIFIED("Exiting thread %ld on read error\n",pthread_self());
            // close(fd);
            threadExit();
        }
    }

    inline ssize_t write(const void* buf, size_t count) {return ::write(desc,buf,count);}

    ssize_t writeAll(const void* buf_, size_t count) {
        auto buf = (uint8_t*)buf_;
        size_t alreadyWritten = 0;
        size_t remaining = count;
        for(;;) {
            ssize_t curr = write(buf+alreadyWritten,remaining);
            if (curr <= 0) return curr; // EOF

            remaining -= curr;
            alreadyWritten += curr;

            if (remaining == 0) return count; // all expected bytes written
        }
    }

    void writeAllOrExit(const void* buf, size_t count) {
        ssize_t writtenBytes = writeAll(buf,count);
        ssize_t count_ = count;
        if (writtenBytes < count_) {
            //~ PRINTUNIFIED("Exiting thread %d on write error\n",syscall(SYS_gettid));
            PRINTUNIFIED("Exiting thread %ld on write error\n",pthread_self());
            // close(fd);
            threadExit();
        }
    }

    inline void close() {::close(desc);}
};

#endif /* POSIXDESCRIPTOR */

#endif
