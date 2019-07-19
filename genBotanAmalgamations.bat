setlocal enableextensions

REM dummy line

set BOTAN_SRC_DIR=c:\Windows\Temp\Botan-2.11.0

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
mkdir %BOTAN_DEST_DESKTOP_DIR%\linux\x86_64
mkdir %BOTAN_DEST_DESKTOP_DIR%\bsd\x86_64
mkdir %BOTAN_DEST_DESKTOP_DIR%\mac\x86_64

cd %BOTAN_SRC_DIR%


REM ########## ANDROID

REM # Android Emulator x86 does not support extensions from AVX2 onwards
REM # PKCS11 disabled due to Botan amalgamtion generation bug since v2.0.1

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=x86 --os=linux --cc=clang --disable-avx2 --disable-aes-ni --disable-sha-ni
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_ANDROID_DIR%\x86

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=x64 --os=linux --cc=clang --disable-avx2 --disable-aes-ni --disable-sha-ni
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_ANDROID_DIR%\x86_64

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=armv7 --os=linux --cc=clang
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_ANDROID_DIR%\arm

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=aarch64 --os=linux --cc=clang
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_ANDROID_DIR%\arm64


REM ########## IOS

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=arm --os=ios --cc=clang
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_IOS_DIR%\arm

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=aarch64 --os=ios --cc=clang
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_IOS_DIR%\arm64


REM ########## Desktop (with all possible cpu extensions enabled)

REM # Windows build not tested with MSVC, please download MingW from https://nuwen.net/mingw.html
python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=x64 --os=mingw --cc=gcc
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_DESKTOP_DIR%\windows\x86_64

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=x64 --os=linux --cc=gcc
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_DESKTOP_DIR%\linux\x86_64

REM # See README file for building on MacOS
python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=x64 --os=darwin --cc=gcc
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_DESKTOP_DIR%\mac\x86_64

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=x64 --os=freebsd --cc=gcc
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_DESKTOP_DIR%\bsd\x86_64

