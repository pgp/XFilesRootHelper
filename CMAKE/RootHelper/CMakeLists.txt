cmake_minimum_required(VERSION 3.6)

set(CMAKE_CXX_STANDARD 11)

find_package(X11)

message(STATUS "X11_FOUND = ${X11_FOUND}")
message(STATUS "X11_INCLUDE_DIR = ${X11_INCLUDE_DIR}")
message(STATUS "X11_LIBRARIES = ${X11_LIBRARIES}")

if(WIN32)
else()
	set(USE_X11 "USE_X11") # comment this line if you don't want X11 GUI hash window or it's not available (e.g. on MacOS when XQuartz is not installed)
endif()

if(DEFINED USE_X11)
	if(X11_FOUND)
		add_definitions("-D${USE_X11}")
	endif()
endif()

if(WIN32) # -O3 allows fast data transfer (order of Gbps)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s -O3 -municode -mavx -Wl,--stack,67108864 -Wa,-mbig-obj")
else()
	if(DEFINED USE_X11)
		if(X11_FOUND)
			include_directories(${X11_INCLUDE_DIR})
			link_directories(${X11_LIBRARIES})
		else()
		endif()
	else()
	endif()

	add_compile_options(-O3 -mavx)
endif()

if(WIN32)
	set(CMAKE_RC_COMPILER_INIT windres)
	ENABLE_LANGUAGE(RC)
	SET(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> <FLAGS> -O coff <DEFINES> -i <SOURCE> -o <OBJECT>")
endif()

set(BOTAN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../botanAm/desktop")

if(WIN32)
	set(BOTAN_DIR ${BOTAN_DIR}/windows/x86_64)
elseif(APPLE)
	set(BOTAN_DIR ${BOTAN_DIR}/mac/x86_64)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(BOTAN_DIR ${BOTAN_DIR}/linux/x86_64)
else() # assume BSD
	set(BOTAN_DIR ${BOTAN_DIR}/bsd/x86_64)
endif()
include_directories(${BOTAN_DIR})
set(BOTAN_AM_SRC ${BOTAN_DIR}/botan_all.cpp)

if(WIN32)
	include_directories(
			"-I../../RootHelper"
			"-I../../RootHelper/hashing"
			"-I../../RootHelper/reqs"
			"-I../../RootHelper/resps"
			"${BOTAN_DIR}"
	)
else()
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
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -Werror=return-type")

# add_definitions( -D_LARGEFILE_SOURCE -D_REENTRANT -DENV_UNIX -DBREAK_HANDLER -DUNICODE -D_UNICODE -DUNIX_USE_WIN_FILE)
add_definitions( -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_REENTRANT -DENV_UNIX -DBREAK_HANDLER -DUNICODE -D_UNICODE -DUNIX_USE_WIN_FILE)


if(WIN32)
	add_executable(r
			"../../appIcon.rc"
			"../../RootHelper/xrewin.cpp"
			"${BOTAN_AM_SRC}"
			)
else()
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
			"../../CPP/Windows/TimeUtils.cpp"
			"../../CPP/myWindows/wine_date_and_time.cpp"
			"${BOTAN_AM_SRC}"
			"../../C/Threads.c"
			)
endif()

if(WIN32)
else()
	find_library(DL_LIB dl)
	link_directories(${DL_LIB_PATH})

	IF(HAVE_PTHREADS)
		TARGET_LINK_LIBRARIES(r ${CMAKE_THREAD_LIBS_INIT} dl)
	ENDIF(HAVE_PTHREADS)
endif()

if(DEFINED USE_X11)
	if(X11_FOUND)
		target_link_libraries(r ${X11_LIBRARIES})
	else()
	endif()
else()
endif()

if(WIN32)
	target_link_libraries(r wsock32 ws2_32)
else()
	if(APPLE)
		target_link_libraries(r "-framework Security -framework Foundation")
	else()
		if(${CMAKE_SYSTEM_NAME} MATCHES "BSD")
			target_link_libraries(r "-static-libstdc++ -lpthread")
		else()
			target_link_libraries(r pthread)
		endif()
	endif()
endif()
