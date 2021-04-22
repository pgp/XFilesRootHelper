@ECHO OFF

:: main entry point
IF "%1" == "-f" (
    call :fullbuild
) ELSE (
    IF "%1" == "" (
        call :stdbuild
    ) ELSE (
        echo Only -f option is allowed - for full build
		EXIT /B 1
    )
)
EXIT /B %ERRORLEVEL%


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
EXIT /B 0


:: full build
:fullbuild
cd CMAKE
md ..\bin
rd /S /Q winbuild
set CC=c:\MinGW\bin\gcc.exe
set CXX=c:\MinGW\bin\g++.exe
cmake -G "MinGW Makefiles" -DCMAKE_SH="CMAKE_SH-NOTFOUND" -H. -Bwinbuild || EXIT /B
call :detectcpus
cmake --build winbuild -- -j%ERRORLEVEL% || EXIT /B
robocopy ..\cert ..\bin /s /e
cd ..
EXIT /B 0
