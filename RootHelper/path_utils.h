#ifndef __RH_PATH_UTILS__
#define __RH_PATH_UTILS__

#include "unifiedlogging.h"
#include <algorithm>

// PGP
std::string wchar_to_UTF8(const std::wstring& in_) {
    const wchar_t* in = in_.c_str();
    std::string out;
    unsigned int codepoint = 0;
    for (;*in != 0;++in)
    {
        if (*in >= 0xd800 && *in <= 0xdbff)
            codepoint = ((*in - 0xd800) << 10) + 0x10000;
        else
        {
            if (*in >= 0xdc00 && *in <= 0xdfff)
                codepoint |= *in - 0xdc00;
            else
                codepoint = *in;

            if (codepoint <= 0x7f)
                out.append(1, static_cast<char>(codepoint));
            else if (codepoint <= 0x7ff)
            {
                out.append(1, static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f)));
                out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
            }
            else if (codepoint <= 0xffff)
            {
                out.append(1, static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f)));
                out.append(1, static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
            }
            else
            {
                out.append(1, static_cast<char>(0xf0 | ((codepoint >> 18) & 0x07)));
                out.append(1, static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
                out.append(1, static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
            }
            codepoint = 0;
        }
    }
    return out;
}

std::wstring UTF8_to_wchar(const std::string& in_) {
    const char* in = in_.c_str();
    std::wstring out;
    unsigned int codepoint;
    while (*in != 0)
    {
        auto ch = static_cast<unsigned char>(*in);
        if (ch <= 0x7f)
            codepoint = ch;
        else if (ch <= 0xbf)
            codepoint = (codepoint << 6) | (ch & 0x3f);
        else if (ch <= 0xdf)
            codepoint = ch & 0x1f;
        else if (ch <= 0xef)
            codepoint = ch & 0x0f;
        else
            codepoint = ch & 0x07;
        ++in;
        if (((*in & 0xc0) != 0x80) && (codepoint <= 0x10ffff))
        {
            if (sizeof(wchar_t) > 2)
                out.append(1, static_cast<wchar_t>(codepoint));
            else if (codepoint > 0xffff)
            {
                out.append(1, static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
                out.append(1, static_cast<wchar_t>(0xdc00 + (codepoint & 0x03ff)));
            }
            else if (codepoint < 0xd800 || codepoint >= 0xe000)
                out.append(1, static_cast<wchar_t>(codepoint));
        }
    }
    return out;
}
// PGP

/********************* Windows-only ************************/
#ifdef _WIN32
std::wstring unixToWindowsPath(std::string s) {
    if(s.empty()) return L"";
    if (s[0] != '/') {
        PRINTUNIFIEDERROR("'/' expected in Unix-to-Windows conversion path\n");
        exit(-1);
    }
    const char* s_ = s.c_str();
    std::string rpl(s_+1);
    std::replace(rpl.begin(),rpl.end(),'/','\\');

    return UTF8_to_wchar(rpl);
}

std::string windowsToUnixPath(const std::wstring& ws) {
    if(ws.empty()) return "";
    std::string s = std::string("/") + wchar_to_UTF8(ws);
    std::replace(s.begin(),s.end(),'\\','/');
    return s;
}
#endif
/*********************************************/

#ifdef _WIN32
#define FROMUNIXPATH(s) (unixToWindowsPath((s)))
#define TOUNIXPATH(s) (windowsToUnixPath((s)))
#define FROMUTF(s) (UTF8_to_wchar(s))
#define TOUTF(s) (wchar_to_UTF8(s))
#else
#define FROMUNIXPATH(s) (s)
#define TOUNIXPATH(s) (s)
#define FROMUTF(s) (s)
#define TOUTF(s) (s)
#endif

#ifdef _WIN32
std::wstring pathConcat(const std::wstring& dir, const std::wstring& filename) {
    wchar_t last = dir[dir.size()-1];
    if (last == L'\\') return dir + filename;
    else return dir + std::wstring(L"\\") + filename;
}
#else
std::string pathConcat(const std::string& dir, const std::string& filename) {
    if (dir.back()=='/') // non-standard path, ending with '/'
        return dir + filename;
    else return dir+"/"+filename;
}
#endif

#endif /* __RH_PATH_UTILS__ */