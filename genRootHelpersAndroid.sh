#!/bin/bash

set -e

NDKDIR=/home/pgp/Android/Sdk/ndk-bundle

XFILES_ASSET_DIR=/media/pgp/Dati/android-workspace/XFiles/libs
mkdir -p $XFILES_ASSET_DIR 

MAINDIR=$(pwd)

FORMAT7ZDIR=$MAINDIR/ANDROID/Format7zFree/jni
RHDIR=$MAINDIR/ANDROID/RootHelper/jni

FORMAT7ZLIBDIR=$MAINDIR/ANDROID/Format7zFree/libs
RHLIBDIR=$MAINDIR/ANDROID/RootHelper/libs

TLSCERTDIR=$MAINDIR/cert

export PATH=$PATH:$NDKDIR
# export PATH=$PATH:/home/pgp/Android/Sdk/ndk-bundle/

# build lib7z.so
cd $FORMAT7ZDIR
# rm -rf ../obj/*
# rm -rf ../libs/*
ndk-build -j12

# build roothelper executable shared object (r)
cd $RHDIR
# rm -rf ../obj/*
# rm -rf ../libs/*
ndk-build -j12

# rename to libr.so (for gradle to accept it as embeddable in apk)
cd $RHLIBDIR
for i in $(ls); do
mv ./$i/r ./$i/libr.so
done

rm -rf $XFILES_ASSET_DIR/*

######################### copy libraries

cd $RHLIBDIR
for i in $(ls); do
mkdir -p $XFILES_ASSET_DIR/$i
cp ./$i/libr.so $XFILES_ASSET_DIR/$i/libr.so
done

cd $FORMAT7ZLIBDIR
for i in $(ls); do
mkdir -p $XFILES_ASSET_DIR/$i
cp ./$i/lib7z.so $XFILES_ASSET_DIR/$i/lib7z.so
done

cd $XFILES_ASSET_DIR
for i in $(ls); do
cp $TLSCERTDIR/dummycrt.pem ./$i/libdummycrt.so
cp $TLSCERTDIR/dummykey.pem ./$i/libdummykey.so
done
