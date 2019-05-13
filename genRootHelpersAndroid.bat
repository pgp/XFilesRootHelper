setlocal enableextensions enabledelayedexpansion

SET "NDKDIR=%appdata%\..\Local\Android\Sdk\ndk-bundle"

set "XFILES_ASSET_DIR=%userprofile%\android-workspace\XFiles\libs"

md %XFILES_ASSET_DIR%

set "MAINDIR=%cd%"

set "FORMAT7ZDIR=%MAINDIR%\ANDROID\Format7zFree\jni"
set "RHDIR=%MAINDIR%\ANDROID\RootHelper\jni"

set "FORMAT7ZLIBDIR=%MAINDIR%\ANDROID\Format7zFree\libs"
set "RHLIBDIR=%MAINDIR%\ANDROID\RootHelper\libs"

set "TLSCERTDIR=%MAINDIR%\cert"

cd %FORMAT7ZDIR%
call %NDKDIR%\ndk-build -j4

cd %RHDIR%
call %NDKDIR%\ndk-build -j4


REM rename to libr.so (for gradle to accept it as embeddable in apk)
cd %RHLIBDIR%
for /D %%i in (*) do ( cd %%i & ren r libr.so & cd..)

REM copy libraries

robocopy %RHLIBDIR% %XFILES_ASSET_DIR% /s /e
robocopy %FORMAT7ZLIBDIR% %XFILES_ASSET_DIR% /s /e

cd %XFILES_ASSET_DIR%
for /D %%i in (*) do ( cd %%i & copy %TLSCERTDIR%\dummycrt.pem libdummycrt.so /Y & copy %TLSCERTDIR%\dummykey.pem libdummykey.so /Y & cd..)
