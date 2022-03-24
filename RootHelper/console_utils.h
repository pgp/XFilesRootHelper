#ifndef _CONSOLE_UTILS_H_
#define _CONSOLE_UTILS_H_

#ifdef _WIN32
#include "common_win.h"
#else
#include <sys/ioctl.h>
#endif

typedef struct {
    uint16_t W;
    uint16_t H;
} ConsoleDims;

// web source:
// https://stackoverflow.com/questions/6812224/getting-terminal-size-in-c-for-windows

ConsoleDims sampleConsoleDimensions() {
    ConsoleDims dims;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    dims.W = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    dims.H = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    dims.W = w.ws_col;
    dims.H = w.ws_row;
#endif
    return dims;
}
#endif /* _CONSOLE_UTILS_H_ */