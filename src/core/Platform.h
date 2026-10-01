#pragma once
#ifndef FT_CORE_PLATFORM_H
#define FT_CORE_PLATFORM_H
//
// FullTerminal — Cross-Platform Core Types & Definitions
// Supports both Windows and Linux (X11 / Wayland / OpenGL)
//

#include <cstdint>
#include <string>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <deque>

#if defined(_WIN32)
    #include <windows.h>
    #include <d2d1.h>
#else
    #include <unistd.h>
    // Linux / POSIX type compatibility
    using HWND = void*;
    using HANDLE = void*;
    using HINSTANCE = void*;
    using HICON = void*;
    using HDC = void*;
    using HBITMAP = void*;
    using BOOL = int;
    using UINT = uint32_t;
    using DWORD = uint32_t;
    using BYTE = uint8_t;
    using WORD = uint16_t;
    using ULONG = uint32_t;
    using LONG = int32_t;
    using WPARAM = uintptr_t;
    using LPARAM = intptr_t;
    using LRESULT = intptr_t;
    using HRESULT = int32_t;
    using ULONG_PTR = uintptr_t;
    using UINT_PTR = uintptr_t;
    using LONG_PTR = intptr_t;
    using SHORT = int16_t;
    using HMENU = void*;

    struct LinuxWindowHandle {
        void* display = nullptr;
        uintptr_t window = 0;
        void* glContext = nullptr;
        int screen = 0;
    };

    #ifndef CALLBACK
    #define CALLBACK
    #endif
    #ifndef WINAPI
    #define WINAPI
    #endif

    #ifndef S_OK
    #define S_OK ((HRESULT)0L)
    #endif
    #ifndef SUCCEEDED
    #define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
    #endif
    #ifndef FAILED
    #define FAILED(hr) (((HRESULT)(hr)) < 0)
    #endif

    #ifndef TRUE
    #define TRUE 1
    #endif
    #ifndef FALSE
    #define FALSE 0
    #endif

    #ifndef WM_APP
    #define WM_APP 0x8000
    #endif
    #ifndef WM_USER
    #define WM_USER 0x0400
    #endif
    #ifndef WM_CLOSE
    #define WM_CLOSE 0x0010
    #endif
    #ifndef WM_SETICON
    #define WM_SETICON 0x0080
    #endif
    #ifndef ICON_SMALL
    #define ICON_SMALL 0
    #endif
    #ifndef ICON_BIG
    #define ICON_BIG 1
    #endif
    #ifndef SW_SHOW
    #define SW_SHOW 5
    #endif
    #ifndef SW_SHOWNORMAL
    #define SW_SHOWNORMAL 1
    #endif
    #ifndef SW_HIDE
    #define SW_HIDE 0
    #endif
    #ifndef SW_SHOWMAXIMIZED
    #define SW_SHOWMAXIMIZED 3
    #endif
    #ifndef SW_RESTORE
    #define SW_RESTORE 9
    #endif

    #ifndef GWL_STYLE
    #define GWL_STYLE (-16)
    #endif
    #ifndef GWL_EXSTYLE
    #define GWL_EXSTYLE (-20)
    #endif
    #ifndef WS_OVERLAPPEDWINDOW
    #define WS_OVERLAPPEDWINDOW 0x00CF0000L
    #endif
    #ifndef WS_CLIPCHILDREN
    #define WS_CLIPCHILDREN 0x02000000L
    #endif
    #ifndef WS_EX_TOOLWINDOW
    #define WS_EX_TOOLWINDOW 0x00000080L
    #endif

    #ifndef HWND_TOPMOST
    #define HWND_TOPMOST ((HWND)(intptr_t)-1)
    #endif
    #ifndef HWND_NOTOPMOST
    #define HWND_NOTOPMOST ((HWND)(intptr_t)-2)
    #endif

    #ifndef SWP_NOSIZE
    #define SWP_NOSIZE 0x0001
    #endif
    #ifndef SWP_NOMOVE
    #define SWP_NOMOVE 0x0002
    #endif
    #ifndef SWP_NOACTIVATE
    #define SWP_NOACTIVATE 0x0010
    #endif
    #ifndef SWP_FRAMECHANGED
    #define SWP_FRAMECHANGED 0x0020
    #endif

    #ifndef MONITOR_DEFAULTTONEAREST
    #define MONITOR_DEFAULTTONEAREST 2
    #endif

    #ifndef MOD_ALT
    #define MOD_ALT 0x0001
    #endif
    #ifndef MOD_CONTROL
    #define MOD_CONTROL 0x0002
    #endif
    #ifndef MOD_SHIFT
    #define MOD_SHIFT 0x0004
    #endif
    #ifndef MOD_WIN
    #define MOD_WIN 0x0008
    #endif
    #ifndef MOD_NOREPEAT
    #define MOD_NOREPEAT 0x4000
    #endif

    struct POINT {
        int x = 0;
        int y = 0;
    };

    struct RECT {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    struct MONITORINFO {
        DWORD cbSize = sizeof(MONITORINFO);
        RECT rcMonitor{};
        RECT rcWork{};
        DWORD dwFlags = 0;
    };

    struct WINDOWPLACEMENT {
        UINT length = sizeof(WINDOWPLACEMENT);
        UINT flags = 0;
        UINT showCmd = 0;
        POINT ptMinPosition{};
        POINT ptMaxPosition{};
        RECT rcNormalPosition{};
    };

    #ifndef WS_POPUP
    #define WS_POPUP 0x80000000L
    #endif
    #ifndef WS_CLIPSIBLINGS
    #define WS_CLIPSIBLINGS 0x04000000L
    #endif
    #ifndef WS_EX_NOREDIRECTIONBITMAP
    #define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
    #endif
    #ifndef WS_EX_APPWINDOW
    #define WS_EX_APPWINDOW 0x00040000L
    #endif

    #ifndef MONITOR_DEFAULTTONULL
    #define MONITOR_DEFAULTTONULL 0
    #endif
    #ifndef MONITOR_DEFAULTTOPRIMARY
    #define MONITOR_DEFAULTTOPRIMARY 1
    #endif

    #ifndef SPI_GETWORKAREA
    #define SPI_GETWORKAREA 0x0030
    #endif

    #ifndef MAPVK_VK_TO_CHAR
    #define MAPVK_VK_TO_CHAR 2
    #endif
    #ifndef MAPVK_VK_TO_VSC
    #define MAPVK_VK_TO_VSC 0
    #endif
    #ifndef MAPVK_VSC_TO_VK
    #define MAPVK_VSC_TO_VK 1
    #endif

    using HMONITOR = void*;
    using HRGN = void*;

    inline LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR) { return 0; }
    inline LONG_PTR GetWindowLongPtrW(HWND, int) { return 0; }
    inline BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return TRUE; }
    inline BOOL ShowWindow(HWND, int) { return TRUE; }
    inline BOOL SetWindowPlacement(HWND, const WINDOWPLACEMENT*) { return TRUE; }
    inline BOOL GetWindowPlacement(HWND, WINDOWPLACEMENT*) { return TRUE; }
    inline BOOL IsIconic(HWND) { return FALSE; }
    inline HWND GetForegroundWindow() { return nullptr; }
    inline DWORD GetWindowThreadProcessId(HWND, DWORD* pid) { if (pid) *pid = getpid(); return 0; }
    inline DWORD GetCurrentProcessId() { return (DWORD)getpid(); }
    inline BOOL ReleaseCapture() { return TRUE; }
    inline HWND SetCapture(HWND h) { return h; }
    inline HMONITOR MonitorFromWindow(HWND, DWORD) { return (HMONITOR)(intptr_t)1; }
    inline HMONITOR MonitorFromPoint(POINT, DWORD) { return (HMONITOR)(intptr_t)1; }
    inline BOOL GetCursorPos(POINT* pt) { if (pt) { pt->x = 0; pt->y = 0; } return TRUE; }
    inline BOOL SystemParametersInfoW(UINT, UINT, void* pvParam, UINT) {
        if (pvParam) {
            RECT* r = static_cast<RECT*>(pvParam);
            r->left = 0; r->top = 0; r->right = 1920; r->bottom = 1080;
        }
        return TRUE;
    }
    inline BOOL SetForegroundWindow(HWND) { return TRUE; }
    inline DWORD GetCurrentThreadId() { return (DWORD)pthread_self(); }
    inline BOOL AttachThreadInput(DWORD, DWORD, BOOL) { return TRUE; }
    inline BOOL BringWindowToTop(HWND) { return TRUE; }
    inline HWND SetFocus(HWND h) { return h; }
    inline BOOL CloseHandle(HANDLE) { return TRUE; }
    inline DWORD WaitForSingleObject(HANDLE, DWORD) { return 0; }
    inline UINT MapVirtualKeyW(UINT vk, UINT) { return vk; }
    inline int GetKeyNameTextW(LONG, wchar_t* lpString, int cchSize) {
        if (lpString && cchSize > 0) lpString[0] = 0;
        return 0;
    }
    inline HRGN CreateRectRgn(int, int, int, int) { return (HRGN)(intptr_t)1; }
    inline BOOL SetWindowRgn(HWND, HRGN, BOOL) { return TRUE; }
    inline BOOL DeleteObject(void*) { return TRUE; }
    inline BOOL RegisterHotKey(HWND, int, UINT, UINT) { return TRUE; }
    inline BOOL UnregisterHotKey(HWND, int) { return TRUE; }
    inline BOOL StartKeyHook(HWND, int, int) { return TRUE; }
    inline void StopKeyHook() {}

    #ifndef wcscpy_s
    inline int wcscpy_s(wchar_t* dest, size_t destsz, const wchar_t* src) {
        if (!dest || !src || destsz == 0) return -1;
        wcsncpy(dest, src, destsz - 1);
        dest[destsz - 1] = L'\0';
        return 0;
    }
    template <size_t N>
    inline int wcscpy_s(wchar_t (&dest)[N], const wchar_t* src) {
        return wcscpy_s(dest, N, src);
    }
    #endif

    struct NOTIFYICONDATAW {
        DWORD cbSize = sizeof(NOTIFYICONDATAW);
        HWND hWnd = nullptr;
        UINT uID = 0;
        UINT uFlags = 0;
        UINT uCallbackMessage = 0;
        HICON hIcon = nullptr;
        wchar_t szTip[128]{};
    };
    #ifndef NIM_ADD
    #define NIM_ADD 0x00000000
    #endif
    #ifndef NIM_MODIFY
    #define NIM_MODIFY 0x00000001
    #endif
    #ifndef NIM_DELETE
    #define NIM_DELETE 0x00000002
    #endif
    #ifndef NIF_MESSAGE
    #define NIF_MESSAGE 0x00000001
    #endif
    #ifndef NIF_ICON
    #define NIF_ICON 0x00000002
    #endif
    #ifndef NIF_TIP
    #define NIF_TIP 0x00000004
    #endif
    inline BOOL Shell_NotifyIconW(DWORD, NOTIFYICONDATAW*) { return TRUE; }

    #ifndef MF_SEPARATOR
    #define MF_SEPARATOR 0x00000800L
    #endif
    #ifndef MF_STRING
    #define MF_STRING 0x00000000L
    #endif
    #ifndef MF_CHECKED
    #define MF_CHECKED 0x00000008L
    #endif
    #ifndef MF_GRAYED
    #define MF_GRAYED 0x00000001L
    #endif
    #ifndef MF_POPUP
    #define MF_POPUP 0x00000010L
    #endif
    #ifndef TPM_RETURNCMD
    #define TPM_RETURNCMD 0x0100L
    #endif
    #ifndef TPM_LEFTALIGN
    #define TPM_LEFTALIGN 0x0000L
    #endif
    #ifndef TPM_TOPALIGN
    #define TPM_TOPALIGN 0x0000L
    #endif
    #ifndef TPM_RIGHTBUTTON
    #define TPM_RIGHTBUTTON 0x0002L
    #endif

    #ifndef _TRUNCATE
    #define _TRUNCATE ((size_t)-1)
    #endif

    inline int wcsncpy_s(wchar_t* dest, size_t destsz, const wchar_t* src, size_t count) {
        if (!dest || !src || destsz == 0) return -1;
        size_t n = (count == (size_t)-1 || count >= destsz) ? (destsz - 1) : count;
        wcsncpy(dest, src, n);
        dest[n] = L'\0';
        return 0;
    }
    template <size_t N>
    inline int wcsncpy_s(wchar_t (&dest)[N], const wchar_t* src, size_t count) {
        return wcsncpy_s(dest, N, src, count);
    }

    inline int strncpy_s(char* dest, size_t destsz, const char* src, size_t count) {
        if (!dest || !src || destsz == 0) return -1;
        size_t n = (count == (size_t)-1 || count >= destsz) ? (destsz - 1) : count;
        strncpy(dest, src, n);
        dest[n] = '\0';
        return 0;
    }
    template <size_t N>
    inline int strncpy_s(char (&dest)[N], const char* src, size_t count) {
        return strncpy_s(dest, N, src, count);
    }

    #ifndef IDI_APPLICATION
    #define IDI_APPLICATION ((const wchar_t*)(intptr_t)32512)
    #endif
    inline HICON LoadIconW(HINSTANCE, const wchar_t*) { return (HICON)(intptr_t)1; }

    inline DWORD GetTempPathW(DWORD nBufferLength, wchar_t* lpBuffer) {
        const wchar_t* tmp = L"/tmp/";
        if (lpBuffer && nBufferLength > 5) {
            wcscpy(lpBuffer, tmp);
            return 5;
        }
        return 0;
    }

    inline BOOL GetClientRect(HWND, RECT* rc) {
        if (rc) { rc->left = 0; rc->top = 0; rc->right = 1920; rc->bottom = 1080; }
        return TRUE;
    }

    #ifndef MB_OK
    #define MB_OK 0x00000000L
    #endif
    inline BOOL MessageBeep(UINT) { return TRUE; }
    inline BOOL IsWindowVisible(HWND) { return TRUE; }
    inline BOOL DeleteFileW(const wchar_t* p) {
        std::error_code ec;
        return std::filesystem::remove(p, ec);
    }

    inline HMENU CreatePopupMenu() { return (HMENU)(intptr_t)1; }
    inline BOOL AppendMenuW(HMENU, UINT, UINT_PTR, const wchar_t*) { return TRUE; }
    inline BOOL DestroyMenu(HMENU) { return TRUE; }
    inline UINT TrackPopupMenuEx(HMENU, UINT, int, int, HWND, void*) { return 0; }

    #ifndef WM_COMMAND
    #define WM_COMMAND 0x0111
    #endif

    #ifndef WHEEL_DELTA
    #define WHEEL_DELTA 120
    #endif
    #ifndef SPI_GETWHEELSCROLLLINES
    #define SPI_GETWHEELSCROLLLINES 0x0068
    #endif
    #ifndef WHEEL_PAGESCROLL
    #define WHEEL_PAGESCROLL ((UINT)-1)
    #endif

    #ifndef MB_ICONWARNING
    #define MB_ICONWARNING 0x00000030L
    #endif
    #ifndef MB_OKCANCEL
    #define MB_OKCANCEL 0x00000001L
    #endif
    #ifndef MB_DEFBUTTON2
    #define MB_DEFBUTTON2 0x00000100L
    #endif
    #ifndef IDOK
    #define IDOK 1
    #endif
    #ifndef IDCANCEL
    #define IDCANCEL 2
    #endif

    inline int MessageBoxW(HWND, const wchar_t*, const wchar_t*, UINT) { return IDOK; }

    inline BOOL GetMonitorInfoW(HMONITOR, MONITORINFO* mi) {
        if (mi) {
            mi->rcMonitor = { 0, 0, 1920, 1080 };
            mi->rcWork = { 0, 0, 1920, 1080 };
        }
        return TRUE;
    }

    struct COPYDATASTRUCT {
        ULONG_PTR dwData = 0;
        DWORD cbData = 0;
        void* lpData = nullptr;
    };

    struct SYSTEMTIME {
        WORD wYear = 0;
        WORD wMonth = 0;
        WORD wDayOfWeek = 0;
        WORD wDay = 0;
        WORD wHour = 0;
        WORD wMinute = 0;
        WORD wSecond = 0;
        WORD wMilliseconds = 0;
    };

    inline void GetLocalTime(SYSTEMTIME* st) {
        if (!st) return;
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&tt, &tm);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        st->wYear = (WORD)(tm.tm_year + 1900);
        st->wMonth = (WORD)(tm.tm_mon + 1);
        st->wDayOfWeek = (WORD)tm.tm_wday;
        st->wDay = (WORD)tm.tm_mday;
        st->wHour = (WORD)tm.tm_hour;
        st->wMinute = (WORD)tm.tm_min;
        st->wSecond = (WORD)tm.tm_sec;
        st->wMilliseconds = (WORD)ms.count();
    }

    #ifndef INVALID_HANDLE_VALUE
    #define INVALID_HANDLE_VALUE ((void*)(intptr_t)-1)
    #endif

    #ifndef MAX_PATH
    #define MAX_PATH 4096
    #endif

    #include <cwchar>
    #include <cwctype>
    #include <cstdlib>
    #include <unistd.h>
    #include <filesystem>

    #ifndef swprintf_s
    #define swprintf_s(buf, fmt, ...) swprintf(buf, sizeof(buf)/sizeof(wchar_t), fmt, ##__VA_ARGS__)
    #endif

    inline BOOL ClientToScreen(HWND, POINT*) { return TRUE; }
    inline BOOL IsZoomed(HWND) { return FALSE; }

    struct LinuxMsg {
        HWND hwnd = nullptr;
        UINT msg = 0;
        WPARAM wp = 0;
        LPARAM lp = 0;
    };
    inline std::mutex g_msgMutex;
    inline std::deque<LinuxMsg> g_msgQueue;

    inline BOOL PostMessageW(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        std::lock_guard<std::mutex> lock(g_msgMutex);
        g_msgQueue.push_back({ hwnd, msg, wp, lp });
        return TRUE;
    }
    inline BOOL LinuxPopMessage(LinuxMsg& out) {
        std::lock_guard<std::mutex> lock(g_msgMutex);
        if (g_msgQueue.empty()) return FALSE;
        out = g_msgQueue.front();
        g_msgQueue.pop_front();
        return TRUE;
    }

    inline LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM) { return 0; }
    inline BOOL DestroyIcon(HICON) { return TRUE; }

    inline BOOL CreateDirectoryW(const wchar_t* path, void*) {
        std::error_code ec;
        return std::filesystem::create_directories(std::filesystem::path(path), ec);
    }

    inline DWORD GetModuleFileNameW(void*, wchar_t* buf, DWORD sz) {
        char link[4096]{};
        ssize_t len = readlink("/proc/self/exe", link, sizeof(link) - 1);
        if (len <= 0) return 0;
        link[len] = '\0';
        size_t wlen = mbstowcs(buf, link, sz);
        if (wlen == (size_t)-1) return 0;
        return (DWORD)wlen;
    }

    inline HINSTANCE ShellExecuteW(HWND, const wchar_t*, const wchar_t* file, const wchar_t*, const wchar_t*, int) {
        if (!file) return nullptr;
        char mb[4096]{};
        wcstombs(mb, file, sizeof(mb) - 1);
        pid_t pid = fork();
        if (pid == 0) {
            execlp("xdg-open", "xdg-open", mb, nullptr);
            _exit(1);
        }
        return (HINSTANCE)(intptr_t)42;
    }

    #ifndef _wcsicmp
    inline int _wcsicmp(const wchar_t* s1, const wchar_t* s2) {
        return wcscasecmp(s1, s2);
    }
    #endif

    #ifndef _wtoi
    inline int _wtoi(const wchar_t* str) {
        return (int)wcstol(str, nullptr, 10);
    }
    #endif

    #ifndef _wtoi64
    inline int64_t _wtoi64(const wchar_t* str) {
        return (int64_t)wcstoll(str, nullptr, 10);
    }
    #endif

    #ifndef StrStrIW
    inline const wchar_t* StrStrIW(const wchar_t* str, const wchar_t* target) {
        if (!str || !target) return nullptr;
        if (!*target) return str;
        size_t targetLen = wcslen(target);
        for (; *str; ++str) {
            if (wcsncasecmp(str, target, targetLen) == 0) {
                return str;
            }
        }
        return nullptr;
    }
    #endif

    #ifndef GetTickCount64
    inline uint64_t GetTickCount64() {
        return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    #endif

    #ifndef SecureZeroMemory
    inline void SecureZeroMemory(void* ptr, size_t cnt) {
        volatile unsigned char* p = (volatile unsigned char*)ptr;
        while (cnt--) *p++ = 0;
    }
    #endif

    // Direct2D compatibility struct for seamless layout calculation
    struct D2D1_RECT_F {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    struct D2D1_COLOR_F {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    namespace D2D1 {
        inline D2D1_RECT_F RectF(float l, float t, float r, float b) {
            return D2D1_RECT_F{ l, t, r, b };
        }
        inline D2D1_COLOR_F ColorF(float r, float g, float b, float a = 1.0f) {
            return D2D1_COLOR_F{ r, g, b, a };
        }
        inline D2D1_COLOR_F ColorF(uint32_t rgb, float a = 1.0f) {
            float red   = ((rgb >> 16) & 0xFF) / 255.0f;
            float green = ((rgb >> 8)  & 0xFF) / 255.0f;
            float blue  = (rgb         & 0xFF) / 255.0f;
            return D2D1_COLOR_F{ red, green, blue, a };
        }
    }

    // Virtual Key Code definitions for Linux event mapping
    enum LinuxVirtualKeys : int {
        VK_LBUTTON    = 0x01,
        VK_RBUTTON    = 0x02,
        VK_CANCEL     = 0x03,
        VK_MBUTTON    = 0x04,
        VK_BACK       = 0x08,
        VK_TAB        = 0x09,
        VK_CLEAR      = 0x0C,
        VK_RETURN     = 0x0D,
        VK_SHIFT      = 0x10,
        VK_CONTROL    = 0x11,
        VK_MENU       = 0x12, // Alt
        VK_PAUSE      = 0x13,
        VK_CAPITAL    = 0x14, // Caps Lock
        VK_ESCAPE     = 0x1B,
        VK_SPACE      = 0x20,
        VK_PRIOR      = 0x21, // Page Up
        VK_NEXT       = 0x22, // Page Down
        VK_END        = 0x23,
        VK_HOME       = 0x24,
        VK_LEFT       = 0x25,
        VK_UP         = 0x26,
        VK_RIGHT      = 0x27,
        VK_DOWN       = 0x28,
        VK_SELECT     = 0x29,
        VK_PRINT      = 0x2A,
        VK_EXECUTE    = 0x2B,
        VK_SNAPSHOT   = 0x2C, // Print Screen
        VK_INSERT     = 0x2D,
        VK_DELETE     = 0x2E,
        VK_HELP       = 0x2F,

        VK_LWIN       = 0x5B,
        VK_RWIN       = 0x5C,
        VK_APPS       = 0x5D,
        VK_NUMPAD0    = 0x60,
        VK_NUMPAD1    = 0x61,
        VK_NUMPAD2    = 0x62,
        VK_NUMPAD3    = 0x63,
        VK_NUMPAD4    = 0x64,
        VK_NUMPAD5    = 0x65,
        VK_NUMPAD6    = 0x66,
        VK_NUMPAD7    = 0x67,
        VK_NUMPAD8    = 0x68,
        VK_NUMPAD9    = 0x69,
        VK_MULTIPLY   = 0x6A,
        VK_ADD        = 0x6B,
        VK_SEPARATOR  = 0x6C,
        VK_SUBTRACT   = 0x6D,
        VK_DECIMAL    = 0x6E,
        VK_DIVIDE     = 0x6F,

        VK_F1         = 0x70,
        VK_F2         = 0x71,
        VK_F3         = 0x72,
        VK_F4         = 0x73,
        VK_F5         = 0x74,
        VK_F6         = 0x75,
        VK_F7         = 0x76,
        VK_F8         = 0x77,
        VK_F9         = 0x78,
        VK_F10        = 0x79,
        VK_F11        = 0x7A,
        VK_F12        = 0x7B,
        VK_F13        = 0x7C,
        VK_F14        = 0x7D,
        VK_F15        = 0x7E,
        VK_F16        = 0x7F,
        VK_F17        = 0x80,
        VK_F18        = 0x81,
        VK_F19        = 0x82,
        VK_F20        = 0x83,
        VK_F21        = 0x84,
        VK_F22        = 0x85,
        VK_F23        = 0x86,
        VK_F24        = 0x87,

        VK_NUMLOCK    = 0x90,
        VK_SCROLL     = 0x91,
        VK_LSHIFT     = 0xA0,
        VK_RSHIFT     = 0xA1,
        VK_LCONTROL   = 0xA2,
        VK_RCONTROL   = 0xA3,
        VK_LMENU      = 0xA4,
        VK_RMENU      = 0xA5,
    };

    inline int g_linuxKeyModifiers = 0;
    inline void LinuxSetModifiers(int mods) { g_linuxKeyModifiers = mods; }
    inline SHORT GetKeyState(int vk) {
        int m = g_linuxKeyModifiers;
        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) {
            return (m & MOD_CONTROL) ? (SHORT)0x8000 : 0;
        }
        if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) {
            return (m & MOD_SHIFT) ? (SHORT)0x8000 : 0;
        }
        if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) {
            return (m & MOD_ALT) ? (SHORT)0x8000 : 0;
        }
        if (vk == VK_LWIN || vk == VK_RWIN) {
            return (m & MOD_WIN) ? (SHORT)0x8000 : 0;
        }
        return 0;
    }
#endif

namespace ft {

inline int64_t PlatformNowTicks() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline int64_t PlatformTickFreq() {
    return 1000000; // microseconds per second
}

} // namespace ft

#endif // FT_CORE_PLATFORM_H
