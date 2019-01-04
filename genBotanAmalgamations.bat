set BOTAN_DEST_ANDROID_DIR=c:\sdcard\botanAm\android

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=x86 --os=linux --cc=clang --disable-avx2 --disable-aes-ni --disable-sha-ni
move botan_all.cpp botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_ANDROID_DIR%\x86

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=x64 --os=linux --cc=clang --disable-avx2 --disable-aes-ni --disable-sha-ni
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_ANDROID_DIR%\x86_64

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=armv7 --os=linux --cc=clang
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_ANDROID_DIR%\arm

python configure.py --amalgamation --single-amalgamation-file --disable-modules=pkcs11 --cpu=aarch64 --os=linux --cc=clang
move botan_all.cpp botan_all.h botan_all_internal.h %BOTAN_DEST_ANDROID_DIR%\arm64