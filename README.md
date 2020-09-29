# XFilesRootHelper
The native library/executable file operations helper for XFiles, which can also be used as standalone file server from other platforms

[![Build Status](https://travis-ci.com/pgp/XFilesRootHelper.svg?branch=master)](https://travis-ci.com/pgp/XFilesRootHelper) 
[![Cirrus Build Status](https://api.cirrus-ci.com/github/pgp/XFilesRootHelper.svg)](https://cirrus-ci.com/github/pgp/XFilesRootHelper)

Precompiled binaries are available for Windows 7+ (x64) and Linux (x64) in the [Release](https://github.com/pgp/XFilesRootHelper/releases) section. If you plan to build from source instead, just continue reading.

## Build instructions (step-by-step)
**Android**
- Follow instructions described in main [XFiles](https://github.com/pgp/XFiles) repository's README in order to generate the executable library needed for the Android app

**Linux**
- Install gcc toolchain, cmake and X11 libs (e.g. from Ubuntu/Mint):
    ```shell
    sudo apt install build-essential cmake libx11-dev
    ```
- From a bash shell:
    ```shell
    export CC=gcc
    export CXX=g++
    cd CMAKE
    cmake -H. -Bbuild
    cmake --build build -- -j2
    ```

**Linux - universal binary**
- Check [this guide](BUILDING_UNIVERSAL_LINUX.md) for more information

**MacOS**

- Install [XQuartz](https://www.xquartz.org/)
- Install [HomeBrew](https://brew.sh/)
- Install gcc and cmake from HomeBrew:
    ```shell
    brew install gcc cmake
    ```
- From a bash shell:
    ```shell
    export CC=gcc-9
    export CXX=g++-9
    cd CMAKE
    cmake -H. -Bbuild
    cmake --build build -- -j2
    ```

**Windows (MinGW)**
- Install [MingW](https://nuwen.net/mingw.html)
- (Optional) Add c:\MinGW\bin to PATH
- Install [CMake](https://cmake.org/download)
- Add CMake to system PATH from installation GUI
- From a command prompt:
    ```bat
    set CC=c:\MinGW\bin\gcc.exe
    set CXX=c:\MinGW\bin\g++.exe
    cd CMAKE
    cmake -G "MinGW Makefiles" -DCMAKE_SH="CMAKE_SH-NOTFOUND" -H. -Bwinbuild
    cmake --build winbuild -- -j2
    ```

**Windows (MSVC)**
- Install [Visual Studio](https://visualstudio.microsoft.com/downloads/) (Build Tools at least)
- Install [CMake](https://cmake.org/download)
- Add CMake to system PATH from installation GUI
- From a command prompt:
    ```bat
    cd CMAKE
    cmake -G "Visual Studio 16 2019" -A x64 -H. -Bmsvcbuild
    cmake --build msvcbuild --config Release --
    ```

**BSD (e.g. FreeBSD, TrueOS)**
- Install latest gcc and cmake:
    ```shell
    sudo pkg install gcc9 cmake
    ```
- From a bash shell:
    ```shell
    export CC=gcc9
    export CXX=g++9
    cd CMAKE
    cmake -H. -Bbuild
    cmake --build build -- -j2
    ```

**iOS (for jailbroken devices only, not really useful, just for testing purposes)**
- This works only from a MacOS host with XCode + developer tools installed
- Install HomeBrew and CMake as described in the MacOS section
- In CMakeLists, set the iOS SDK path (SYSROOT), download legacy SDKs from [here](https://github.com/EachAndOther/Legacy-iOS-SDKs) if needed
- From a bash shell:
    ```shell
    cd CMAKE_ios
    cmake -H. -Bbuild
    cmake --build build -- -j2
    ```
- Generated binaries are in cmakebin folder
