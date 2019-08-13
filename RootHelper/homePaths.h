#ifndef __HOME_PATHS_H__
#define __HOME_PATHS_H__

#include <string>

#ifdef _WIN32

std::wstring currentXREHomePath;

#else

std::string currentHomePath;
std::string currentXREHomePath; // will be modified on XRE server start (after acceptor fork)

#endif

// called once at start
inline void initDefaultHomePaths() {
#ifdef _WIN32 // Windows
    currentXREHomePath = w"C:\\Windows\\Temp"; // TODO try with %TMP% or %TEMP%

#elif defined(ANDROID_NDK)    // Android
    currentXREHomePath = currentHomePath = "/sdcard";

#elif defined(__linux__)    // Linux
    currentXREHomePath = currentHomePath = "/tmp";

#elif defined(__APPLE__)    // OSX and iOS
    currentXREHomePath = currentHomePath = "/tmp"; // TODO try with $TMPDIR
#else                       // BSD
    currentXREHomePath = currentHomePath = "/tmp";
#endif
}


#endif /* __HOME_PATHS_H__ */