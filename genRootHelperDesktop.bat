cd CMAKE
cmake --build build -- -j4

robocopy ..\cert ..\bin /s /e
