#ifndef _WIN32
#error "Windows required"
#endif

#ifdef _WIN32

#ifndef _WINSOCK_DESCRIPTOR_H_
#define _WINSOCK_DESCRIPTOR_H_

#include <cstdlib>
#include <thread>
#include "../common_win.h"
#include "IDescriptor.h"

class WinsockDescriptor : public IDescriptor {
public:
    SOCKET desc;
public:
    explicit WinsockDescriptor(SOCKET desc_) : desc(desc_) {}

    inline ssize_t read(void* buf, size_t count) {
        return recv(desc,(char*)buf,count,MSG_NOSIGNAL);
    }
    
    inline ssize_t readAll(void* buf, size_t count) {
        return recv(desc,(char*)buf,count,MSG_WAITALL);
    }

    void readAllOrExit(void* buf, size_t count) {
        ssize_t readBytes = readAll(buf,count);
        ssize_t count_ = count;
        if (readBytes < count_) {
            fprintf(stderr,"Exiting thread %ld on read error\n",std::this_thread::get_id());
            // close();
            threadExit();
        }
    }

    inline ssize_t write(const void* buf, size_t count) {
        return send(desc,(const char*)buf,count,0);
    }

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
            fprintf(stderr,"Exiting thread %ld on write error\n",std::this_thread::get_id());
            // close();
            threadExit();
        }
    }

    inline void close() {closesocket(desc);}
};

#endif /* _WINSOCK_DESCRIPTOR_H_ */

#endif
