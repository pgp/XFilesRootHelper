#ifndef _RH_UNIFIED_LOGGING_H_
#define _RH_UNIFIED_LOGGING_H_

#define __STDC_FORMAT_MACROS
#include <cinttypes>

#include <iostream>
#include <string>
#include <sstream>
#include <cstdarg>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <stdexcept>
#include "console_utils.h"

char LOG_TAG_WITH_SOCKET_ADDR[64]={};

typedef struct {
	uint32_t x;
} threadExitThrowable;

constexpr threadExitThrowable tEx{};

// constexpr uint64_t tEx = 9876543210L;
// exception instead of thread exit routine (ensure stack unwind and cleanup)
inline void threadExit() {
    throw tEx;
}

std::string getThreadIdAsString() {
    std::stringstream ss;
    ss<<std::this_thread::get_id();
    return ss.str();
}

std::string safestr(const char* fmt, ...) {
    int n, size=100;
    std::string str;
    va_list ap;

    for(;;) {
        str.resize(size);
        va_start(ap, fmt);
        int n = vsnprintf(&str[0], size, fmt, ap);
        va_end(ap);
        if (n > -1 && n < size) break;
        if (n > -1) size = n + 1;
        else size *= 2;
    }
    return str;
}

#ifdef _WIN32

// BEGIN WINDOWS

#include "common_win.h"

HANDLE conHandle = INVALID_HANDLE_VALUE;
// to be called once in main function
void initWindowsConsoleOutput() {
    conHandle = CreateFileA("CONOUT$",
                           GENERIC_WRITE,
                           FILE_SHARE_WRITE,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (conHandle == INVALID_HANDLE_VALUE) {
        // std::cout<<"Invalid Console handle"<<std::endl; // just like the famous "Keyboard not found, press F11 to continue"
        _Exit(-234567);
    }
}

// TODO refactor ( = safestr + WriteFile() )
void safeprintf(const char* fmt, ...) {
    int n, size=100;
    std::string str;
    va_list ap;

    for(;;) {
        str.resize(size);
        va_start(ap, fmt);
        int n = vsnprintf(&str[0], size, fmt, ap);
        va_end(ap);

        if (n > -1 && n < size) break;

        if (n > -1) size = n + 1;
        else size *= 2;
    }
    DWORD writtenBytes;
    WriteFile(conHandle,str.data(),strlen(str.data()),&writtenBytes,nullptr);
}

#define  PRINTUNIFIED(...) safeprintf(__VA_ARGS__)
#define  PRINTUNIFIEDERROR(...) safeprintf(__VA_ARGS__)

// if s contains tab characters, this doesn't work, in that a tab can occupy from 1 to 4 spaces
// a modified strlen method would be needed instead of s.size(), rounding used spaces to the next multiple of 4 when encountering a tab

// TODO this is not efficient! handle SIGWINCH instead
// using truncated size instead of strlen doesn't work well for windows
// (after end of progress, lots of empty spaces are printed before each file hash)
#define SAMELINEPRINT(...) do { \
    auto&& wh = sampleConsoleDimensions(); \
    auto&& s = safestr(__VA_ARGS__); \
    char* p = (char*)(s.c_str()); \
    if(s.size() >= wh.W) p[wh.W-1] = '\0'; \
	safeprintf("\r%*s\r", wh.W, ""); \
    DWORD writtenBytes; \
    WriteFile(conHandle,p,strlen(p),&writtenBytes,nullptr); \
} while(0)

#define EXITWITHERROR(...) do { \
	fprintf(stderr,"Exiting on error:\n"); \
	fprintf(stderr,__VA_ARGS__); \
	_Exit(0); \
} while(0)

#define EXITONSTUB do { \
	fprintf(stderr,"Feature not implemented, exiting..."); \
	_Exit(0); \
} while(0)

// END WINDOWS

#else // Linux - Android
#include <unistd.h>

#ifdef ANDROID_NDK
#include <android/log.h>
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO, LOG_TAG_WITH_SOCKET_ADDR, __VA_ARGS__)
#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG_WITH_SOCKET_ADDR, __VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG_WITH_SOCKET_ADDR, __VA_ARGS__)

#define  PRINTUNIFIED(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG_WITH_SOCKET_ADDR, __VA_ARGS__)
#define  PRINTUNIFIEDERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG_WITH_SOCKET_ADDR, __VA_ARGS__)
#define  SAMELINEPRINT(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG_WITH_SOCKET_ADDR, __VA_ARGS__)


#define EXITWITHERROR(...) do { \
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG_WITH_SOCKET_ADDR, "Exiting on error:"); \
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG_WITH_SOCKET_ADDR, __VA_ARGS__); \
	_Exit(0); \
} while(0)

#define EXITONSTUB do { \
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG_WITH_SOCKET_ADDR, "Feature not implemented, exiting..."); \
	_Exit(0); \
} while(0)


#else

/*
 * Web source:
 * https://stackoverflow.com/questions/4983092/c-equivalent-of-sprintf
 * for having a signal-safe printf
 */
// TODO refactor ( = safestr + write() )
void safefprintf(int descriptor, const char* fmt, ...) {
    int size=100;
    std::string str;
    va_list ap;

    for(;;) {
        str.resize(size);
        va_start(ap, fmt);
        int n = vsnprintf(&str[0], size, fmt, ap);
        va_end(ap);

        if (n > -1 && n < size) break;

        if (n > -1) size = n + 1;
        else size *= 2;
    }
    write(descriptor,&str[0],strlen(str.c_str())); // FIXME should this be replaced with writeAll?
}

#define  PRINTUNIFIED(...) safefprintf(STDOUT_FILENO, __VA_ARGS__)
#define  PRINTUNIFIEDERROR(...) safefprintf(STDERR_FILENO, __VA_ARGS__)

// TODO this is not efficient! handle SIGWINCH instead
#define SAMELINEPRINT(...) do { \
    auto&& wh = sampleConsoleDimensions(); \
    auto&& s = safestr(__VA_ARGS__); \
    char* p = (char*)(s.c_str()); \
    auto sz = s.size(); \
    if(sz >= wh.W) sz = wh.W-2; \
    safefprintf(STDOUT_FILENO, "\r%*s\r", wh.W, ""); \
    write(STDOUT_FILENO,p,sz); \
} while(0)

#define EXITWITHERROR(...) do { \
	 fprintf(stderr,"Exiting on error:\n"); \
	 fprintf(stderr,__VA_ARGS__); \
	 _Exit(0); \
} while(0)

#define EXITONSTUB do { \
	 fprintf(stderr,"Feature not implemented, exiting..."); \
	 _Exit(0); \
} while(0)

#endif

#endif

inline void initLogging() {
#ifdef _WIN32
    initWindowsConsoleOutput();
#endif
}

#endif /* _RH_UNIFIED_LOGGING_H_ */
