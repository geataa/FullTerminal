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
#include <vector>

// Dosya yolu ayirici (L"" literal'lerine yapistirilabilir: m_dataDir + FT_PATH_SEP L"x.json")
#if defined(_WIN32)
    #define FT_PATH_SEP L"\\"
#else
    #define FT_PATH_SEP L"/"
#endif

#if defined(_WIN32)
    #include <windows.h>
    #include <d2d1.h>
#else
    #include <unistd.h>
    #include <poll.h>
    #include <cerrno>
    #include <sys/eventfd.h>
    #include <memory>
    #include <atomic>
    #include <thread>
    #include <unordered_map>
    #include <algorithm>
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
    using ULONGLONG = uint64_t;
    using LONGLONG = int64_t;
    using DWORD_PTR = uintptr_t;

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

    #ifndef VK_OEM_1
    #define VK_OEM_1      0xBA
    #define VK_OEM_PLUS   0xBB
    #define VK_OEM_COMMA  0xBC
    #define VK_OEM_MINUS  0xBD
    #define VK_OEM_PERIOD 0xBE
    #define VK_OEM_2      0xBF
    #define VK_OEM_3      0xC0
    #define VK_OEM_4      0xDB
    #define VK_OEM_5      0xDC
    #define VK_OEM_6      0xDD
    #define VK_OEM_7      0xDE
    #endif

    #ifndef OFN_ALLOWMULTISELECT
    #define OFN_ALLOWMULTISELECT 0x00000200
    #endif
    #ifndef OFN_EXPLORER
    #define OFN_EXPLORER         0x00080000
    #endif

    inline void DragAcceptFiles(HWND, BOOL) {}

    using HMONITOR = void*;
    using HRGN = void*;

    #ifndef SW_SHOWMINIMIZED
    #define SW_SHOWMINIMIZED 2
    #endif
    #ifndef SW_MAXIMIZE
    #define SW_MAXIMIZE 3
    #endif
    #ifndef SW_MINIMIZE
    #define SW_MINIMIZE 6
    #endif
    #ifndef SWP_NOZORDER
    #define SWP_NOZORDER 0x0004
    #endif
    #ifndef GWLP_USERDATA
    #define GWLP_USERDATA (-21)
    #endif
    #ifndef CS_DBLCLKS
    #define CS_DBLCLKS 0x0008
    #endif
    #ifndef CW_USEDEFAULT
    #define CW_USEDEFAULT ((int)0x80000000)
    #endif

    #ifndef WM_NCCREATE
    #define WM_NCCREATE 0x0081
    #define WM_ACTIVATE 0x0006
    #define WA_INACTIVE 0
    #define WM_SETFOCUS 0x0007
    #define WM_KILLFOCUS 0x0008
    #define WM_ACTIVATEAPP 0x001C
    #define WM_DESTROY 0x0002
    #define WM_SIZE 0x0005
    #define WM_KEYDOWN 0x0100
    #define WM_KEYUP 0x0101
    #define WM_CHAR 0x0102
    #define WM_SYSKEYDOWN 0x0104
    #define WM_SYSKEYUP 0x0105
    #define WM_SYSCHAR 0x0106
    #define WM_MOUSEMOVE 0x0200
    #define WM_LBUTTONDOWN 0x0201
    #define WM_LBUTTONUP 0x0202
    #define WM_LBUTTONDBLCLK 0x0203
    #define WM_RBUTTONDOWN 0x0204
    #define WM_RBUTTONUP 0x0205
    #define WM_MBUTTONDOWN 0x0207
    #define WM_MBUTTONUP 0x0208
    #define WM_MOUSEWHEEL 0x020A
    #define WM_CAPTURECHANGED 0x0215
    #define WM_MOUSELEAVE 0x02A3
    #define WM_HOTKEY 0x0312
    #endif

    #ifndef MK_LBUTTON
    #define MK_LBUTTON 0x0001
    #define MK_RBUTTON 0x0002
    #define MK_SHIFT   0x0004
    #define MK_CONTROL 0x0008
    #define MK_MBUTTON 0x0010
    #endif

    #ifndef LOWORD
    #define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
    #define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
    #endif
    #ifndef MAKELPARAM
    #define MAKELPARAM(l, h) ((LPARAM)(DWORD)(((WORD)((DWORD_PTR)(l) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(h) & 0xffff))) << 16))
    #endif
    #ifndef GET_X_LPARAM
    #define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
    #define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
    #endif
    #ifndef GET_WHEEL_DELTA_WPARAM
    #define GET_WHEEL_DELTA_WPARAM(wp) ((short)HIWORD(wp))
    #define GET_KEYSTATE_WPARAM(wp)    (LOWORD(wp))
    #endif

    struct SIZE {
        LONG cx = 0;
        LONG cy = 0;
    };

    struct LinuxRegion {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    using WNDPROC = LRESULT (CALLBACK *)(HWND, UINT, WPARAM, LPARAM);
    struct WNDCLASSW {
        UINT style = 0;
        WNDPROC lpfnWndProc = nullptr;
        int cbClsExtra = 0;
        int cbWndExtra = 0;
        HINSTANCE hInstance = nullptr;
        HICON hIcon = nullptr;
        void* hCursor = nullptr;
        void* hbrBackground = nullptr;
        const wchar_t* lpszMenuName = nullptr;
        const wchar_t* lpszClassName = nullptr;
    };
    struct CREATESTRUCTW {
        void* lpCreateParams = nullptr;
        HINSTANCE hInstance = nullptr;
        HMENU hMenu = nullptr;
        HWND hwndParent = nullptr;
        int cy = 0;
        int cx = 0;
        int y = 0;
        int x = 0;
        LONG style = 0;
        const wchar_t* lpszName = nullptr;
        const wchar_t* lpszClass = nullptr;
        DWORD dwExStyle = 0;
    };

    struct TRACKMOUSEEVENT {
        DWORD cbSize = sizeof(TRACKMOUSEEVENT);
        DWORD dwFlags = 0;
        HWND hwndTrack = nullptr;
        DWORD dwHoverTime = 0;
    };
    #ifndef TME_LEAVE
    #define TME_LEAVE 0x00000002
    #endif
    inline BOOL TrackMouseEvent(TRACKMOUSEEVENT*) { return TRUE; }

    #ifndef SEM_FAILCRITICALERRORS
    #define SEM_FAILCRITICALERRORS 0x0001
    #define SEM_NOGPFAULTERRORBOX  0x0002
    #endif
    inline UINT SetErrorMode(UINT) { return 0; }
    #ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    #define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((void*)(intptr_t)-4)
    #endif
    inline BOOL SetProcessDpiAwarenessContext(void*) { return TRUE; }
    #ifndef COINIT_APARTMENTTHREADED
    #define COINIT_APARTMENTTHREADED 0x2
    #define COINIT_DISABLE_OLE1DDE   0x4
    #endif
    inline HRESULT CoInitializeEx(void*, DWORD) { return S_OK; }
    inline void CoUninitialize() {}
    inline HINSTANCE GetModuleHandleW(const wchar_t*) { return (HINSTANCE)(intptr_t)1; }
    inline LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return 0; }

    // ------------------------------------------------------------------
    // Linux platform kancalari
    //
    // Win32 benzeri API'ler (ShowWindow, MessageBoxW, TrackPopupMenuEx,
    // Shell_NotifyIconW ...) burada yalnizca ince sarmalayicidir. Gercek X11
    // uygulamasi GUI ikilisine baglanan platform/linux/LinuxNative.cpp
    // tarafindan calisma aninda kaydedilir. Kanca yoksa (birim testleri,
    // --mcp / --daemon gibi GUI'siz kipler) zararsiz varsayilan davranis kalir.
    // ------------------------------------------------------------------
    struct NOTIFYICONDATAW;

    // HICON Linux'ta bu yapiya isaret eder (ARGB, on carpimsiz).
    struct LinuxIcon {
        int size = 0;
        std::vector<uint32_t> argb;   // size*size, 0xAARRGGBB
        bool owned = true;            // DestroyIcon silebilir mi
    };

    struct LinuxMenuItem {
        UINT flags = 0;
        UINT_PTR id = 0;              // MF_POPUP ise alt menu (HMENU)
        std::wstring text;
    };
    struct LinuxMenu {
        std::vector<LinuxMenuItem> items;
    };

    struct LinuxPlatformHooks {
        BOOL (*showWindow)(HWND, int) = nullptr;
        BOOL (*isIconic)(HWND) = nullptr;
        BOOL (*isWindowVisible)(HWND) = nullptr;
        BOOL (*isZoomed)(HWND) = nullptr;
        BOOL (*getWindowPlacement)(HWND, WINDOWPLACEMENT*) = nullptr;
        BOOL (*setWindowPlacement)(HWND, const WINDOWPLACEMENT*) = nullptr;
        BOOL (*setForegroundWindow)(HWND) = nullptr;
        HWND (*getForegroundWindow)() = nullptr;
        BOOL (*getCursorPos)(POINT*) = nullptr;
        BOOL (*clientToScreen)(HWND, POINT*) = nullptr;
        BOOL (*getClientRect)(HWND, RECT*) = nullptr;
        HMONITOR (*monitorFromPoint)(POINT, DWORD) = nullptr;
        HMONITOR (*monitorFromWindow)(HWND, DWORD) = nullptr;
        BOOL (*getMonitorInfo)(HMONITOR, MONITORINFO*) = nullptr;
        BOOL (*getWorkArea)(RECT*) = nullptr;
        void (*messageBeep)(UINT) = nullptr;
        BOOL (*flashWindow)(HWND, BOOL) = nullptr;
        int  (*messageBox)(HWND, const wchar_t*, const wchar_t*, UINT) = nullptr;
        UINT (*trackPopupMenu)(HMENU, UINT, int, int, HWND) = nullptr;
        BOOL (*notifyIcon)(DWORD, NOTIFYICONDATAW*) = nullptr;
        LRESULT (*sendMessage)(HWND, UINT, WPARAM, LPARAM) = nullptr;
        HICON (*loadAppIcon)() = nullptr;
        BOOL (*registerHotKey)(HWND, int, UINT, UINT) = nullptr;
        BOOL (*unregisterHotKey)(HWND, int) = nullptr;
        BOOL (*fileDialog)(struct OPENFILENAMEW*, bool save) = nullptr;
        BOOL (*setWindowPos)(HWND, HWND, int, int, int, int, UINT) = nullptr;
        LONG_PTR (*setWindowLongPtr)(HWND, int, LONG_PTR) = nullptr;
        LONG_PTR (*getWindowLongPtr)(HWND, int) = nullptr;
        BOOL (*setWindowRgn)(HWND, HRGN, BOOL) = nullptr;
        HWND (*createWindowEx)(DWORD, const wchar_t*, const wchar_t*, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void*) = nullptr;
        BOOL (*destroyWindow)(HWND) = nullptr;
    };
    namespace ft::lnx { void InstallHooks(); }

    inline LinuxPlatformHooks g_linuxHooks{};
    inline WNDPROC g_linuxLastWndProc = nullptr;

    inline WORD RegisterClassW(const WNDCLASSW* wc) {
        if (!g_linuxHooks.createWindowEx) ft::lnx::InstallHooks();
        if (wc && wc->lpfnWndProc) g_linuxLastWndProc = wc->lpfnWndProc;
        return 1;
    }
    inline LONG_PTR SetWindowLongPtrW(HWND h, int idx, LONG_PTR val) {
        return g_linuxHooks.setWindowLongPtr ? g_linuxHooks.setWindowLongPtr(h, idx, val) : 0;
    }
    inline LONG_PTR GetWindowLongPtrW(HWND h, int idx) {
        return g_linuxHooks.getWindowLongPtr ? g_linuxHooks.getWindowLongPtr(h, idx) : 0;
    }
    inline BOOL SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
        return g_linuxHooks.setWindowPos ? g_linuxHooks.setWindowPos(h, after, x, y, cx, cy, flags) : TRUE;
    }
    inline HWND CreateWindowExW(DWORD exStyle, const wchar_t* cls, const wchar_t* title, DWORD style,
                                int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, void* param) {
        if (!g_linuxHooks.createWindowEx) ft::lnx::InstallHooks();
        return g_linuxHooks.createWindowEx
            ? g_linuxHooks.createWindowEx(exStyle, cls, title, style, x, y, w, h, parent, menu, inst, param)
            : nullptr;
    }
    inline BOOL DestroyWindow(HWND h) {
        return g_linuxHooks.destroyWindow ? g_linuxHooks.destroyWindow(h) : TRUE;
    }
    inline BOOL ShowWindow(HWND h, int cmd) {
        return g_linuxHooks.showWindow ? g_linuxHooks.showWindow(h, cmd) : TRUE;
    }
    inline BOOL SetWindowPlacement(HWND h, const WINDOWPLACEMENT* wp) {
        return g_linuxHooks.setWindowPlacement ? g_linuxHooks.setWindowPlacement(h, wp) : TRUE;
    }
    inline BOOL GetWindowPlacement(HWND h, WINDOWPLACEMENT* wp) {
        return g_linuxHooks.getWindowPlacement ? g_linuxHooks.getWindowPlacement(h, wp) : TRUE;
    }
    inline BOOL IsIconic(HWND h) {
        return g_linuxHooks.isIconic ? g_linuxHooks.isIconic(h) : FALSE;
    }
    inline HWND GetForegroundWindow() {
        return g_linuxHooks.getForegroundWindow ? g_linuxHooks.getForegroundWindow() : nullptr;
    }
    inline DWORD GetWindowThreadProcessId(HWND, DWORD* pid) { if (pid) *pid = getpid(); return 0; }
    inline DWORD GetCurrentProcessId() { return (DWORD)getpid(); }
    inline BOOL ReleaseCapture() { return TRUE; }
    inline HWND SetCapture(HWND h) { return h; }
    inline HMONITOR MonitorFromWindow(HWND h, DWORD f) {
        return g_linuxHooks.monitorFromWindow ? g_linuxHooks.monitorFromWindow(h, f) : (HMONITOR)(intptr_t)1;
    }
    inline HMONITOR MonitorFromPoint(POINT pt, DWORD f) {
        return g_linuxHooks.monitorFromPoint ? g_linuxHooks.monitorFromPoint(pt, f) : (HMONITOR)(intptr_t)1;
    }
    inline BOOL GetCursorPos(POINT* pt) {
        if (g_linuxHooks.getCursorPos) return g_linuxHooks.getCursorPos(pt);
        if (pt) { pt->x = 0; pt->y = 0; }
        return TRUE;
    }
    inline BOOL SystemParametersInfoW(UINT action, UINT, void* pvParam, UINT) {
        if (pvParam && action == SPI_GETWORKAREA) {
            RECT* r = static_cast<RECT*>(pvParam);
            if (g_linuxHooks.getWorkArea && g_linuxHooks.getWorkArea(r)) return TRUE;
            r->left = 0; r->top = 0; r->right = 1920; r->bottom = 1080;
        }
        return TRUE;
    }
    inline BOOL SetForegroundWindow(HWND h) {
        return g_linuxHooks.setForegroundWindow ? g_linuxHooks.setForegroundWindow(h) : TRUE;
    }
    inline DWORD GetCurrentThreadId() { return (DWORD)pthread_self(); }
    inline BOOL AttachThreadInput(DWORD, DWORD, BOOL) { return TRUE; }
    inline BOOL BringWindowToTop(HWND h) {
        return g_linuxHooks.setForegroundWindow ? g_linuxHooks.setForegroundWindow(h) : TRUE;
    }
    inline HWND SetFocus(HWND h) { return h; }
    // ---- Cekirdek nesnesi emulasyonu (event, pipe, mutex) ------------------
    // Windows ile paylasilan kod HANDLE uzerinden calisiyor. Linux'ta gercek
    // nesneler bu tabloda shared_ptr ile tutulur; CloseHandle tablodan cikarir,
    // baska bir thread hala kullaniyorsa nesne son referansla birlikte yok olur.
    // Tabloda olmayan (eski sahte) handle'lar icin davranis eskisi gibi no-op.
    struct LinuxKernelObject {
        virtual ~LinuxKernelObject() = default;
        virtual DWORD Wait(DWORD /*ms*/) { return 0; }      // WAIT_OBJECT_0
        virtual int PollFd() const { return -1; }           // poll() ile beklenebilir fd
    };
    inline std::mutex& LinuxHandleMutex() { static std::mutex m; return m; }
    inline std::unordered_map<void*, std::shared_ptr<LinuxKernelObject>>& LinuxHandleTable() {
        static std::unordered_map<void*, std::shared_ptr<LinuxKernelObject>> t;
        return t;
    }
    inline HANDLE LinuxRegisterHandle(std::shared_ptr<LinuxKernelObject> obj) {
        if (!obj) return nullptr;
        void* key = obj.get();
        std::lock_guard<std::mutex> lock(LinuxHandleMutex());
        LinuxHandleTable()[key] = std::move(obj);
        return key;
    }
    template <class T>
    inline std::shared_ptr<T> LinuxHandleAs(HANDLE h) {
        if (!h) return nullptr;
        std::lock_guard<std::mutex> lock(LinuxHandleMutex());
        auto it = LinuxHandleTable().find(h);
        if (it == LinuxHandleTable().end()) return nullptr;
        return std::dynamic_pointer_cast<T>(it->second);
    }
    inline BOOL CloseHandle(HANDLE h) {
        std::shared_ptr<LinuxKernelObject> victim;
        {
            std::lock_guard<std::mutex> lock(LinuxHandleMutex());
            auto it = LinuxHandleTable().find(h);
            if (it != LinuxHandleTable().end()) { victim = std::move(it->second); LinuxHandleTable().erase(it); }
        }
        return TRUE; // victim kilit disinda serbest kalir
    }
    #ifndef INFINITE
    #define INFINITE 0xFFFFFFFFu
    #endif
    #ifndef WAIT_OBJECT_0
    #define WAIT_OBJECT_0 0u
    #endif
    #ifndef WAIT_TIMEOUT
    #define WAIT_TIMEOUT 258u
    #endif
    #ifndef WAIT_FAILED
    #define WAIT_FAILED 0xFFFFFFFFu
    #endif
    inline DWORD WaitForSingleObject(HANDLE h, DWORD ms) {
        auto obj = LinuxHandleAs<LinuxKernelObject>(h);
        return obj ? obj->Wait(ms) : 0;
    }

    // Win32 event (eventfd tabanli; poll() ile soketlerle birlikte beklenebilir)
    struct LinuxEvent : LinuxKernelObject {
        int fd = -1;
        bool manualReset = true;
        std::mutex mtx;
        bool signaled = false;
        LinuxEvent(bool manual, bool initial) : manualReset(manual) {
            fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
            if (initial) Set();
        }
        ~LinuxEvent() override { if (fd >= 0) ::close(fd); }
        void Set() {
            std::lock_guard<std::mutex> lock(mtx);
            if (signaled) return;
            signaled = true;
            uint64_t one = 1;
            if (fd >= 0) { ssize_t r = ::write(fd, &one, sizeof(one)); (void)r; }
        }
        void ResetLocked() {
            signaled = false;
            uint64_t v = 0;
            if (fd >= 0) { ssize_t r = ::read(fd, &v, sizeof(v)); (void)r; }
        }
        void Reset() { std::lock_guard<std::mutex> lock(mtx); ResetLocked(); }
        bool TryConsume() {
            std::lock_guard<std::mutex> lock(mtx);
            if (!signaled) return false;
            if (!manualReset) ResetLocked();
            return true;
        }
        int PollFd() const override { return fd; }
        DWORD Wait(DWORD ms) override {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms == INFINITE ? 0 : ms);
            for (;;) {
                if (TryConsume()) return WAIT_OBJECT_0;
                int timeout = -1;
                if (ms != INFINITE) {
                    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                    if (left <= 0) return WAIT_TIMEOUT;
                    timeout = (int)left;
                }
                pollfd p{fd, POLLIN, 0};
                const int r = ::poll(&p, 1, timeout);
                if (r < 0 && errno != EINTR) return WAIT_FAILED;
            }
        }
    };
    inline HANDLE CreateEventW(void* /*sa*/, BOOL manualReset, BOOL initialState, const wchar_t* /*name*/) {
        auto ev = std::make_shared<LinuxEvent>(manualReset != FALSE, initialState != FALSE);
        if (ev->fd < 0) return nullptr;
        return LinuxRegisterHandle(ev);
    }
    inline BOOL SetEvent(HANDLE h) { if (auto e = LinuxHandleAs<LinuxEvent>(h)) { e->Set(); return TRUE; } return FALSE; }
    inline BOOL ResetEvent(HANDLE h) { if (auto e = LinuxHandleAs<LinuxEvent>(h)) { e->Reset(); return TRUE; } return FALSE; }
    inline DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD ms) {
        // Sadece "herhangi biri" (waitAll=FALSE) semantigi kullaniliyor.
        (void)waitAll;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms == INFINITE ? 0 : ms);
        for (;;) {
            std::vector<pollfd> fds;
            std::vector<std::shared_ptr<LinuxKernelObject>> objs;
            for (DWORD i = 0; i < count; ++i) {
                auto o = LinuxHandleAs<LinuxKernelObject>(handles[i]);
                if (auto e = std::dynamic_pointer_cast<LinuxEvent>(o)) { if (e->TryConsume()) return WAIT_OBJECT_0 + i; }
                else if (o && o->PollFd() < 0 && o->Wait(0) == WAIT_OBJECT_0) return WAIT_OBJECT_0 + i;
                objs.push_back(o);
                fds.push_back(pollfd{o ? o->PollFd() : -1, POLLIN, 0});
            }
            int timeout = -1;
            if (ms != INFINITE) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                if (left <= 0) return WAIT_TIMEOUT;
                timeout = (int)left;
            }
            const int r = ::poll(fds.data(), (nfds_t)fds.size(), timeout < 0 ? -1 : std::min(timeout, 50));
            if (r < 0 && errno != EINTR) return WAIT_FAILED;
        }
    }
    inline UINT MapVirtualKeyW(UINT vk, UINT) { return vk; }
    inline int GetKeyNameTextW(LONG, wchar_t* lpString, int cchSize) {
        if (lpString && cchSize > 0) lpString[0] = 0;
        return 0;
    }
    inline HRGN CreateRectRgn(int l, int t, int r, int b) {
        return new LinuxRegion{ l, t, r, b };
    }
    inline BOOL SetWindowRgn(HWND h, HRGN rgn, BOOL redraw) {
        if (g_linuxHooks.setWindowRgn) return g_linuxHooks.setWindowRgn(h, rgn, redraw);
        delete static_cast<LinuxRegion*>(rgn);
        return TRUE;
    }
    inline BOOL DeleteObject(void* obj) {
        delete static_cast<LinuxRegion*>(obj);
        return TRUE;
    }
    inline BOOL RegisterHotKey(HWND h, int id, UINT mods, UINT vk) {
        return g_linuxHooks.registerHotKey ? g_linuxHooks.registerHotKey(h, id, mods, vk) : TRUE;
    }
    inline BOOL UnregisterHotKey(HWND h, int id) {
        return g_linuxHooks.unregisterHotKey ? g_linuxHooks.unregisterHotKey(h, id) : TRUE;
    }
    // Linux'ta dusuk seviye klavye kancasi yok; RegisterHotKey (XGrabKey) basarisizsa
    // basarili gibi davranmak kullaniciyi yaniltirdi.
    inline BOOL StartKeyHook(HWND, int, int) { return FALSE; }
    inline void StopKeyHook() {}
    inline BOOL FlashWindow(HWND h, BOOL invert) {
        return g_linuxHooks.flashWindow ? g_linuxHooks.flashWindow(h, invert) : TRUE;
    }

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
    inline BOOL Shell_NotifyIconW(DWORD msg, NOTIFYICONDATAW* nid) {
        // Kanca yoksa tepsi yok: FALSE, cagiran gizlemek yerine baska yol secsin.
        return g_linuxHooks.notifyIcon ? g_linuxHooks.notifyIcon(msg, nid) : FALSE;
    }

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
    // Yalnizca uygulama simgesi (kaynak 1) desteklenir; gomulu app.png pikselleri.
    inline HICON LoadIconW(HINSTANCE, const wchar_t*) {
        return g_linuxHooks.loadAppIcon ? g_linuxHooks.loadAppIcon() : nullptr;
    }

    inline void Sleep(DWORD ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

    inline DWORD GetTempPathW(DWORD nBufferLength, wchar_t* lpBuffer) {
        const wchar_t* tmp = L"/tmp/";
        if (lpBuffer && nBufferLength > 5) {
            wcscpy(lpBuffer, tmp);
            return 5;
        }
        return 0;
    }

    inline BOOL GetClientRect(HWND h, RECT* rc) {
        if (g_linuxHooks.getClientRect && g_linuxHooks.getClientRect(h, rc)) return TRUE;
        if (rc) { rc->left = 0; rc->top = 0; rc->right = 1920; rc->bottom = 1080; }
        return TRUE;
    }

    #ifndef MB_OK
    #define MB_OK 0x00000000L
    #endif
    inline BOOL MessageBeep(UINT type) {
        if (g_linuxHooks.messageBeep) g_linuxHooks.messageBeep(type);
        return TRUE;
    }
    inline BOOL IsWindowVisible(HWND h) {
        return g_linuxHooks.isWindowVisible ? g_linuxHooks.isWindowVisible(h) : TRUE;
    }
    inline BOOL DeleteFileW(const wchar_t* p) {
        std::error_code ec;
        return std::filesystem::remove(p, ec);
    }
    #ifndef MOVEFILE_REPLACE_EXISTING
    #define MOVEFILE_REPLACE_EXISTING 0x1u
    #endif
    #ifndef MOVEFILE_WRITE_THROUGH
    #define MOVEFILE_WRITE_THROUGH 0x8u
    #endif
    // rename(2) ayni dosya sisteminde atomiktir (Windows'taki MOVEFILE_REPLACE_EXISTING karsiligi).
    inline BOOL MoveFileExW(const wchar_t* from, const wchar_t* to, DWORD flags) {
        if (!from || !to) return FALSE;
        std::error_code ec;
        const std::filesystem::path src(from), dst(to);
        if (!(flags & MOVEFILE_REPLACE_EXISTING) && std::filesystem::exists(dst, ec)) return FALSE;
        std::filesystem::rename(src, dst, ec);
        return !ec;
    }
    inline BOOL MoveFileW(const wchar_t* from, const wchar_t* to) { return MoveFileExW(from, to, 0); }
    // Win32 zamanlayicilari: Linux dongusu imlec yanip sonmesini kendisi suruyor. SetTimer
    // cagrisi (ResetCursorBlink) fazi sifirlamak icin bir damga birakir; dongu bunu okur.
    inline std::atomic<int64_t> g_linuxTimerResetMs{0};
    inline UINT_PTR SetTimer(HWND, UINT_PTR id, UINT, void*) {
        g_linuxTimerResetMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        return id ? id : 1;
    }
    inline BOOL KillTimer(HWND, UINT_PTR) { return TRUE; }
    #ifndef INVALID_FILE_ATTRIBUTES
    #define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
    #endif
    #ifndef FILE_ATTRIBUTE_DIRECTORY
    #define FILE_ATTRIBUTE_DIRECTORY 0x10u
    #endif
    #ifndef FILE_ATTRIBUTE_NORMAL
    #define FILE_ATTRIBUTE_NORMAL 0x80u
    #endif
    #ifndef FILE_ATTRIBUTE_READONLY
    #define FILE_ATTRIBUTE_READONLY 0x1u
    #endif
    #ifndef FILE_ATTRIBUTE_HIDDEN
    #define FILE_ATTRIBUTE_HIDDEN 0x2u
    #endif
    inline DWORD GetFileAttributesW(const wchar_t* path) {
        if (!path) return INVALID_FILE_ATTRIBUTES;
        std::error_code ec;
        const std::filesystem::path p(path);
        const auto st = std::filesystem::status(p, ec);
        if (ec || !std::filesystem::exists(st)) return INVALID_FILE_ATTRIBUTES;
        DWORD attr = std::filesystem::is_directory(st) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        const auto name = p.filename().native();
        if (!name.empty() && name[0] == '.') attr |= FILE_ATTRIBUTE_HIDDEN;
        if (::access(p.c_str(), W_OK) != 0) attr |= FILE_ATTRIBUTE_READONLY;
        return attr;
    }
    // Mutlak + normalize edilmis yol (sembolik baglantilari cozmeden, Windows'taki gibi).
    inline DWORD GetFullPathNameW(const wchar_t* path, DWORD size, wchar_t* buffer, wchar_t** filePart) {
        if (!path) return 0;
        std::error_code ec;
        std::filesystem::path p = std::filesystem::absolute(std::filesystem::path(path), ec);
        if (ec) return 0;
        std::wstring full = p.lexically_normal().wstring();
        while (full.size() > 1 && full.back() == L'/') full.pop_back();
        if (full.size() + 1 > size) return (DWORD)(full.size() + 1);
        if (buffer) {
            std::wmemcpy(buffer, full.c_str(), full.size() + 1);
            if (filePart) {
                const size_t slash = full.rfind(L'/');
                *filePart = (slash == std::wstring::npos || slash + 1 >= full.size()) ? nullptr : buffer + slash + 1;
            }
        }
        return (DWORD)full.size();
    }

    inline HMENU CreatePopupMenu() { return static_cast<HMENU>(new LinuxMenu()); }
    inline BOOL AppendMenuW(HMENU menu, UINT flags, UINT_PTR id, const wchar_t* text) {
        if (!menu) return FALSE;
        LinuxMenuItem it;
        it.flags = flags;
        it.id = id;
        if (text) it.text = text;
        static_cast<LinuxMenu*>(menu)->items.push_back(std::move(it));
        return TRUE;
    }
    // Win32 gibi: MF_POPUP ile eklenen alt menuler de birlikte yok edilir.
    inline BOOL DestroyMenu(HMENU menu) {
        if (!menu) return FALSE;
        auto* m = static_cast<LinuxMenu*>(menu);
        for (auto& it : m->items) {
            if ((it.flags & 0x00000010L /* MF_POPUP */) && it.id) DestroyMenu(reinterpret_cast<HMENU>(it.id));
        }
        delete m;
        return TRUE;
    }
    inline UINT TrackPopupMenuEx(HMENU menu, UINT flags, int x, int y, HWND owner, void*) {
        return (g_linuxHooks.trackPopupMenu && menu) ? g_linuxHooks.trackPopupMenu(menu, flags, x, y, owner) : 0;
    }

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

    #ifndef MB_YESNO
    #define MB_YESNO 0x00000004L
    #endif
    #ifndef MB_YESNOCANCEL
    #define MB_YESNOCANCEL 0x00000003L
    #endif
    #ifndef MB_RETRYCANCEL
    #define MB_RETRYCANCEL 0x00000005L
    #endif
    #ifndef MB_ICONERROR
    #define MB_ICONERROR 0x00000010L
    #endif
    #ifndef MB_ICONQUESTION
    #define MB_ICONQUESTION 0x00000020L
    #endif
    #ifndef MB_ICONINFORMATION
    #define MB_ICONINFORMATION 0x00000040L
    #endif
    #ifndef MB_DEFBUTTON3
    #define MB_DEFBUTTON3 0x00000200L
    #endif
    #ifndef MB_TOPMOST
    #define MB_TOPMOST 0x00040000L
    #endif
    #ifndef MB_SETFOREGROUND
    #define MB_SETFOREGROUND 0x00010000L
    #endif
    #ifndef IDRETRY
    #define IDRETRY 4
    #endif
    #ifndef IDYES
    #define IDYES 6
    #endif
    #ifndef IDNO
    #define IDNO 7
    #endif

    // Kanca yoksa (GUI'siz kip) soru sorulamaz: guvenli taraf secilir. Onay
    // isteyen kutularda "hayir/iptal", bilgi kutusunda "tamam".
    // ---- Ortak dosya iletisim kutulari (commdlg) -------------------------------
    // Gercek uygulama LinuxNative.cpp'de (zenity / kdialog); kanca yoksa iptal sayilir.
    struct OPENFILENAMEW {
        DWORD lStructSize = sizeof(OPENFILENAMEW);
        HWND hwndOwner = nullptr;
        HINSTANCE hInstance = nullptr;
        const wchar_t* lpstrFilter = nullptr;      // "Ad\0*.a;*.b\0...\0\0"
        wchar_t* lpstrCustomFilter = nullptr;
        DWORD nMaxCustFilter = 0;
        DWORD nFilterIndex = 0;
        wchar_t* lpstrFile = nullptr;
        DWORD nMaxFile = 0;
        wchar_t* lpstrFileTitle = nullptr;
        DWORD nMaxFileTitle = 0;
        const wchar_t* lpstrInitialDir = nullptr;
        const wchar_t* lpstrTitle = nullptr;
        DWORD Flags = 0;
        WORD nFileOffset = 0;
        WORD nFileExtension = 0;
        const wchar_t* lpstrDefExt = nullptr;
    };
    #ifndef OFN_OVERWRITEPROMPT
    #define OFN_OVERWRITEPROMPT 0x00000002u
    #define OFN_NOCHANGEDIR     0x00000008u
    #define OFN_PATHMUSTEXIST   0x00000800u
    #define OFN_FILEMUSTEXIST   0x00001000u
    #endif
    inline BOOL GetOpenFileNameW(OPENFILENAMEW* ofn) {
        return ofn && g_linuxHooks.fileDialog ? g_linuxHooks.fileDialog(ofn, false) : FALSE;
    }
    inline BOOL GetSaveFileNameW(OPENFILENAMEW* ofn) {
        return ofn && g_linuxHooks.fileDialog ? g_linuxHooks.fileDialog(ofn, true) : FALSE;
    }

    inline int MessageBoxW(HWND owner, const wchar_t* text, const wchar_t* caption, UINT type) {
        if (g_linuxHooks.messageBox) return g_linuxHooks.messageBox(owner, text, caption, type);
        switch (type & 0x0F) {
        case MB_OKCANCEL: case MB_RETRYCANCEL: case MB_YESNOCANCEL: return IDCANCEL;
        case MB_YESNO: return IDNO;
        default: return IDOK;
        }
    }

    inline BOOL GetMonitorInfoW(HMONITOR mon, MONITORINFO* mi) {
        if (g_linuxHooks.getMonitorInfo && g_linuxHooks.getMonitorInfo(mon, mi)) return TRUE;
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

    #ifndef sscanf_s
    // Yalnizca boyut parametresi en sonda olan kullanimlar icin guvenli (fazla arguman yok sayilir).
    #define sscanf_s sscanf
    #endif
    #ifndef swprintf_s
    #define swprintf_s(buf, fmt, ...) swprintf(buf, sizeof(buf)/sizeof(wchar_t), fmt, ##__VA_ARGS__)
    #endif

    inline BOOL ClientToScreen(HWND h, POINT* pt) {
        return g_linuxHooks.clientToScreen ? g_linuxHooks.clientToScreen(h, pt) : TRUE;
    }
    inline BOOL ScreenToClient(HWND h, POINT* pt) {
        if (!pt) return FALSE;
        POINT orig{ 0, 0 };
        if (ClientToScreen(h, &orig)) {
            pt->x -= orig.x;
            pt->y -= orig.y;
            return TRUE;
        }
        return FALSE;
    }
    inline BOOL IsZoomed(HWND h) {
        return g_linuxHooks.isZoomed ? g_linuxHooks.isZoomed(h) : FALSE;
    }

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

    inline LRESULT SendMessageW(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        return g_linuxHooks.sendMessage ? g_linuxHooks.sendMessage(h, msg, wp, lp) : 0;
    }
    inline BOOL DestroyIcon(HICON icon) {
        auto* li = static_cast<LinuxIcon*>(icon);
        if (li && li->owned) delete li;
        return TRUE;
    }

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
