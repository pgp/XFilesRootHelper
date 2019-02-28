#!/bin/bash

set -e

# if [ -z "$ANDROID_NDK_HOME" ]; then echo "ANDROID_NDK_HOME is unset" && exit 1; else echo "ANDROID_NDK_HOME is set"; fi

# download Android NDK if not present
ANDROID_NDK_HOME=`pwd`/android-ndk-r19b
if [ -d "${ANDROID_NDK_HOME}" ] 
then
    echo "Directory ${ANDROID_NDK_HOME} exists." 
else
    echo "Directory ${ANDROID_NDK_HOME} does not exist, downloading ndk..."
    wget https://dl.google.com/android/repository/android-ndk-r19b-linux-x86_64.zip
    echo "ndk download complete, unzipping..."
    unzip -qq android-ndk-r19b-linux-x86_64.zip
fi

MAINDIR=$(pwd)

FORMAT7ZDIR=$MAINDIR/ANDROID/Format7zFree/jni
RHDIR=$MAINDIR/ANDROID/RootHelper/jni

export PATH=$PATH:$ANDROID_NDK_HOME

cd $FORMAT7ZDIR
ndk-build -j2

cd $RHDIR
ndk-build -j2
