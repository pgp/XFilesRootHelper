#ifndef _WIN32
#error "Windows required"
#endif

#ifdef _WIN32
#ifndef _MFC_HASHVIEW_
#define _MFC_HASHVIEW_

#include "../common_win.h"
#include <tchar.h>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <thread>
#include <iostream>
#include <random>

#include "hashview.h"

Botan::secure_vector<uint8_t> latestForHashview;

constexpr size_t squareSize = 50;
constexpr size_t gridSize = 16;
constexpr size_t bitsPerCell = 3;
constexpr size_t outBitsLen = gridSize*gridSize*bitsPerCell; // 16*16 grid, 3 bpp

// colors copied from XFiles' HashView.java
COLORREF colorMap[] = {RGB(0, 0, 0), RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255),
                       RGB(255, 255, 255), RGB(255, 255, 0), RGB(0, 255, 255), RGB(255, 0, 255),
                       RGB(0x7F, 0, 0), RGB(0, 0x7F, 0x7F), RGB(0, 0x7F, 0), RGB(0x7F, 0x7F, 0),
                       RGB(0x7F, 0x44, 0), RGB(0x7F, 0, 0x6E), RGB(0xFF, 0x88, 0), RGB(0x7F, 0x7F, 0x7F)};

void createAndShowSquareGridFromBytes (HWND hWnd,
                                       size_t gridSize,
                                       size_t squareSize,
                                       size_t bitsPerCell,
                                       uint8_t* b,
                                       size_t bLenInBytes) {
    PAINTSTRUCT ps;
    HDC hDC = BeginPaint(hWnd, &ps);
    HPEN hpenOld = static_cast<HPEN>(SelectObject(hDC, GetStockObject(DC_PEN)));
    HBRUSH hbrushOld = static_cast<HBRUSH>(SelectObject(hDC, GetStockObject(NULL_BRUSH)));

    RECT* grid = new RECT[gridSize*gridSize];
    HBRUSH* brushGrid = new HBRUSH[gridSize*gridSize];

    std::vector<bool> bv = bytesToBools(b,bLenInBytes);

    size_t currentLeft = 0;
    size_t currentRight = squareSize;
    size_t currentTop = 0;
    size_t currentBottom = squareSize;

    for (size_t i=0;i<gridSize;i++) {
        for (size_t j=0;j<gridSize;j++) {
            size_t k = i*gridSize+j;

            int color = getBitSeqFromBooleanArray(bitsPerCell*k,bitsPerCell,bv);

            brushGrid[k] = CreateSolidBrush(colorMap[color]);

            grid[k].top = currentTop;
            grid[k].bottom = currentBottom;
            grid[k].left = currentLeft;
            grid[k].right = currentRight;

            currentTop += squareSize;
            currentBottom += squareSize;

            Rectangle(hDC,grid[k].left,grid[k].top,grid[k].right,grid[k].bottom);
            FillRect(hDC,&grid[k],brushGrid[k]);
        }

        currentLeft += squareSize;
        currentRight += squareSize;

        currentTop = 0;
        currentBottom = squareSize;
    }

    SelectObject(hDC, hpenOld);
    SelectObject(hDC, hbrushOld);
    EndPaint(hWnd, &ps);
}

// The main window class name.
constexpr static TCHAR szWindowClass[] = _T("win32app");

// The string that appears in the application's title bar.
constexpr static TCHAR szTitle[] = _T("Hashview");


HINSTANCE H = 0;
WNDCLASSEX wcex;

// Forward declarations of functions included in this code module:
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT:
            createAndShowSquareGridFromBytes(hWnd,gridSize,squareSize,bitsPerCell,&latestForHashview[0],latestForHashview.size());
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

constexpr int nCmdShow = SW_NORMAL;

void initGUIContext() {
    if (H == 0) {
        H = GetModuleHandle(0);

        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style          = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc    = WndProc;
        wcex.cbClsExtra     = 0;
        wcex.cbWndExtra     = 0;
        wcex.hInstance      = H;
        wcex.hIcon          = LoadIcon(H, IDI_APPLICATION);
        wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
        wcex.lpszMenuName   = NULL;
        wcex.lpszClassName  = szWindowClass;
        wcex.hIconSm        = LoadIcon(wcex.hInstance, IDI_APPLICATION);

        if (!RegisterClassEx(&wcex))
        {
            MessageBox(nullptr,
                       _T("Call to RegisterClassEx failed!"),
                       szTitle,
                       0);

            return ;
        }
    }
    else {
        std::cout<<"GUI Context already initialized"<<std::endl;
    }
}

void runMFCSessionWithColorGrid(Botan::secure_vector<uint8_t> inBytes) {
    initGUIContext();

    std::unique_ptr<Botan::HashFunction> sponge(new Botan::SHAKE_128(outBitsLen)); // ctor accepts bits, multiply by 8
    sponge->update(&inBytes[0],inBytes.size());
    latestForHashview = sponge->final();

    HWND hWnd = CreateWindow(
            szWindowClass,
            szTitle,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT,
            squareSize*gridSize, squareSize*gridSize,
            NULL,
            NULL,
            H,
            NULL
    );

    if (!hWnd)
    {
        MessageBox(nullptr,
                   _T("Call to CreateWindow failed!"),
                   szTitle,
                   0);

        return;
    }

    ShowWindow(hWnd,nCmdShow);
    UpdateWindow(hWnd);

    // Main message loop:
    MSG msg;
    while(GetMessage(&msg, hWnd, 0, 0)>=0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    std::cout<<"GUI ended for hWnd "<<(uint64_t)(hWnd)<<std::endl;
}

#endif /* _MFC_HASHVIEW_ */
#endif
