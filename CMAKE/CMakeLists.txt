cmake_minimum_required(VERSION 3.6)

project(XFilesRootHelper)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin")

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

add_definitions(-DENV_HAVE_GCCVISIBILITYPATCH)

#SET(CMAKE_CXX_FLAGS "-fvisibility=hidden -fvisibility-inlines-hidden")

#~ add_subdirectory(7za)

#~ add_subdirectory(7z_)

#~ add_subdirectory(7zr)

if(WIN32)
else()
add_subdirectory(Format7zFree  bin)
endif()

add_subdirectory(RootHelper)

#~ find_package(wxWidgets COMPONENTS core base adv)

#~ if(wxWidgets_FOUND)

	#~ add_subdirectory(7zG)

	#~ add_subdirectory(7zFM)

#~ else(wxWidgets_FOUND)
	#~ message("Warning : wxWidgets not found")
#~ endif(wxWidgets_FOUND)
