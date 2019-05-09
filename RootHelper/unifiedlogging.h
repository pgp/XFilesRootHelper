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
        std::cout<<"Invalid Console handle"<<std::endl; // actually nonsense
        _Exit(-234567);
    }
}

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

#define EXITWITHERROR(...) do { \
	fprintf(stderr,"Exiting on error:\n"); \
	fprintf(stderr,__VA_ARGS__); \
	exit(0); \
} while(0)

#define EXITONSTUB do { \
	fprintf(stderr,"Feature not implemented, exiting..."); \
	exit(0); \
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


#define EXITWITHERROR(...) do { \
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG_WITH_SOCKET_ADDR, "Exiting on error:"); \
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG_WITH_SOCKET_ADDR, __VA_ARGS__); \
	exit(0); \
} while(0)

#define EXITONSTUB do { \
	__android_log_print(ANDROID_LOG_ERROR, LOG_TAG_WITH_SOCKET_ADDR, "Feature not implemented, exiting..."); \
	exit(0); \
} while(0)


#else

/*
 * Web source:
 * https://stackoverflow.com/questions/4983092/c-equivalent-of-sprintf
 * for having a signal-safe printf
 */
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
    write(descriptor,&str[0],strlen(str.c_str())); // FIXME should replaced with writeAll
}

#define  PRINTUNIFIED(...) safefprintf(STDOUT_FILENO, __VA_ARGS__)
#define  PRINTUNIFIEDERROR(...) safefprintf(STDERR_FILENO, __VA_ARGS__)

#define EXITWITHERROR(...) do { \
	 fprintf(stderr,"Exiting on error:\n"); \
	 fprintf(stderr,__VA_ARGS__); \
	 exit(0); \
} while(0)

#define EXITONSTUB do { \
	 fprintf(stderr,"Feature not implemented, exiting..."); \
	 exit(0); \
} while(0)

#endif

#endif

inline void initLogging() {
#ifdef _WIN32
    initWindowsConsoleOutput();
#endif
}

#endif /* _RH_UNIFIED_LOGGING_H_ */
