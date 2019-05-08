#ifndef __BUFFERED_WRITE_DESCRIPTOR_
#define __BUFFERED_WRITE_DESCRIPTOR_

#include <sstream>
#include "IDescriptor.h"

// write is buffered, read is unbuffered
class BufferedWriteDescriptor : public IDescriptor { // inheritance-with-composition
protected:
    IDescriptor& desc;
    std::ostringstream buffer; // use ostringstream and tellp() for autoflushing write descriptor
public:

    explicit BufferedWriteDescriptor(IDescriptor& desc_) : desc(desc_) {}

    ssize_t read(void* buf, size_t count) override { return desc.read(buf,count); };

    ssize_t write(const void* buf, size_t count) override { buffer<<std::string((char*)buf,count); return count; }

    void close() override { flush(); } // flush without close

    ssize_t readAll(void* buf, size_t count) override { return desc.readAll(buf,count); }

    void readAllOrExit(void* buf, size_t count) override { desc.readAllOrExit(buf,count); }

    ssize_t writeAll(const void* buf, size_t count) override { buffer<<std::string((char*)buf,count); return count; }

    void writeAllOrExit(const void* buf, size_t count) override { buffer<<std::string((char*)buf,count); }

    void flush() {
        auto x = buffer.str();
        if(!x.empty()) {
            desc.writeAllOrExit(x.c_str(),x.size());
            std::ostringstream tmp;
            buffer.swap(tmp);
        }
    }
};

#endif /* __BUFFERED_WRITE_DESCRIPTOR_ */