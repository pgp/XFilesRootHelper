#ifndef CFILEDESCRIPTOR_H
#define CFILEDESCRIPTOR_H

#include <cstdio>
#include <cstdlib>
#include <thread>
#include "../unifiedlogging.h"
#include "IDescriptor.h"

class CFileDescriptor : public IDescriptor {
public:
    FILE* desc;
public:
    explicit CFileDescriptor(FILE* desc_) : desc(desc_) {}
    inline ssize_t read(void* buf, size_t count) {return fread(buf,1,count,desc);}

    ssize_t readAll(void* buf_, size_t count) {
        uint8_t* buf = (uint8_t*)buf_;
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
            PRINTUNIFIED("Exiting thread %ld on read error\n",pthread_self());
            // close(fd);
            threadExit();
        }
    }

    inline ssize_t write(const void* buf, size_t count) {return fwrite(buf,1,count,desc);}

    ssize_t writeAll(const void* buf_, size_t count) {
        uint8_t* buf = (uint8_t*)buf_;
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
            PRINTUNIFIED("Exiting thread %ld on write error\n",pthread_self());
            // close(fd);
            threadExit();
        }
    }

    inline void close() {fclose(desc);}
};

#endif /* CFILEDESCRIPTOR_H */
