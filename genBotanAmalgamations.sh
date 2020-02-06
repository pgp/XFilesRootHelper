#!/bin/bash

set -e

BOTAN_SRC_DIR=/home/pgp/Scaricati/Botan-2.13.0
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
mkdir -p $BOTAN_DEST_DESKTOP_DIR/linux/x86_64
mkdir -p $BOTAN_DEST_DESKTOP_DIR/bsd/x86_64
mkdir -p $BOTAN_DEST_DESKTOP_DIR/mac/x86_64

cd $BOTAN_SRC_DIR


########## ANDROID

# Android Emulator x86 does not support extensions from AVX2 onwards
# PKCS11 disabled due to Botan amalgamtion generation bug since v2.0.1

./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x86 --os=linux --cc=clang --disable-avx2 --disable-aes-ni --disable-sha-ni
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_ANDROID_DIR/x86

./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=linux --cc=clang --disable-avx2 --disable-aes-ni --disable-sha-ni
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_ANDROID_DIR/x86_64

./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=armv7 --os=linux --cc=clang
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_ANDROID_DIR/arm

./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=aarch64 --os=linux --cc=clang
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_ANDROID_DIR/arm64


########## IOS

./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --without-os-feature=thread_local --cpu=arm --os=ios --cc=clang
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_IOS_DIR/arm

./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=aarch64 --os=ios --cc=clang
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_IOS_DIR/arm64


########## Desktop (with all possible cpu extensions enabled)

# Windows build with MinGW, please download MingW from https://nuwen.net/mingw.html
./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=mingw --cc=gcc
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_DESKTOP_DIR/windows/x86_64

# Windows build with MSVC, at least Visual Studio Build Tools are needed
./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=windows --cc=msvc
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_DESKTOP_DIR/windows/x64_msvc

./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=linux --cc=gcc
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_DESKTOP_DIR/linux/x86_64

# See README file for building on MacOS
./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=darwin --cc=gcc
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_DESKTOP_DIR/mac/x86_64

./configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11,tls_10 --disable-cc-tests --cpu=x64 --os=freebsd --cc=gcc
mv botan_all.cpp botan_all.h botan_all_internal.h $BOTAN_DEST_DESKTOP_DIR/bsd/x86_64
