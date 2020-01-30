#ifndef _WIN32
#error "Windows required"
#endif

#ifdef _WIN32

#ifndef _WINFILE_DESCRIPTOR_H_
#define _WINFILE_DESCRIPTOR_H_

#include "IDescriptor.h"

#define EXCLUSIVE_RW_ACCESS 0

class WinfileDescriptor : public IDescriptor {
public:
    HANDLE hFile = INVALID_HANDLE_VALUE;
public:
    explicit WinfileDescriptor(HANDLE hFile_) : hFile(hFile_) {}

    WinfileDescriptor(const std::wstring& path, FileOpenMode openFormat) {
        switch(openFormat) {
            case FileOpenMode::READ:
                hFile = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,nullptr);
                break;
            case FileOpenMode::WRITE:
                //~ hFile = CreateFileW(path.c_str(),GENERIC_WRITE,EXCLUSIVE_RW_ACCESS,
                                   //~ nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,nullptr);
                hFile = CreateFileW(path.c_str(),GENERIC_WRITE,EXCLUSIVE_RW_ACCESS,
                                   nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,nullptr);
                break;
        }

        if (hFile == INVALID_HANDLE_VALUE) error = GetLastError();
    }

    WinfileDescriptor(const IDescriptor& other) = delete;
    WinfileDescriptor(const WinfileDescriptor& other) = delete;

    WinfileDescriptor(IDescriptor&& other) = delete;
    WinfileDescriptor(WinfileDescriptor&& other) = delete; /*noexcept {
        hFile = other.hFile;
        other.hFile = INVALID_HANDLE_VALUE;
    }*/

    virtual ssize_t read(void* buf, size_t count) override {
        DWORD readBytes = -1;
        if (!ReadFile(hFile,buf,count,&readBytes,nullptr)) {
            close();
        }
        return readBytes;
    }

    virtual ssize_t write(const void* buf, size_t count) override {
        DWORD writtenBytes = -1;
        if (!WriteFile(hFile,buf,count,&writtenBytes,nullptr)) {
            close();
        }
        return writtenBytes;
    }

    virtual inline void close() override {
        CloseHandle(hFile);
    }

};

#endif /* _WINFILE_DESCRIPTOR_H_ */
#endif
