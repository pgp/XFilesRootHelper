#ifndef _COMMON_WIN_H_
#define _COMMON_WIN_H_

// MSVC
#ifdef _MSC_VER
#define NOMINMAX
#endif

#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <shobjidl.h>   // For ITaskbarList3
#include <iostream>
#include <cstdlib>
#include <thread>
#include <cstdint>

// MSVC
#ifdef _MSC_VER
typedef int64_t ssize_t;
#endif


ITaskbarList3 *console_pTaskbarList = nullptr;   // careful, COM objects should only be accessed from apartment they are created in

HWND console_hwnd = nullptr;

HWND GetConsoleHwnd() {
    constexpr size_t MY_BUFSIZE = 1024; // Buffer size for console window titles.
    HWND hwndFound;         // This is what is returned to the caller.
    char pszNewWindowTitle[MY_BUFSIZE]; // Contains fabricated
    // WindowTitle.
    char pszOldWindowTitle[MY_BUFSIZE]; // Contains original
    // WindowTitle.

    // Fetch current window title.
    GetConsoleTitleA(pszOldWindowTitle, MY_BUFSIZE);

    // Format a "unique" NewWindowTitle.
    sprintf(pszNewWindowTitle,"%d/%d",GetTickCount(),GetCurrentProcessId());

    // Change current window title.
    SetConsoleTitleA(pszNewWindowTitle);

    // Ensure window title has been updated.
    Sleep(40);

    // Look for NewWindowTitle.
    hwndFound=FindWindowA(nullptr, pszNewWindowTitle);

    // Restore original window title.
    SetConsoleTitleA(pszOldWindowTitle);

    return hwndFound;
}

void SafeCoCreateInstance(ITaskbarList3* &pTaskbarList, void (*progressThreadFn)(HWND), HWND hWnd) {
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pTaskbarList));
    if (SUCCEEDED(hr)) {
        // std::cout<<"CoCreateInstance succeeded"<<std::endl;
        hr = pTaskbarList->HrInit();
        if (FAILED(hr)) {
            std::cout<<"HrInit failed"<<std::endl;
            pTaskbarList->Release();
            pTaskbarList = nullptr;
        }
        else {
            // std::cout<<"HrInit succeeded"<<std::endl;
            if(progressThreadFn == nullptr)
                /*std::cout<<"Null function pointer for progress thread function"<<std::endl*/;
            else {
                std::thread progressThread(progressThreadFn,hWnd);
                progressThread.detach();
            }
        }
    }
    else std::cout<<"CoCreateInstance failed"<<std::endl;
}

#endif /* _COMMON_WIN_H_ */
