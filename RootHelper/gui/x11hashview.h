#ifndef _X11_HASHVIEW_
#define _X11_HASHVIEW_

#include "hashview.h"
#include <chrono>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>

#include "botan_all.h"

XColor getSingleColor(uint16_t R, uint16_t G, uint16_t B) {
    XColor xcolour;
    xcolour.red = R*256;
    xcolour.green = G*256;
    xcolour.blue = B*256;
    xcolour.flags = DoRed | DoGreen | DoBlue;
    return xcolour;
}

// colors copied from XFiles' HashView.java
// XColor has 16 bits per channel, just multiply by 256
const std::vector<XColor> xcolors {
        getSingleColor(0,0,0),
        getSingleColor(255,0,0),
        getSingleColor(0,255,0),
        getSingleColor(0,0,255),
        getSingleColor(255,255,255),
        getSingleColor(255,255,0),
        getSingleColor(0,255,255),
        getSingleColor(255,0,255),
        getSingleColor(0x7F,0,0),
        getSingleColor(0,0x7F,0x7F),
        getSingleColor(0,0x7F,0),
        getSingleColor(0x7F,0x7F,0),
        getSingleColor(0x7F,0x44,0),
        getSingleColor(0x7F,0,0x6E),
        getSingleColor(0xFF,0x88,0),
        getSingleColor(0x7F,0x7F,0x7F)
};

void createAndShowSquareGridFromBytes(Display* dpy, int s, Window& win, size_t gridSize, size_t squareSize, size_t bitsPerCell, uint8_t* b, size_t bLenInBytes) {

    size_t currentLeft = 0;
    size_t currentTop = 0;

    std::vector<bool> bv = bytesToBools(b,bLenInBytes);

    for (size_t i=0;i<gridSize;i++) {
        for (size_t j=0;j<gridSize;j++) {
            size_t k = i*gridSize+j;

            int color = getBitSeqFromBooleanArray(bitsPerCell*k,bitsPerCell,bv);

            XColor xc = xcolors[color];
            XAllocColor(dpy, DefaultColormap(dpy,s), &xc);
            XSetForeground(dpy, DefaultGC(dpy, s), xc.pixel);
            XFillRectangle(dpy,win,DefaultGC(dpy, s), currentTop, currentLeft, squareSize, squareSize);

            currentLeft += squareSize;
        }
        currentTop += squareSize;
        currentLeft = 0;
    }
}

void runSessionWithColorGrid(Botan::secure_vector<uint8_t> inBytes) {
    constexpr size_t squareSize = 50;
    constexpr size_t gridSize = 16;
    constexpr size_t bpp = 3;
    constexpr size_t outBitsLen = gridSize*gridSize*bpp; // 16*16 grid, 3 bpp
    std::unique_ptr<Botan::HashFunction> sponge(new Botan::SHAKE_128(outBitsLen)); // ctor accepts bits, multiply by 8
    sponge->update(&inBytes[0],inBytes.size());
    Botan::secure_vector<uint8_t> outBytes = sponge->final();

    ////////////////////////////////////////////////

    Display* dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr)
    {
        fprintf(stderr, "Cannot open display for showing visual hash\n");
        return;
    }

    int s = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, s), 0, 0, squareSize*gridSize, squareSize*gridSize, 1,
                                     BlackPixel(dpy, s), WhitePixel(dpy, s));
    XSelectInput(dpy, win, ExposureMask | KeyPressMask);
    XMapWindow(dpy, win);

    XStoreName(dpy, win, "Hashview");

    Atom WM_DELETE_WINDOW = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &WM_DELETE_WINDOW, 1);

    XEvent e;

    for(;;) {
        XNextEvent(dpy, &e);
        if (e.type == Expose) {
            createAndShowSquareGridFromBytes(dpy,s,win,gridSize,squareSize,bpp,&outBytes[0],outBytes.size());
        }

        if (e.type == KeyPress) {
            char buf[128]{};
            KeySym keysym;
            int len = XLookupString(&e.xkey, buf, sizeof buf, &keysym, nullptr);
            if (keysym == XK_Escape)
                break;
        }

        if ((e.type == ClientMessage) &&
            (static_cast<unsigned int>(e.xclient.data.l[0]) == WM_DELETE_WINDOW)) break;
    }

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    std::cout<<"Program end";
}

#endif /* _X11_HASHVIEW_ */