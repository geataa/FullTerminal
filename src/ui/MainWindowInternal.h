#pragma once
#define _CRT_RAND_S

#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "core/Utf8.h"
#include "core/I18n.h"
#include "model/K8sManager.h"
#include "services/AgentDetector.h"
#include "mcp/Json.h"


#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include <thread>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace ft {

constexpr wchar_t kClassName[] = L"FullTerminalWindow";
constexpr UINT WM_PTY_DATA   = WM_APP + 1;
constexpr UINT WM_TRAY       = WM_APP + 2;
// WM_APP + 3: MainWindow::WM_SUMMON
constexpr UINT WM_QUAKE_CMD  = WM_APP + 4;
constexpr UINT WM_HUB_NODES  = WM_APP + 5;
constexpr UINT_PTR kBlinkTimer = 1;
constexpr UINT kBlinkMs = 530;

struct HubProbeResult {
    uint64_t gen = 0;
    std::vector<ConnectionNode> nodes;
};

constexpr int TRAY_SHOW = 1, TRAY_NEW = 2, TRAY_QUIT = 3, TRAY_QUAKE = 4, TRAY_HIDE = 5;
constexpr int BTN_MIN = 0, BTN_MAX = 1, BTN_CLOSE = 2, BTN_NEWTAB = 3,
              BTN_MENU = 4, BTN_OVERFLOW = 5, BTN_QUAKE = 6;
constexpr int kQuakeHotKeyId = 9001;

enum : int {
    ID_NONE = 0,
    ID_QUICK = 100,
    ID_H_LABEL, ID_H_ADDR, ID_H_PORT, ID_H_USER, ID_H_PASS, ID_H_KEY, ID_H_PUBKEY, ID_H_CERT,
    ID_H_GROUP, ID_H_TAGS, ID_H_JUMP, ID_H_NOTES,
    ID_I_NAME, ID_I_USER, ID_I_PASS, ID_I_KEY, ID_I_PUBKEY, ID_I_CERT, ID_I_PASSPHRASE,
    ID_S_FONT, ID_S_SCROLLBACK, ID_S_MCPPORT, ID_S_MCPTOKEN,
    ID_S_K8S_DIR, ID_S_CUSTOM_BG, ID_S_CUSTOM_FG,
    ID_KH_FILTER, ID_SFTP_LOCAL_FILTER, ID_SFTP_REMOTE_FILTER, ID_SFTP_HOST_SEARCH, ID_SFTP_NEW_FOLDER, ID_SFTP_RENAME,
    ID_TAB_RENAME,
    ID_SNP_SEARCH, ID_SNP_TITLE, ID_SNP_CMD, ID_SNP_CAT, ID_SNP_DESC, ID_SNP_YAML,
    ID_TUN_NAME, ID_TUN_HOST, ID_TUN_LPORT, ID_TUN_RHOST, ID_TUN_RPORT,
    ID_LOGS_SEARCH,
    ID_AGENT_APPROVE = 2000, ID_AGENT_DENY = 2001, ID_AGENT_FOCUS = 2002,
};

inline int64_t NowTicks() { LARGE_INTEGER li; QueryPerformanceCounter(&li); return li.QuadPart; }
inline int64_t TickFreq() {
    static int64_t f = [] { LARGE_INTEGER li; QueryPerformanceFrequency(&li); return li.QuadPart; }();
    return f;
}

inline int ModifierCode() {
    int m = 1;
    if (GetKeyState(VK_SHIFT)   & 0x8000) m += 1;
    if (GetKeyState(VK_MENU)    & 0x8000) m += 2;
    if (GetKeyState(VK_CONTROL) & 0x8000) m += 4;
    return m;
}

inline std::string CsiFinal(char final, int mods, bool appCursor) {
    if (mods <= 1) return appCursor ? std::string("\x1bO") + final : std::string("\x1b[") + final;
    return "\x1b[1;" + std::to_string(mods) + final;
}

inline std::string CsiTilde(int n, int mods) {
    if (mods <= 1) return "\x1b[" + std::to_string(n) + "~";
    return "\x1b[" + std::to_string(n) + ";" + std::to_string(mods) + "~";
}

inline UINT DpiFor(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && hwnd) { UINT d = fn(hwnd); if (d) return d; }
    HDC dc = GetDC(nullptr);
    UINT d = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return d ? d : 96;
}

inline int MetricFor(int index, UINT dpi) {
    using Fn = int(WINAPI*)(int, UINT);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi");
    return fn ? fn(index, dpi) : GetSystemMetrics(index);
}

inline void EnableRoundedCorners(HWND hwnd, bool round = true) {
    const DWORD attr = 33, pref = round ? 2 : 1;
    DwmSetWindowAttribute(hwnd, attr, &pref, sizeof(pref));
}

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

inline void EnableWindowTransparency(HWND hwnd) {
    if (!hwnd) return;
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));

    // DWMSBT_NONE (1): DWM'in pencere arkasina koyu Acrylic/Mica katmani cizmesini engeller,
    // boylece saydam bolgelerde masaustu gercekten arkadan gorunur.
    DWORD backdrop = 1; // DWMSBT_NONE
    DwmSetWindowAttribute(hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof(backdrop));
}


inline float EaseOutCubic(float t) { t = 1.0f - t; return 1.0f - t * t * t; }

inline bool IsModifierVk(WPARAM vk) {
    switch (vk) {
    case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LWIN: case VK_RWIN:
    case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
    case VK_LMENU: case VK_RMENU: case VK_CAPITAL: case VK_NUMLOCK: case VK_APPS:
        return true;
    default:
        return false;
    }
}

inline std::wstring Trunc(const std::wstring& s, size_t n) {
    return s.size() <= n ? s : s.substr(0, n - 1) + L"…";
}

inline std::wstring TrimWs(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}

inline HICON MakeAppIcon(int size, uint32_t accent) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) { if (color) DeleteObject(color); return nullptr; }

    auto* px = static_cast<uint32_t*>(bits);
    const float rad = size * 0.24f;
    const uint32_t fillArgb = 0xFF000000u | (accent & 0x00FFFFFFu);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float dx = 0.0f, dy = 0.0f;
            if (x < rad) dx = rad - x; else if (x > size - 1 - rad) dx = x - (size - 1 - rad);
            if (y < rad) dy = rad - y; else if (y > size - 1 - rad) dy = y - (size - 1 - rad);
            px[(size_t)y * size + x] = (dx * dx + dy * dy <= rad * rad) ? fillArgb : 0u;
        }
    }

    auto plot = [&](int x, int y) {
        if (x >= 0 && y >= 0 && x < size && y < size) px[(size_t)y * size + x] = 0xFF0A0D11u;
    };
    auto stroke = [&](float x0, float y0, float x1, float y1, int th) {
        const int steps = (int)(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0)) * 2.0f) + 1;
        for (int i = 0; i <= steps; ++i) {
            const float t = (float)i / (float)steps;
            const int cx = (int)std::lround(x0 + (x1 - x0) * t);
            const int cy = (int)std::lround(y0 + (y1 - y0) * t);
            for (int oy = 0; oy < th; ++oy)
                for (int ox = 0; ox < th; ++ox) plot(cx + ox, cy + oy);
        }
    };

    const float s = (float)size;
    const int th = std::max(1, size / 11);
    stroke(s * 0.28f, s * 0.30f, s * 0.50f, s * 0.50f, th);
    stroke(s * 0.50f, s * 0.50f, s * 0.28f, s * 0.70f, th);
    stroke(s * 0.56f, s * 0.68f, s * 0.76f, s * 0.68f, th);

    std::vector<uint8_t> maskBits((size_t)((size + 15) / 16) * 2 * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

inline std::wstring MenuEscape(std::wstring s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'&') { s.insert(i, 1, L'&'); ++i; }
    }
    return s;
}

inline std::wstring ElideTail(Renderer& r, const std::wstring& s, float maxW,
                              float px, bool bold, bool mono = false) {
    if (s.empty() || maxW <= 0.0f) return std::wstring();
    if (r.MeasureText(s, px, bold, mono) <= maxW) return s;

    const std::wstring ell(1, (wchar_t)0x2026);
    const float dots = r.MeasureText(ell, px, bold, mono);
    if (dots > maxW) return std::wstring();

    size_t lo = 0, hi = s.size();
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        if (r.MeasureText(s.substr(0, mid), px, bold, mono) + dots <= maxW) lo = mid;
        else hi = mid - 1;
    }
    if (lo > 0 && lo < s.size() && s[lo - 1] >= 0xD800 && s[lo - 1] <= 0xDBFF) --lo;
    if (lo == 0) return ell;
    return s.substr(0, lo) + ell;
}

inline std::wstring FitTitle(Renderer& r, const std::wstring& s, float maxW,
                             float px, bool bold) {
    if (s.empty() || maxW <= 0.0f) return std::wstring();
    if (r.MeasureText(s, px, bold, false) <= maxW) return s;

    const size_t sep = s.find_last_of(L"\\/");
    if (sep != std::wstring::npos && sep + 1 < s.size()) {
        const std::wstring tail = std::wstring(1, (wchar_t)0x2026) + s.substr(sep);
        if (r.MeasureText(tail, px, bold, false) <= maxW) return tail;
        return ElideTail(r, s.substr(sep + 1), maxW, px, bold, false);
    }
    return ElideTail(r, s, maxW, px, bold, false);
}

inline std::wstring RandomToken() {
    static const wchar_t hex[] = L"0123456789abcdef";
    std::wstring out;
    out.reserve(32);
    for (int i = 0; i < 32; ++i) {
        unsigned int v = 0;
        if (rand_s(&v) != 0) v = (unsigned int)GetTickCount64() * (i + 7);
        out.push_back(hex[v & 0xF]);
    }
    return out;
}

inline bool PickYamlFile(HWND owner, std::wstring& out) {
    wchar_t cwd[MAX_PATH]{};
    const DWORD cwdLen = GetCurrentDirectoryW(MAX_PATH, cwd);

    wchar_t fileBuf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Kubernetes YAML (*.yaml;*.yml)\0*.yaml;*.yml\0Tum Dosyalar (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const bool ok = GetOpenFileNameW(&ofn) != FALSE;

    if (cwdLen > 0 && cwdLen < MAX_PATH) SetCurrentDirectoryW(cwd);
    if (ok) out = fileBuf;
    return ok;
}

inline bool PickKeyFile(HWND owner, std::wstring& out) {
    wchar_t cwd[MAX_PATH]{};
    const DWORD cwdLen = GetCurrentDirectoryW(MAX_PATH, cwd);

    wchar_t fileBuf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"SSH Anahtarlari (*.pem;*.key;id_*;*)\0*.pem;*.key;id_*;*\0Tum Dosyalar (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const bool ok = GetOpenFileNameW(&ofn) != FALSE;

    if (cwdLen > 0 && cwdLen < MAX_PATH) SetCurrentDirectoryW(cwd);
    if (ok) out = fileBuf;
    return ok;
}

inline bool PickPubOrCertFile(HWND owner, std::wstring& out) {
    wchar_t cwd[MAX_PATH]{};
    const DWORD cwdLen = GetCurrentDirectoryW(MAX_PATH, cwd);

    wchar_t fileBuf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Public Key ve Sertifikalar (*.pub;*.crt;*.pem;*)\0*.pub;*.crt;*.pem;*\0Tum Dosyalar (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const bool ok = GetOpenFileNameW(&ofn) != FALSE;

    if (cwdLen > 0 && cwdLen < MAX_PATH) SetCurrentDirectoryW(cwd);
    if (ok) out = fileBuf;
    return ok;
}

inline std::wstring ReadFileUtf8(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > 10 * 1024 * 1024) { CloseHandle(h); return {}; }
    std::string data((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    ReadFile(h, data.data(), (DWORD)data.size(), &got, nullptr);
    CloseHandle(h);
    data.resize(got);
    return Utf8ToWide(data);
}

inline std::wstring ResolveTool(const wchar_t* exe) {
    if (_wcsicmp(exe, L"wsl.exe") == 0) {
        wchar_t sys[MAX_PATH]{};
        const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
        if (n > 0 && n < MAX_PATH) return L"\"" + std::wstring(sys) + L"\\wsl.exe\"";
    }
    const DWORD need = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (need > 0) {
        std::wstring path(need, L'\0');
        const DWORD got = GetEnvironmentVariableW(L"PATH", path.data(), need);
        if (got > 0 && got < need) {
            path.resize(got);
            wchar_t full[MAX_PATH]{};
            const DWORD r = SearchPathW(path.c_str(), exe, nullptr, MAX_PATH, full, nullptr);
            if (r > 0 && r < MAX_PATH) return L"\"" + std::wstring(full) + L"\"";
        }
    }
    return exe;
}

struct PointerMask {
    UiInput& in;
    bool on;
    float mx, my, px, py;
    bool down, clicked, dbl;
    PointerMask(UiInput& i, bool mask)
        : in(i), on(mask), mx(i.mx), my(i.my), px(i.px), py(i.py),
          down(i.down), clicked(i.clicked), dbl(i.dblClick) {
        if (!on) return;
        in.mx = in.my = in.px = in.py = -1.0f;
        in.down = in.clicked = in.dblClick = false;
    }
    ~PointerMask() {
        if (!on) return;
        in.mx = mx; in.my = my; in.px = px; in.py = py;
        in.down = down; in.clicked = clicked; in.dblClick = dbl;
    }
    PointerMask(const PointerMask&) = delete;
    PointerMask& operator=(const PointerMask&) = delete;
};

} // namespace ft
