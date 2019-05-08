LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := r

# BEGIN Botan TLS
BOTAN_AM_PREFIX := ../../../botanAm/android

$(info $(TARGET_ARCH))

BOTAN_AM_SRC := ${BOTAN_AM_PREFIX}/$(TARGET_ARCH)/botan_all.cpp
BOTAN_AM_INCL := ${BOTAN_AM_PREFIX}/$(TARGET_ARCH)
# END Botan TLS

# web source for ARM:
# https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html


# working from android-19 onwards with ndk14 auxv.h patch
ARCH_OPT_FLAGS_arm := -march=armv7-a -mfpu=neon -mfloat-abi=softfp
ARCH_OPT_FLAGS_arm64 := -march=armv8-a+crc+simd+crypto

# ARCH_OPT_FLAGS_x86 := -msse2 -msse3 -mssse3 -msse4 -msse4.1 -msse4.2 -mavx -mavx2 -msha -maes
# ARCH_OPT_FLAGS_x86_64 := -msse2 -msse3 -mssse3 -msse4 -msse4.1 -msse4.2 -mavx -mavx2 -msha -maes

# Android Emulator x86 not supporting from AVX onwards
# ARCH_OPT_FLAGS_x86_64 := -march=native -msha -maes
# ARCH_OPT_FLAGS_x86 := -march=native -msha -maes

ARCH_OPT_FLAGS_x86 := -msse2 -msse3 -mssse3 -msse4 -msse4.1 -msse4.2
ARCH_OPT_FLAGS_x86_64 := -msse2 -msse3 -mssse3 -msse4 -msse4.1 -msse4.2

# -D_FILE_OFFSET_BITS=64

LOCAL_CFLAGS := -O2 -DANDROID_NDK -fexceptions \
	${ARCH_OPT_FLAGS_${TARGET_ARCH}} \
	-DNDEBUG -D_REENTRANT -DENV_UNIX \
	-DEXTERNAL_CODECS \
	-DBREAK_HANDLER \
	-DUNICODE -D_UNICODE -DUNIX_USE_WIN_FILE \
	-I../../../CPP/Windows \
	-I../../../CPP/Common \
	-I../../../C \
	-I../../../RootHelper \
	-I../../../RootHelper/hashing \
	-I../../../RootHelper/reqs \
	-I../../../RootHelper/resps \
	-I../../../CPP/myWindows \
	-I../../../CPP \
	-I../../../CPP/include_windows

LOCAL_CPPFLAGS := -O2 -std=c++11 -frtti

LOCAL_C_INCLUDES += ${BOTAN_AM_INCL}

LOCAL_SRC_FILES := \
  ${BOTAN_AM_SRC} \
  ../../../CPP/7zip/Common/FileStreams.cpp \
  ../../../RootHelper/r.cpp \
  ../../../CPP/Common/IntToString.cpp \
  ../../../CPP/Common/MyString.cpp \
  ../../../CPP/Common/MyVector.cpp \
  ../../../CPP/Common/MyWindows.cpp \
  ../../../CPP/Common/StringConvert.cpp \
  ../../../CPP/Common/UTFConvert.cpp \
  ../../../CPP/Common/Wildcard.cpp \
  ../../../CPP/Windows/DLL.cpp \
  ../../../CPP/Windows/FileDir.cpp \
  ../../../CPP/Windows/FileFind.cpp \
  ../../../CPP/Windows/FileIO.cpp \
  ../../../CPP/Windows/FileName.cpp \
  ../../../CPP/Windows/PropVariant.cpp \
  ../../../CPP/Windows/PropVariantConv.cpp \
  ../../../CPP/Windows/TimeUtils.cpp \
  ../../../CPP/myWindows/wine_date_and_time.cpp \
  ../../../C/Threads.c \

# LOCAL_STATIC_LIBRARIES := -lstdc++fs # no experimental::filesystem support on NDK

LOCAL_CFLAGS += -fPIC -pie
LOCAL_LDFLAGS += -fPIE -llog -latomic -Wl,-E

include $(BUILD_EXECUTABLE)
