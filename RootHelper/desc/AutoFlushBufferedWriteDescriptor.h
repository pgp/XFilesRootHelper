#ifndef __AUTOFLUSH_BUFFERED_WRITE_DESCRIPTOR_
#define __AUTOFLUSH_BUFFERED_WRITE_DESCRIPTOR_

#include <sstream>
#include "BufferedWriteDescriptor.h"

// write is buffered, read is unbuffered
class AutoFlushBufferedWriteDescriptor : public BufferedWriteDescriptor {
private:
    const int flushSize;
public:

    explicit AutoFlushBufferedWriteDescriptor(IDescriptor& desc_, int flushSize_ = 8192) :
            BufferedWriteDescriptor(desc_), flushSize(flushSize_) {}

    ssize_t write(const void* buf, size_t count) override {
        buffer<<std::string((char*)buf,count);
        if(buffer.tellp() >= flushSize) flush();
        return count;
    }

    inline ssize_t writeAll(const void* buf, size_t count) override {
        return write(buf,count);
    }

    inline void writeAllOrExit(const void* buf, size_t count) override {
        write(buf,count);
    }
};

#endif /* __AUTOFLUSH_BUFFERED_WRITE_DESCRIPTOR_ */