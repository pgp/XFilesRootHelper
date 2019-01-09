cd CMAKE
cmake --build winbuild -- -j4

robocopy ..\cert ..\bin /s /e
