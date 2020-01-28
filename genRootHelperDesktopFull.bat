cd CMAKE

md ..\bin
rd /S /Q winbuild

set CC=c:\MinGW\bin\gcc.exe
set CXX=c:\MinGW\bin\g++.exe

cmake -G "MinGW Makefiles" -DCMAKE_SH="CMAKE_SH-NOTFOUND" -H. -Bwinbuild
cmake --build winbuild -- -j4

robocopy ..\cert ..\bin /s /e

cd ..
