#ifndef __HOME_PATHS_H__
#define __HOME_PATHS_H__

#include <string>
#include <cstring>
#include <cstdlib>

/**
 * Home (default) paths
 */

#ifdef _WIN32

std::wstring currentXREHomePath;

#else

std::string currentHomePath;
std::string currentXREHomePath; // will be modified on XRE server start (after acceptor fork)

#endif

// called once at start
inline void initDefaultHomePaths() {
#ifdef _WIN32 // Windows
    currentXREHomePath = L"C:\\Windows\\Temp"; // TODO try with %TMP% or %TEMP%

#elif defined(ANDROID_NDK)    // Android
    const char* tmp = getenv("EXTERNAL_STORAGE");
    currentXREHomePath = currentHomePath = tmp==nullptr?"/sdcard":tmp;

#elif defined(__linux__)    // Linux
    currentXREHomePath = currentHomePath = "/tmp";

#elif defined(__APPLE__)    // OSX and iOS
    currentXREHomePath = currentHomePath = "/tmp"; // TODO try with $TMPDIR
#else                       // BSD
    currentXREHomePath = currentHomePath = "/tmp";
#endif
}

/**
 * Announced directories for XRE are in the OS-dependent headers in the xreannounce folder
 */

#ifdef _WIN32
SOCKET rhss = INVALID_SOCKET; // acceptor socket
#else
int rhss = -1; // acceptor socket (assigned only in forked process or in xre mode)
#endif

/**
 * Exposed directory for XRE
 */

#ifdef _WIN32
inline bool isXreProcess() { return rhss != INVALID_SOCKET; }
std::wstring xreExposedDirectory;
#else
inline bool isXreProcess() { return rhss != -1; }
std::string xreExposedDirectory; // currently served directory, may be empty (assigned only in forked process or in standalone xre mode)
#endif

template<typename STR>
inline int rhss_checkAccess(const STR& targetPath) {
    if(!isXreProcess()) return 0; // don't enforce restrictions for exploring filesystem locally
    return targetPath.rfind(xreExposedDirectory,0);
}

#endif /* __HOME_PATHS_H__ */