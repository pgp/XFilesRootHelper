cd CMAKE

md ..\bin
rd /S /Q build

set CC=c:\MinGW\bin\gcc.exe
set CXX=c:\MinGW\bin\g++.exe

cmake -G "MinGW Makefiles" -H. -Bbuild
cmake --build build -- -j4

robocopy ..\cert ..\bin /s /e
