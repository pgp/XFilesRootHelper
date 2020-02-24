#ifndef SINK_DESCRIPTOR_H
#define SINK_DESCRIPTOR_H

#include "IDescriptor.h"

class SinkDescriptor : public IDescriptor {
public:
    SinkDescriptor() = default;
    inline ssize_t read(void* buf, size_t count) override {return count;}

    inline ssize_t write(const void* buf, size_t count) override {return count;}

    inline void close() override {}
};

#endif /* SINK_DESCRIPTOR_H */
