#ifndef IDESCRIPTOR_H
#define IDESCRIPTOR_H

#include <cstdio>

#ifdef _WIN32
#include "../common_win.h"
#else
#include <unistd.h>
#include <dirent.h> // MSVC
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/types.h>
#include <fcntl.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "../unifiedlogging.h"

// both binary
enum class FileOpenMode {
    READ, // "rb"
    WRITE // "wb"
};

// interface, only pure virtual functions
class IDescriptor {
public:
    int error = 0; // indicates constructor failure in the subclasses, contains errno in that case

    virtual operator bool() {
        return error == 0;
    }

    virtual ssize_t read(void* buf, size_t count) = 0;
    virtual ssize_t write(const void* buf, size_t count) = 0;
    virtual void close() = 0;

    // defaults to close(), PosixDescriptor uses ::shutdown syscall instead
    virtual void shutdown() {close();}

    virtual ssize_t readAll(void* buf_, size_t count) {
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

    virtual void readAllOrExit(void* buf, size_t count) {
        ssize_t readBytes = readAll(buf,count);
        ssize_t count_ = count;
        if (readBytes < count_) {
            PRINTUNIFIEDERROR("Exiting thread %s on read error\n",getThreadIdAsString().c_str());
            close();
            threadExit();
        }
    }

    virtual ssize_t writeAll(const void* buf_, size_t count) {
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

    virtual void writeAllOrExit(const void* buf, size_t count) {
        ssize_t writtenBytes = writeAll(buf,count);
        ssize_t count_ = count;
        if (writtenBytes < count_) {
            PRINTUNIFIEDERROR("Exiting thread %s on write error\n",getThreadIdAsString().c_str());
            close();
            threadExit();
        }
    }

    virtual ~IDescriptor() = default;
};

#endif /* IDESCRIPTOR_H */
