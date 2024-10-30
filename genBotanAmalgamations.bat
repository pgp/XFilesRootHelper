setlocal enableextensions

REM env variables

set NDK_PATH=%LOCALAPPDATA%\Android\Sdk\ndk\21.3.6528147
set BOTAN_SRC_DIR=c:\Windows\Temp\Botan-2.19.5

set BOTAN_DEST_ANDROID_DIR=%cd%\botanAm\android
set BOTAN_DEST_IOS_DIR=%cd%\botanAm\ios
set BOTAN_DEST_DESKTOP_DIR=%cd%\botanAm\desktop

mkdir %BOTAN_DEST_ANDROID_DIR%\x86
mkdir %BOTAN_DEST_ANDROID_DIR%\x86_64
mkdir %BOTAN_DEST_ANDROID_DIR%\arm
mkdir %BOTAN_DEST_ANDROID_DIR%\arm64

mkdir %BOTAN_DEST_IOS_DIR%\arm
mkdir %BOTAN_DEST_IOS_DIR%\arm64

mkdir %BOTAN_DEST_DESKTOP_DIR%\windows\x86_64
mkdir %BOTAN_DEST_DESKTOP_DIR%\windows\x64_msvc
mkdir %BOTAN_DEST_DESKTOP_DIR%\linux\x86_64
mkdir %BOTAN_DEST_DESKTOP_DIR%\bsd\x86_64
mkdir %BOTAN_DEST_DESKTOP_DIR%\mac\x86_64

cd %BOTAN_SRC_DIR%


REM ########## ANDROID

REM # Android Emulator x86 does not support extensions from AVX2 onwards
REM # PKCS11 disabled due to Botan amalgamtion generation bug since v2.0.1

python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x86 --os=linux --cc=clang --disable-avx2 --disable-aes-ni --disable-sha-ni
move botan_all.cpp %BOTAN_DEST_ANDROID_DIR%\x86
move botan_all.h %BOTAN_DEST_ANDROID_DIR%\x86

python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=linux --cc=clang --disable-avx2 --disable-aes-ni --disable-sha-ni
move botan_all.cpp %BOTAN_DEST_ANDROID_DIR%\x86_64
move botan_all.h %BOTAN_DEST_ANDROID_DIR%\x86_64

set AR=%NDK_PATH%\toolchains\llvm\prebuilt\linux-x86_64\bin\llvm-ar
set CXX=%NDK_PATH%\toolchains\llvm\prebuilt\linux-x86_64\bin\armv7a-linux-androideabi19-clang++

python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --cpu=armv7 --os=linux --cc=clang
move botan_all.cpp %BOTAN_DEST_ANDROID_DIR%\arm
move botan_all.h %BOTAN_DEST_ANDROID_DIR%\arm

set CXX=%NDK_PATH%\toolchains\llvm\prebuilt\linux-x86_64\bin\aarch64-linux-android21-clang++

python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --cpu=aarch64 --os=linux --cc=clang
move botan_all.cpp %BOTAN_DEST_ANDROID_DIR%\arm64
move botan_all.h %BOTAN_DEST_ANDROID_DIR%\arm64

set AR=
set CXX=

REM ########## IOS

python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --without-os-feature=thread_local --cpu=arm --os=ios --cc=clang
move botan_all.cpp %BOTAN_DEST_IOS_DIR%\arm
move botan_all.h %BOTAN_DEST_IOS_DIR%\arm

python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=aarch64 --os=ios --cc=clang
move botan_all.cpp %BOTAN_DEST_IOS_DIR%\arm64
move botan_all.h %BOTAN_DEST_IOS_DIR%\arm64


REM ########## Desktop (with all possible cpu extensions enabled)

REM # Windows build with MinGW, please download MingW from https://nuwen.net/mingw.html
python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=mingw --cc=gcc
move botan_all.cpp %BOTAN_DEST_DESKTOP_DIR%\windows\x86_64
move botan_all.h %BOTAN_DEST_DESKTOP_DIR%\windows\x86_64

REM Windows build with MSVC, at least Visual Studio Build Tools are needed
python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=windows --cc=msvc
move botan_all.cpp %BOTAN_DEST_DESKTOP_DIR%\windows\x64_msvc
move botan_all.h %BOTAN_DEST_DESKTOP_DIR%\windows\x64_msvc

python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=linux --cc=gcc
move botan_all.cpp %BOTAN_DEST_DESKTOP_DIR%\linux\x86_64
move botan_all.h %BOTAN_DEST_DESKTOP_DIR%\linux\x86_64

REM # See README file for building on MacOS
python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=darwin --cc=gcc
move botan_all.cpp %BOTAN_DEST_DESKTOP_DIR%\mac\x86_64
move botan_all.h %BOTAN_DEST_DESKTOP_DIR%\mac\x86_64

python configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=freebsd --cc=gcc
move botan_all.cpp %BOTAN_DEST_DESKTOP_DIR%\bsd\x86_64
move botan_all.h %BOTAN_DEST_DESKTOP_DIR%\bsd\x86_64
