#ifndef _WIN32
#error "Windows required"
#endif

#ifdef _WIN32

#ifndef _WINSOCK_DESCRIPTOR_H_
#define _WINSOCK_DESCRIPTOR_H_

#include "IDescriptor.h"

class WinsockDescriptor : public IDescriptor {
public:
    SOCKET desc;
public:
    WinsockDescriptor(SOCKET desc_) : desc(desc_) {}

    operator bool() override {
        return error == 0 && desc != INVALID_SOCKET;
    }

    virtual inline ssize_t read(void* buf, size_t count) override {
        return recv(desc,(char*)buf,count,MSG_NOSIGNAL);
    }

    virtual inline ssize_t readAll(void* buf, size_t count) override {
        return recv(desc,(char*)buf,count,MSG_WAITALL);
    }

    virtual inline ssize_t write(const void* buf, size_t count) override {
        return send(desc,(const char*)buf,count,0);
    }

    virtual inline void close() override {closesocket(desc);}

    virtual inline void shutdown() override {::shutdown(desc, SD_BOTH);}
};

#endif /* _WINSOCK_DESCRIPTOR_H_ */

#endif
