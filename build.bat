@ECHO OFF

:: main entry point
:: 
:: (no options) -> stdbuild
:: -f, --full -> fullbuild
:: -i, --install -> install
:: -f -i (OR -i -f) -> fullbuild + install
:: -u, --uninstall -> uninstall

IF "%1" == "" goto :stdbuild

IF "%1" == "-u" goto :uninstall
IF "%1" == "--uninstall" goto :uninstall

IF "%1" == "-f" goto :fullbuild
IF "%1" == "--full" goto :fullbuild

IF "%1" == "-i" goto :prepareinstall
IF "%1" == "--install" goto :prepareinstall

:badusage
ECHO Invalid options, available ones are -f,--full for full build, -i,--install for install, -u,--uninstall for uninstall
EXIT /B %ERRORLEVEL%


:prepareinstall
IF "%2" == "" goto :stdbuild
IF "%2" == "-f" goto :fullbuild
IF "%2" == "--full" goto :fullbuild
goto :badusage

:: detect cpu number
:detectcpus
IF "%NUMBER_OF_PROCESSORS%"=="" ( 
    EXIT /B 2
) ELSE (
    EXIT /B %NUMBER_OF_PROCESSORS%
)


:: standard build
:stdbuild
cd CMAKE
call :detectcpus
cmake --build winbuild -- -j%ERRORLEVEL% || EXIT /B
robocopy ..\cert ..\bin /s /e
cd ..
IF "%1" == "-i" goto :install
IF "%1" == "--install" goto :install
EXIT /B 0


:: full build
:fullbuild
md bin
cd CMAKE
rd /S /Q winbuild
set CC=c:\MinGW\bin\gcc.exe
set CXX=c:\MinGW\bin\g++.exe
cmake -G "MinGW Makefiles" -DCMAKE_SH="CMAKE_SH-NOTFOUND" -H. -Bwinbuild || EXIT /B
call :detectcpus
cmake --build winbuild -- -j%ERRORLEVEL% || EXIT /B
robocopy ..\cert ..\bin /s /e
cd ..
IF "%1" == "-i" goto :install
IF "%1" == "--install" goto :install
IF "%2" == "-i" goto :install
IF "%2" == "--install" goto :install
EXIT /B 0


:: install
:install
cd bin
copy /Y r.exe %SYSTEMROOT%\System32
copy /Y dummycrt.pem %SYSTEMROOT%\System32
copy /Y dummykey.pem %SYSTEMROOT%\System32
cd ..
EXIT /B 0


:: uninstall
:uninstall
set CURDIR=%cd%
cd %SYSTEMROOT%\System32
echo Removing r.exe
del r.exe
echo Removing dummycrt.pem
del dummycrt.pem
echo Removing dummykey.pem
del dummykey.pem
cd %CURDIR%
EXIT /B 0
