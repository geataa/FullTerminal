#include "mcp/McpBridge.h"
#include "ui/MainWindow.h"
#include "core/Utf8.h"

#if defined(_WIN32)
#include <shellapi.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <cstring>
#endif

#include <chrono>
#include <thread>
#include <filesystem>

namespace ft {
#if defined(_WIN32)
namespace {
HWND FindOwnGui() {
    wchar_t own[32768]{}; GetModuleFileNameW(nullptr, own, 32768);
    HWND hwnd = nullptr;
    while ((hwnd = FindWindowExW(nullptr, hwnd, MainWindow::ClassName(), nullptr))) {
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) continue;
        wchar_t path[32768]{}; DWORD size = 32768;
        const bool same = QueryFullProcessImageNameW(process, 0, path, &size) && _wcsicmp(own, path) == 0;
        CloseHandle(process);
        if (same) return hwnd;
    }
    return nullptr;
}
}
#endif

#if !defined(_WIN32)
static std::string GetMcpGuiSocketPath() {
    const char* user = getenv("USER");
    if (!user) user = "default";
    return "/tmp/ft-" + std::string(user) + "/mcp-gui.sock";
}
#endif

bool McpBridge::IsGuiRunning() {
#if defined(_WIN32)
    return FindOwnGui() != nullptr;
#else
    std::string sockPath = GetMcpGuiSocketPath();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
    bool running = (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    close(fd);
    return running;
#endif
}

bool McpBridge::EnsureGuiRunning(int timeoutMs) {
    if (IsGuiRunning()) return true;

#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (wcsstr(exe, L"test") != nullptr) {
        return false;
    }
    HINSTANCE hInst = ShellExecuteW(nullptr, L"open", exe, nullptr, nullptr, SW_SHOW);
    if ((INT_PTR)hInst <= 32) {
        return false;
    }
#else
    char exePath[4096]{};
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len <= 0) return false;
    exePath[len] = '\0';

    std::filesystem::path p(exePath);
    if (p.filename().string().find("test") != std::string::npos) {
        return false;
    }

    std::filesystem::path fullterminalPath = p.parent_path() / "fullterminal";
    std::string binToRun = std::filesystem::exists(fullterminalPath) ? fullterminalPath.string() : "fullterminal";

    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        execlp(binToRun.c_str(), "fullterminal", nullptr);
        _exit(127);
    }
#endif

    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < timeoutMs) {
        if (IsGuiRunning()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return IsGuiRunning();
}

bool McpBridge::CallGui(const json::Value& req, json::Value& resp, std::string& err) {
#if defined(_WIN32)
    HWND hwnd = FindOwnGui();
    if (!hwnd) {
        if (!EnsureGuiRunning(2500)) {
            err = "FullTerminal GUI calismiyor. Gorsel terminal orkestrasyon araclari (ft_pane_*) icin FullTerminal penceresini baslatin (veya bagimsiz komutlar icin ft_exec kullanin).";
            return false;
        }
        hwnd = FindOwnGui();
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
    DWORD_PTR lRes = 0;
    const bool delivered = SendMessageTimeoutW(hwnd, WM_COPYDATA, static_cast<WPARAM>(pid), reinterpret_cast<LPARAM>(&cds),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 10000, &lRes) != 0;
    if (!delivered || lRes != 1) {
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
#else
    if (!IsGuiRunning()) {
        if (!EnsureGuiRunning(2500)) {
            err = "FullTerminal GUI calismiyor. Gorsel terminal orkestrasyon araclari (ft_pane_*) icin FullTerminal penceresini baslatin (veya bagimsiz komutlar icin ft_exec kullanin).";
            return false;
        }
    }

    std::string sockPath = GetMcpGuiSocketPath();
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        err = "UNIX domain socket acilamadi: " + std::string(strerror(errno));
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        err = "FullTerminal GUI soketine baglanilamadi (" + sockPath + "): " + std::string(strerror(errno));
        return false;
    }

    std::string reqStr = req.dump();
    uint32_t reqLen = static_cast<uint32_t>(reqStr.size());
    if (write(fd, &reqLen, sizeof(reqLen)) != sizeof(reqLen) ||
        write(fd, reqStr.data(), reqLen) != static_cast<ssize_t>(reqLen)) {
        close(fd);
        err = "GUI soketine istek yazilamadi.";
        return false;
    }

    uint32_t respLen = 0;
    ssize_t r = read(fd, &respLen, sizeof(respLen));
    if (r != sizeof(respLen) || respLen == 0 || respLen > 16 * 1024 * 1024) {
        close(fd);
        err = "GUI soketinden gecersiz yanit uzunlugu alindi.";
        return false;
    }

    std::string respStr(respLen, '\0');
    size_t totalRead = 0;
    while (totalRead < respLen) {
        ssize_t got = read(fd, respStr.data() + totalRead, respLen - totalRead);
        if (got <= 0) break;
        totalRead += got;
    }
    close(fd);

    if (totalRead != respLen) {
        err = "GUI yaniti eksik alindi.";
        return false;
    }

    bool ok = false;
    resp = json::Value::parse(respStr, &ok);
    if (!ok) {
        err = "GUI yaniti JSON olarak ayristirilamadi: " + respStr;
        return false;
    }
    return true;
#endif
}

} // namespace ft
