#ifndef SSTREAM_DESCRIPTOR_H
#define SSTREAM_DESCRIPTOR_H

#include <sstream>
#include "IDescriptor.h"

class SstreamDescriptor : public IDescriptor {
public:
    std::stringstream ss;
    SstreamDescriptor() = default;
    inline ssize_t read(void* buf, size_t count) override {return -1;} // stringstream is write-only

    inline ssize_t write(const void* buf, size_t count) override {
        ss.write((char*)buf, count);
        return count;
    }

    inline std::string str() {
        return ss.str();
    }

    inline void close() override {}
};

#endif /* SSTREAM_DESCRIPTOR_H */
