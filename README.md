# XFilesRootHelper
The native library/executable file operations helper for XFiles, which can also be used as standalone file server from other platforms

# Build instructions (step-by-step)
**Android**
- Follow instructions described in main [XFiles](https://github.com/pgp/XFiles) repository's README in order to generate the executable library needed for the Android app

**Linux**
- Install gcc toolchain, cmake and X11 libs (e.g. from Ubuntu/Mint):
    ```shell
    sudo apt install build-essential cmake libx11-dev
    ```
- From a bash shell:
    ```shell
    cd CMAKE
    cmake -H. -Bbuild
    cmake --build build -- -j2
    ```

**MacOS**

- Install [XQuartz](https://www.xquartz.org/)
- Install [HomeBrew](https://brew.sh/)
- Install gcc and cmake from HomeBrew:
    ```shell
    brew install gcc cmake
    ```
- From a bash shell:
    ```shell
    export CC=$(which gcc-8)
    export CXX=$(which g++-8)
    cd CMAKE
    cmake -H. -Bbuild
    cmake --build build -- -j2
    ```

**Windows**
- Install [MingW](https://nuwen.net/mingw.html)
- (Optional) Add c:\MinGW\bin to PATH
- Install [CMake](https://cmake.org/download)
- Add CMake to system PATH from installation GUI
- From a command prompt:
    ```bat
    set CC=c:\MinGW\bin\gcc.exe
    set CXX=c:\MinGW\bin\g++.exe
    cd CMAKE
    cmake -G "MinGW Makefiles" -H. -Bwinbuild
    cmake --build winbuild -- -j2
    ```

**BSD (e.g. FreeBSD, TrueOS)**
- Install latest gcc and cmake:
    ```shell
    sudo pkg install gcc8 cmake
    ```
- From a bash shell:
    ```shell
    export CC=$(which gcc8)
    export CXX=$(which g++8)
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
