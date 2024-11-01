#!/bin/bash

set -e

NDK_PATH=$HOME/Android/Sdk/ndk/21.3.6528147
BOTAN_SRC_DIR=$HOME/Scaricati/Botan-2.19.5
BOTAN_DEST_DIR=$(pwd)/botanAm

BOTAN_DEST_ANDROID_DIR=$BOTAN_DEST_DIR/android
BOTAN_DEST_IOS_DIR=$BOTAN_DEST_DIR/ios
BOTAN_DEST_DESKTOP_DIR=$BOTAN_DEST_DIR/desktop

mkdir -p $BOTAN_DEST_ANDROID_DIR/x86
mkdir -p $BOTAN_DEST_ANDROID_DIR/x86_64
mkdir -p $BOTAN_DEST_ANDROID_DIR/arm
mkdir -p $BOTAN_DEST_ANDROID_DIR/arm64

mkdir -p $BOTAN_DEST_IOS_DIR/arm
mkdir -p $BOTAN_DEST_IOS_DIR/arm64

mkdir -p $BOTAN_DEST_DESKTOP_DIR/windows/x86_64
mkdir -p $BOTAN_DEST_DESKTOP_DIR/windows/x64_msvc
mkdir -p $BOTAN_DEST_DESKTOP_DIR/linux/x86_64
mkdir -p $BOTAN_DEST_DESKTOP_DIR/bsd/x86_64
mkdir -p $BOTAN_DEST_DESKTOP_DIR/mac/x86_64

cd $BOTAN_SRC_DIR


########## ANDROID

# Android Emulator x86 does not support extensions from AVX2 onwards
# PKCS11 disabled due to Botan amalgamtion generation bug since v2.0.1

echo "****************** Android x86 ******************"
./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x86 --os=linux --cc=clang --disable-avx2 --disable-aes-ni --disable-sha-ni
mv botan_all.cpp botan_all.h $BOTAN_DEST_ANDROID_DIR/x86

echo "****************** Android x64 ******************"
./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=linux --cc=clang --disable-avx2 --disable-aes-ni --disable-sha-ni
mv botan_all.cpp botan_all.h $BOTAN_DEST_ANDROID_DIR/x86_64

export AR=$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar

export CXX=$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi19-clang++
echo "****************** Android armv7 ******************"
./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --cpu=armv7 --os=linux --cc=clang
mv botan_all.cpp botan_all.h $BOTAN_DEST_ANDROID_DIR/arm

export CXX=$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang++
echo "****************** Android aarch64 ******************"
./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --cpu=aarch64 --os=linux --cc=clang
mv botan_all.cpp botan_all.h $BOTAN_DEST_ANDROID_DIR/arm64

unset AR
unset CXX
echo "************************************"
########## IOS

./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --without-os-feature=thread_local --cpu=arm --os=ios --cc=clang
mv botan_all.cpp botan_all.h $BOTAN_DEST_IOS_DIR/arm

./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=aarch64 --os=ios --cc=clang
mv botan_all.cpp botan_all.h $BOTAN_DEST_IOS_DIR/arm64


########## Desktop (with all possible cpu extensions enabled)

# Windows build with MinGW, please download MingW from https://nuwen.net/mingw.html
# AND use at least commit cb6f4c4 from https://github.com/randombit/botan
./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=mingw --cc=gcc
mv botan_all.cpp botan_all.h $BOTAN_DEST_DESKTOP_DIR/windows/x86_64

# Windows build with MSVC, at least Visual Studio Build Tools are needed
./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=windows --cc=msvc
mv botan_all.cpp botan_all.h $BOTAN_DEST_DESKTOP_DIR/windows/x64_msvc

./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --cpu=x64 --os=linux --cc=gcc
mv botan_all.cpp botan_all.h $BOTAN_DEST_DESKTOP_DIR/linux/x86_64

# See README file for building on MacOS
./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=darwin --cc=gcc
mv botan_all.cpp botan_all.h $BOTAN_DEST_DESKTOP_DIR/mac/x86_64

./configure.py --amalgamation --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=freebsd --cc=gcc
mv botan_all.cpp botan_all.h $BOTAN_DEST_DESKTOP_DIR/bsd/x86_64
