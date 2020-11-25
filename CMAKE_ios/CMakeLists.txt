cmake_minimum_required(VERSION 3.6)

project(XFilesRootHelper)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmakebin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmakebin")
    
set(SYSROOT "/Users/pgp/opt/Legacy-iOS-SDKs/iPhoneOS6.1.sdk")
set(CMAKE_CXX_STANDARD 11)

IF(HAVE_WCHAR_H)
  OPTION(HAVE_UNICODE_WCHAR "Enable Unicode Support" YES)
  MARK_AS_ADVANCED(HAVE_UNICODE_WCHAR)
ELSE(HAVE_WCHAR_H)
  SET(HAVE_UNICODE_WCHAR 0)
ENDIF(HAVE_WCHAR_H)

SET(HAVE_PTHREADS 0)
FIND_PACKAGE(Threads)
IF(WIN32 OR CMAKE_USE_PTHREADS_INIT)
  SET(HAVE_PTHREADS 1)
ENDIF(WIN32 OR CMAKE_USE_PTHREADS_INIT)

#~ add_definitions(-DENV_HAVE_GCCVISIBILITYPATCH)
add_compile_options(-O3)

# SET(CMAKE_CXX_FLAGS "-fvisibility=hidden -fvisibility-inlines-hidden")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -arch armv7 -g -fexceptions -Wno-narrowing -Werror=return-type -stdlib=libc++ --sysroot=${SYSROOT} -DHAVE\_ARMV6=1 -DZ\_PREFIX -DPLATFORM\_IPHONE -DARM_FPU_VFP=1 -miphoneos-version-min=6.0 -mno-thumb")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch armv7 -g -Werror=return-type --sysroot=${SYSROOT} -DHAVE\_ARMV6=1 -DZ\_PREFIX -DPLATFORM\_IPHONE -DARM_FPU_VFP=1 -miphoneos-version-min=6.0 -mno-thumb")

add_subdirectory(7za)

add_subdirectory(7z_)

add_subdirectory(7zr)

add_subdirectory(Format7zFree  bin)

add_subdirectory(RootHelper)
