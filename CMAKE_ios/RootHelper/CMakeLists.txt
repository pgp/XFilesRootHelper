cmake_minimum_required(VERSION 3.6)

set(BOTAN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../botanAm/ios/arm")

include_directories(${BOTAN_DIR})
set(BOTAN_AM_SRC ${BOTAN_DIR}/botan_all.cpp)

include_directories(
  "-I../../CPP/Windows"
  "-I../../CPP/Common"
  "-I../../C"
  "-I../../RootHelper"
  "-I../../RootHelper/hashing"
  "-I../../RootHelper/reqs"
  "-I../../RootHelper/resps"
  "../../CPP/myWindows"
  "../../CPP/"
  "../../CPP/include_windows"
  "${BOTAN_DIR}"
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -Werror=return-type")

add_definitions( -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_REENTRANT -DENV_UNIX -DBREAK_HANDLER -DUNICODE -D_UNICODE -DUNIX_USE_WIN_FILE)

add_executable(r
"../../CPP/7zip/Common/FileStreams.cpp"
"../../RootHelper/r.cpp"
"../../CPP/Common/IntToString.cpp"
"../../CPP/Common/MyString.cpp"
"../../CPP/Common/MyVector.cpp"
"../../CPP/Common/MyWindows.cpp"
"../../CPP/Common/StringConvert.cpp"
"../../CPP/Common/UTFConvert.cpp"
"../../CPP/Common/Wildcard.cpp"
"../../CPP/Windows/DLL.cpp"
"../../CPP/Windows/FileDir.cpp"
"../../CPP/Windows/FileFind.cpp"
"../../CPP/Windows/FileIO.cpp"
"../../CPP/Windows/FileName.cpp"
"../../CPP/Windows/PropVariant.cpp"
"../../CPP/Windows/PropVariantConv.cpp"
"../../CPP/myWindows/wine_date_and_time.cpp"
"${BOTAN_AM_SRC}"
"../../C/Threads.c"
)

target_link_libraries(r "-arch armv7 --sysroot=${SYSROOT} -Wl,-syslibroot,${SYSROOT} -stdlib=libc++ -miphoneos-version-min=6.0")
