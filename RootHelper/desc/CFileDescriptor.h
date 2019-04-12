#ifndef CFILEDESCRIPTOR_H
#define CFILEDESCRIPTOR_H

#include "IDescriptor.h"

class CFileDescriptor : public IDescriptor {
public:
    FILE* desc;
public:
    explicit CFileDescriptor(FILE* desc_) : desc(desc_) {}
    virtual inline ssize_t read(void* buf, size_t count) override {return fread(buf,1,count,desc);}

    virtual inline ssize_t write(const void* buf, size_t count) override {return fwrite(buf,1,count,desc);}

    virtual inline void close() override {fclose(desc);}
};

#endif /* CFILEDESCRIPTOR_H */
