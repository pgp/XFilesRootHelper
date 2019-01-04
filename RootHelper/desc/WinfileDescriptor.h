#ifndef _WIN32
#error "Windows required"
#endif

#ifdef _WIN32

#ifndef _WINFILE_DESCRIPTOR_H_
#define _WINFILE_DESCRIPTOR_H_

#include <string>
#include "../unifiedlogging.h"

#define EXCLUSIVE_RW_ACCESS 0

class WinfileDescriptor : public IDescriptor {
public:
    HANDLE hFile = INVALID_HANDLE_VALUE;
public:
    explicit WinfileDescriptor(HANDLE hFile_) : hFile(hFile_) {}

    WinfileDescriptor(const std::wstring& path, const std::string& openFormat) {
        if (openFormat == "rb") {
            hFile = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,nullptr);
        }
        else if (openFormat == "wb") {
            //~ hFile = CreateFileW(path.c_str(),GENERIC_WRITE,EXCLUSIVE_RW_ACCESS,
                                   //~ nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,nullptr);
            hFile = CreateFileW(path.c_str(),GENERIC_WRITE,EXCLUSIVE_RW_ACCESS,
                                   nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,nullptr);
        }
    }

    ssize_t read(void* buf, size_t count) {
        DWORD readBytes = -1;
        if (!ReadFile(hFile,buf,count,&readBytes,nullptr)) {
            close();
        }
        return readBytes;
    }

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
            fprintf(stderr,"Exiting thread %ld on read error\n",std::this_thread::get_id());
            close();
            threadExit();
        }
    }

    ssize_t write(const void* buf, size_t count) {
        DWORD writtenBytes = -1;
        if (!WriteFile(hFile,buf,count,&writtenBytes,nullptr)) {
            close();
        }
        return writtenBytes;
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
            PRINTUNIFIEDERROR("Exiting thread %ld on write error\n",std::this_thread::get_id());
            close();
            threadExit();
        }
    }

    inline void close() {
        CloseHandle(hFile);
    }

};

#endif /* _WINFILE_DESCRIPTOR_H_ */
#endif
