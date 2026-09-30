#include "mcp/McpBridge.h"
#include "ui/MainWindow.h"
#include "core/Utf8.h"

#include <shellapi.h>
#include <chrono>
#include <thread>

namespace ft {

bool McpBridge::IsGuiRunning() {
    return FindWindowW(MainWindow::ClassName(), nullptr) != nullptr;
}

bool McpBridge::EnsureGuiRunning(int timeoutMs) {
    if (IsGuiRunning()) return true;

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    HINSTANCE hInst = ShellExecuteW(nullptr, L"open", exe, nullptr, nullptr, SW_SHOW);
    if ((INT_PTR)hInst <= 32) {
        return false;
    }

    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < timeoutMs) {
        if (IsGuiRunning()) return true;
        Sleep(100);
    }
    return IsGuiRunning();
}

bool McpBridge::CallGui(const json::Value& req, json::Value& resp, std::string& err) {
    HWND hwnd = FindWindowW(MainWindow::ClassName(), nullptr);
    if (!hwnd) {
        // Otomatik baslatmayi dene
        if (!EnsureGuiRunning(2500)) {
            err = "FullTerminal GUI calismiyor. Gorsel terminal orkestrasyon araclari (ft_pane_*) icin FullTerminal penceresini baslatin (veya bagimsiz komutlar icin ft_exec kullanin).";
            return false;
        }
        hwnd = FindWindowW(MainWindow::ClassName(), nullptr);
        if (!hwnd) {
            err = "FullTerminal GUI penceresine baglanilamadi.";
            return false;
        }
    }

    const DWORD pid = GetCurrentProcessId();
    const ULONGLONG ticks = GetTickCount64();
    const std::wstring mapName = L"Local\\FullTerminal_Ipc_" + std::to_wstring(pid) + L"_" + std::to_wstring(ticks);
    constexpr DWORD kMapSize = 4 * 1024 * 1024; // 4 MB tampon

    HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kMapSize, mapName.c_str());
    if (!hMap) {
        err = "IPC paylasimli bellek olusturulamadi (hata: " + std::to_string(GetLastError()) + ").";
        return false;
    }

    void* pBuf = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, kMapSize);
    if (!pBuf) {
        CloseHandle(hMap);
        err = "IPC paylasimli bellek gorunumu eslenemedi.";
        return false;
    }

    // Baslangic uzunluk basligini sifirla
    memset(pBuf, 0, sizeof(uint32_t));

    json::Value envelope = json::Value::Object();
    envelope["map"] = WideToUtf8(mapName);
    envelope["req"] = req;
    const std::string payload = envelope.dump();

    COPYDATASTRUCT cds{};
    cds.dwData = FT_IPC_MAGIC;
    cds.cbData = static_cast<DWORD>(payload.size());
    cds.lpData = const_cast<char*>(payload.data());

    // SendMessageW senkrondur: GUI istegi isleyip yaniti yazana kadar donmez
    const LRESULT lRes = SendMessageW(hwnd, WM_COPYDATA, static_cast<WPARAM>(pid), reinterpret_cast<LPARAM>(&cds));
    if (lRes != 1) {
        UnmapViewOfFile(pBuf);
        CloseHandle(hMap);
        err = "FullTerminal GUI istegi isleyemedi veya reddetti.";
        return false;
    }

    uint32_t respLen = 0;
    memcpy(&respLen, pBuf, sizeof(uint32_t));
    if (respLen == 0 || respLen > kMapSize - sizeof(uint32_t)) {
        UnmapViewOfFile(pBuf);
        CloseHandle(hMap);
        err = "FullTerminal GUI bos veya gecersiz yanit dondurdu.";
        return false;
    }

    const char* respBytes = static_cast<const char*>(pBuf) + sizeof(uint32_t);
    std::string respStr(respBytes, respLen);

    UnmapViewOfFile(pBuf);
    CloseHandle(hMap);

    bool ok = false;
    resp = json::Value::parse(respStr, &ok);
    if (!ok) {
        err = "GUI yaniti JSON olarak ayristirilamadi: " + respStr;
        return false;
    }
    return true;
}

} // namespace ft
