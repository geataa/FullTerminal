#include "core/Settings.h"
#include "core/Utf8.h"

#include <sstream>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <cwchar>
#include <vector>
#include <cmath>

#if defined(_WIN32)
#include <windows.h>
#include <shlwapi.h>
#include <wincrypt.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <cstdio>
#include "core/SecretStore.h"
#endif

namespace ft {
namespace {

std::wstring PathJoin(const std::wstring& dir, const std::wstring& file) {
    if (dir.empty()) return file;
    wchar_t sep = L'/';
    if (dir.find(L'\\') != std::wstring::npos) sep = L'\\';
    if (dir.back() == L'/' || dir.back() == L'\\') return dir + file;
    return dir + sep + file;
}

std::wstring ReadAll(const std::wstring& path) {
#if defined(_WIN32)
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart < 0 || sz.QuadPart > 1024 * 1024) { CloseHandle(h); return {}; }
    std::string data((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    ReadFile(h, data.data(), (DWORD)data.size(), &got, nullptr);
    CloseHandle(h);
    data.resize(got);
    return Utf8ToWide(data);
#else
    std::string u8path = WideToUtf8(path);
    std::ifstream file(u8path, std::ios::binary);
    if (!file.is_open()) return {};
    file.seekg(0, std::ios::end);
    size_t sz = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string data(sz, '\0');
    file.read(data.data(), sz);
    return Utf8ToWide(data);
#endif
}

void WriteAll(const std::wstring& path, const std::wstring& text) {
    const std::string utf8 = WideToUtf8(text);
#if defined(_WIN32)
    const std::wstring temp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    bool ok = WriteFile(h, utf8.data(), (DWORD)utf8.size(), &w, nullptr) && w == utf8.size();
    if (ok) ok = FlushFileBuffers(h) != FALSE;
    CloseHandle(h);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) DeleteFileW(temp.c_str());
#else
    // Windows ile ayni atomik yazim: gecici dosya + fsync + rename (yarim kalan ayar dosyasi olmaz).
    const std::string u8path = WideToUtf8(path);
    const std::string temp = u8path + "." + std::to_string((long)getpid()) + ".tmp";
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return;
    bool ok = true;
    size_t off = 0;
    while (ok && off < utf8.size()) {
        const ssize_t n = ::write(fd, utf8.data() + off, utf8.size() - off);
        if (n <= 0) ok = false; else off += (size_t)n;
    }
    if (ok) ok = ::fsync(fd) == 0;
    ::close(fd);
    if (!ok || ::rename(temp.c_str(), u8path.c_str()) != 0) ::unlink(temp.c_str());
#endif
}

#if defined(_WIN32)
std::wstring ProtectToken(const std::wstring& value) {
    if (value.empty()) return {};
    DATA_BLOB input{static_cast<DWORD>(value.size() * sizeof(wchar_t)), (BYTE*)value.data()}, output{};
    if (!CryptProtectData(&input, L"FullTerminal MCP", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    DWORD count = 0; const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    CryptBinaryToStringW(output.pbData, output.cbData, flags, nullptr, &count);
    std::wstring encoded(count, 0);
    CryptBinaryToStringW(output.pbData, output.cbData, flags, encoded.data(), &count);
    LocalFree(output.pbData);
    while (!encoded.empty() && !encoded.back()) encoded.pop_back();
    return encoded;
}
std::wstring UnprotectToken(const std::wstring& encoded) {
    DWORD count = 0;
    if (!CryptStringToBinaryW(encoded.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &count, nullptr, nullptr)) return {};
    std::vector<BYTE> bytes(count);
    if (!CryptStringToBinaryW(encoded.c_str(), 0, CRYPT_STRING_BASE64, bytes.data(), &count, nullptr, nullptr)) return {};
    DATA_BLOB input{count, bytes.data()}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    std::wstring value((wchar_t*)output.pbData, output.cbData / sizeof(wchar_t));
    SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData);
    return value;
}
#else
// Linux'ta DPAPI yok: core/SecretStore.h (makine + kullanici anahtarli, dogrulamali sifreleme).
std::wstring ProtectToken(const std::wstring& value) {
    return Utf8ToWide(secret::Protect(value, "mcp-token"));
}
std::wstring UnprotectToken(const std::wstring& encoded) {
    std::wstring value;
    if (!secret::Unprotect(WideToUtf8(encoded), value, "mcp-token")) return {};
    return value;
}
#endif

} // namespace

std::wstring PortableDataDir() {
#if defined(_WIN32)
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    PathRemoveFileSpecW(exe);
    std::wstring dir = std::wstring(exe) + L"\\portable_data";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
#else
    namespace fs = std::filesystem;
    // 1. Check explicit environment override
    const char* envDataDir = getenv("FULLTERMINAL_DATA_DIR");
    if (envDataDir && envDataDir[0] != '\0') {
        fs::path p(envDataDir);
        std::error_code ec;
        fs::create_directories(p, ec);
        return Utf8ToWide(p.string());
    }

    // 2. Check if running inside AppImage with portable_data alongside it
    const char* appimage = getenv("APPIMAGE");
    if (appimage && appimage[0] != '\0') {
        fs::path appimgPath(appimage);
        fs::path pdata = appimgPath.parent_path() / "portable_data";
        if (fs::exists(pdata) && fs::is_directory(pdata)) {
            return Utf8ToWide(pdata.string());
        }
    }

    // 3. Check if local portable_data exists in current working directory
    if (fs::exists("portable_data") && fs::is_directory("portable_data")) {
        return Utf8ToWide(fs::absolute("portable_data").string());
    }

    // Try relative to executable
    char exePath[4096]{};
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        fs::path p(exePath);
        fs::path pdata = p.parent_path() / "portable_data";
        if (fs::exists(pdata) && fs::is_directory(pdata)) {
            return Utf8ToWide(pdata.string());
        }
    }

    // Standard XDG config: ~/.config/fullterminal
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    fs::path configDir;
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        configDir = fs::path(xdg) / "fullterminal";
    } else if (home) {
        configDir = fs::path(home) / ".config" / "fullterminal";
    } else {
        configDir = fs::current_path() / "fullterminal_data";
    }

    std::error_code ec;
    fs::create_directories(configDir, ec);
    return Utf8ToWide(configDir.string());
#endif
}

void Settings::Load(const std::wstring& dir) {
    const std::wstring raw = ReadAll(PathJoin(dir, L"settings.ini"));
    const bool hasFile = !raw.empty();
    int loadedPolicyVersion = 0;
    std::wistringstream in(raw);
    std::wstring line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        if (line.empty() || line[0] == L'#' || line[0] == L'[') continue;
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring k = line.substr(0, eq);
        const std::wstring v = line.substr(eq + 1);

        const bool on = (v == L"1");
        if (k == L"fontFamily")             fontFamily = v;
        else if (k == L"fontPt")            fontPt = (float)wcstof(v.c_str(), nullptr);
        else if (k == L"opacity")           opacity = (float)wcstof(v.c_str(), nullptr);
        else if (k == L"accentChoice")      accentChoice = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"cursorStyle")       cursorStyle = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"cursorBlink")       cursorBlink = on;
        else if (k == L"colorScheme")       colorScheme = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"highlightPlainText") highlightPlainText = v == L"1";
        else if (k == L"customBgColor")     customBgColor = (uint32_t)wcstoul(v.c_str(), nullptr, 16);
        else if (k == L"customFgColor")     customFgColor = (uint32_t)wcstoul(v.c_str(), nullptr, 16);
        else if (k == L"quakeMode")         quakeMode = on;
        else if (k == L"quakeHeightPercent")quakeHeightPercent = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"quakeWidthPercent") quakeWidthPercent = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"quakeAlign")        quakeAlign = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"quakeMonitor")      quakeMonitor = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"quakeHotkeyVk")     quakeHotkeyVk = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"quakeHotkeyVk2")    quakeHotkeyVk2 = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"quakeHotkeyMods")   quakeHotkeyMods = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"quakeHideOnLoseFocus") quakeHideOnLoseFocus = on;
        else if (k == L"quakeAnimDurationMs") quakeAnimDurationMs = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"k8sYamlDir")        k8sYamlDir = v;
        else if (k == L"k8sAutoExport")     k8sAutoExport = on;
        else if (k == L"k8sAutoEnv")        k8sAutoEnv = on;
        else if (k == L"scrollbackLines")   scrollbackLines = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"copyOnSelect")      copyOnSelect = on;
        else if (k == L"bellSound")         bellSound = on;
        else if (k == L"pasteGuard")        pasteGuard = on;
        else if (k == L"defaultProfile")    defaultProfile = v;
        else if (k == L"language")          language = v;
        else if (k == L"launchAtStartup")   launchAtStartup = on;
        else if (k == L"runInBackground")   runInBackground = on;
        else if (k == L"minimizeToTray")    minimizeToTray = on;
        else if (k == L"confirmClose")      confirmClose = on;
        else if (k == L"restoreSessions")   restoreSessions = on;
        else if (k == L"sessionPolicyVersion") loadedPolicyVersion = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"mcpEnabled")        mcpEnabled = on;
        else if (k == L"mcpStdio")          mcpStdio = on;
        else if (k == L"mcpHttp")           mcpHttp = on;
        else if (k == L"mcpPort")           mcpPort = (int)wcstol(v.c_str(), nullptr, 10);
        else if (k == L"mcpToken")          mcpToken = v;
        else if (k == L"mcpTokenProtected") { mcpTokenBlob = v; mcpToken = UnprotectToken(v); mcpTokenLocked = !v.empty() && mcpToken.empty(); }
        else if (k == L"mcpReadOnly")       mcpReadOnly = on;
        else if (k == L"mcpApproval")       mcpApproval = on;
        else if (k == L"mcpAudit")          mcpAudit = on;
        else if (k == L"mcpAllowRun")       mcpAllowRun = on;
        else if (k == L"mcpAllowFiles")     mcpAllowFiles = on;
        else if (k == L"mcpAllowK8s")       mcpAllowK8s = on;
    }
    if (!std::isfinite(fontPt) || fontPt < 6.0f || fontPt > 42.0f) fontPt = 11.0f;
    if (!std::isfinite(opacity)) opacity = 1.0f;
    if (opacity < 0.10f || opacity > 1.0f) opacity = std::clamp(opacity, 0.10f, 1.0f);
    if (scrollbackLines < 100) scrollbackLines = 100;
    if (scrollbackLines > 1000000) scrollbackLines = 1000000;
    if (accentChoice < 0 || accentChoice > 4) accentChoice = 0;
    if (colorScheme < 0 || colorScheme > 8) colorScheme = 0;
    if (cursorStyle < 0 || cursorStyle > 6) cursorStyle = 0;
    if (quakeHeightPercent < 20 || quakeHeightPercent > 100) quakeHeightPercent = 50;
    if (quakeWidthPercent < 30 || quakeWidthPercent > 100) quakeWidthPercent = 100;
    if (quakeAlign < 0 || quakeAlign > 2) quakeAlign = 1;
    if (quakeMonitor < 0 || quakeMonitor > 1) quakeMonitor = 0;
    if (quakeHotkeyVk <= 0 || quakeHotkeyVk > 0xFE) { quakeHotkeyVk = 0x7B; quakeHotkeyVk2 = 0; quakeHotkeyMods = 0; } // VK_F12
    if (quakeHotkeyVk2 < 0 || quakeHotkeyVk2 > 0xFE || quakeHotkeyVk2 == quakeHotkeyVk) quakeHotkeyVk2 = 0;
    quakeHotkeyMods &= 0x0F;
    if (quakeAnimDurationMs < 0 || quakeAnimDurationMs > 1000) quakeAnimDurationMs = 200;
    if (hasFile && loadedPolicyVersion < 1) {
        restoreSessions = true;
        sessionPolicyVersion = 1;
    } else if (hasFile) {
        sessionPolicyVersion = loadedPolicyVersion;
    } else {
        sessionPolicyVersion = 1;
    }
    if (mcpPort < 1 || mcpPort > 65535) mcpPort = 8787;
}

void Settings::Save(const std::wstring& dir) const {
    auto b = [](bool v) { return std::wstring(v ? L"1" : L"0"); };
    std::wstring o = L"# FullTerminal ayarlari\n[general]\n";
    o += L"fontFamily=" + fontFamily + L"\n";
    o += L"fontPt=" + std::to_wstring(fontPt) + L"\n";
    o += L"opacity=" + std::to_wstring(opacity) + L"\n";
    o += L"accentChoice=" + std::to_wstring(accentChoice) + L"\n";
    o += L"cursorStyle=" + std::to_wstring(cursorStyle) + L"\n";
    o += L"cursorBlink=" + b(cursorBlink) + L"\n";
    o += L"colorScheme=" + std::to_wstring(colorScheme) + L"\n";
    o += L"highlightPlainText=" + std::wstring(highlightPlainText ? L"1" : L"0") + L"\n";
    {
        wchar_t hexbuf[32];
#if defined(_WIN32)
        swprintf_s(hexbuf, L"%06X", customBgColor);
        o += L"customBgColor=" + std::wstring(hexbuf) + L"\n";
        swprintf_s(hexbuf, L"%06X", customFgColor);
        o += L"customFgColor=" + std::wstring(hexbuf) + L"\n";
#else
        swprintf(hexbuf, sizeof(hexbuf) / sizeof(wchar_t), L"%06X", customBgColor);
        o += L"customBgColor=" + std::wstring(hexbuf) + L"\n";
        swprintf(hexbuf, sizeof(hexbuf) / sizeof(wchar_t), L"%06X", customFgColor);
        o += L"customFgColor=" + std::wstring(hexbuf) + L"\n";
#endif
    }
    o += L"quakeMode=" + b(quakeMode) + L"\n";
    o += L"quakeHeightPercent=" + std::to_wstring(quakeHeightPercent) + L"\n";
    o += L"quakeWidthPercent=" + std::to_wstring(quakeWidthPercent) + L"\n";
    o += L"quakeAlign=" + std::to_wstring(quakeAlign) + L"\n";
    o += L"quakeMonitor=" + std::to_wstring(quakeMonitor) + L"\n";
    o += L"quakeHotkeyVk=" + std::to_wstring(quakeHotkeyVk) + L"\n";
    o += L"quakeHotkeyVk2=" + std::to_wstring(quakeHotkeyVk2) + L"\n";
    o += L"quakeHotkeyMods=" + std::to_wstring(quakeHotkeyMods) + L"\n";
    o += L"quakeHideOnLoseFocus=" + b(quakeHideOnLoseFocus) + L"\n";
    o += L"quakeAnimDurationMs=" + std::to_wstring(quakeAnimDurationMs) + L"\n";
    o += L"k8sYamlDir=" + k8sYamlDir + L"\n";
    o += L"k8sAutoExport=" + b(k8sAutoExport) + L"\n";
    o += L"k8sAutoEnv=" + b(k8sAutoEnv) + L"\n";
    o += L"scrollbackLines=" + std::to_wstring(scrollbackLines) + L"\n";
    o += L"copyOnSelect=" + b(copyOnSelect) + L"\n";
    o += L"bellSound=" + b(bellSound) + L"\n";
    o += L"pasteGuard=" + b(pasteGuard) + L"\n";
    o += L"defaultProfile=" + defaultProfile + L"\n";
    o += L"language=" + language + L"\n";
    o += L"launchAtStartup=" + b(launchAtStartup) + L"\n";
    o += L"runInBackground=" + b(runInBackground) + L"\n";
    o += L"minimizeToTray=" + b(minimizeToTray) + L"\n";
    o += L"confirmClose=" + b(confirmClose) + L"\n";
    o += L"restoreSessions=" + b(restoreSessions) + L"\n";
    o += L"sessionPolicyVersion=" + std::to_wstring(std::max(1, sessionPolicyVersion)) + L"\n";
    o += L"\n[mcp]\n";
    o += L"mcpEnabled=" + b(mcpEnabled) + L"\n";
    o += L"mcpStdio=" + b(mcpStdio) + L"\n";
    o += L"mcpHttp=" + b(mcpHttp) + L"\n";
    o += L"mcpPort=" + std::to_wstring(mcpPort) + L"\n";
    const std::wstring protectedToken = ProtectToken(mcpToken);
    if (!mcpToken.empty() && protectedToken.empty()) return; // never overwrite settings on encryption failure
    o += L"mcpTokenProtected=" + (protectedToken.empty() && mcpTokenLocked ? mcpTokenBlob : protectedToken) + L"\n";
    o += L"mcpReadOnly=" + b(mcpReadOnly) + L"\n";
    o += L"mcpApproval=" + b(mcpApproval) + L"\n";
    o += L"mcpAudit=" + b(mcpAudit) + L"\n";
    o += L"mcpAllowRun=" + b(mcpAllowRun) + L"\n";
    o += L"mcpAllowFiles=" + b(mcpAllowFiles) + L"\n";
    o += L"mcpAllowK8s=" + b(mcpAllowK8s) + L"\n";
    WriteAll(PathJoin(dir, L"settings.ini"), o);
}

bool SetLaunchAtStartup(bool enable, const wchar_t* regSubKey) {
#if defined(_WIN32)
    wchar_t exe[32768]{};
    const DWORD len = GetModuleFileNameW(nullptr, exe, 32768);
    if (len == 0 || len >= 32768) return false;

    const bool customKey = (regSubKey != nullptr && regSubKey[0] != L'\0');
    if (!customKey) {
        // Test ikililerinin (test_*.exe) gercek kullanici baslangic kaydini degistirmesini engelle.
        const wchar_t* baseName = PathFindFileNameW(exe);
        if (!baseName || _wcsicmp(baseName, L"FullTerminal.exe") != 0) {
            return true;
        }
    }

    const wchar_t* subKey = customKey ? regSubKey : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* kValueName = L"FullTerminal";
    HKEY hKey = nullptr;
    const LSTATUS openSt = customKey
        ? RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr)
        : RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_SET_VALUE, &hKey);
    if (openSt != ERROR_SUCCESS) {
        return false;
    }

    bool ok = false;
    if (enable) {
        const std::wstring cmd = L"\"" + std::wstring(exe, len) + L"\" --startup";
        const DWORD cb = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
        ok = (RegSetValueExW(hKey, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), cb) == ERROR_SUCCESS);
    } else {
        const LSTATUS st = RegDeleteValueW(hKey, kValueName);
        ok = (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(hKey);
    return ok;
#else
    namespace fs = std::filesystem;
    std::string exe;
    const char* appimage = getenv("APPIMAGE");
    if (appimage && appimage[0] != '\0') {
        exe = appimage;
    } else {
        char buf[4096]{};
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return false;
        exe.assign(buf, (size_t)n);
    }

    const bool customKey = (regSubKey != nullptr && regSubKey[0] != L'\0');
    if (!customKey) {
        const std::string baseName = fs::path(exe).filename().string();
        if (!(appimage && appimage[0] != '\0') &&
            baseName != "fullterminal" && baseName != "FullTerminal") {
            return true;
        }
    }

    fs::path desktopPath;
    if (customKey) {
        std::string sub = WideToUtf8(regSubKey);
        for (char& c : sub) if (c == '\\') c = '/';
        desktopPath = fs::temp_directory_path() / ("fullterminal-autostart-" + std::to_string((long)getuid())) / sub / "fullterminal.desktop";
    } else {
        const char* xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && xdg[0] != '\0') {
            desktopPath = fs::path(xdg) / "autostart" / "fullterminal.desktop";
        } else {
            const char* home = getenv("HOME");
            if (!home || !*home) {
                if (struct passwd* pw = getpwuid(getuid())) home = pw->pw_dir;
            }
            if (!home || !*home) return false;
            desktopPath = fs::path(home) / ".config" / "autostart" / "fullterminal.desktop";
        }
    }

    std::error_code ec;
    if (!enable) {
        fs::remove(desktopPath, ec);
        return !ec;
    }

    fs::create_directories(desktopPath.parent_path(), ec);
    if (ec) return false;

    std::string escapedExe;
    for (char c : exe) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') escapedExe += '\\';
        escapedExe += c;
    }
    const std::string content =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Version=1.0\n"
        "Name=FullTerminal\n"
        "Comment=GPU-Accelerated Terminal Emulator\n"
        "Exec=\"" + escapedExe + "\" --startup\n"
        "Icon=fullterminal\n"
        "Terminal=false\n"
        "StartupNotify=false\n"
        "X-GNOME-Autostart-enabled=true\n";

    std::ofstream out(desktopPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(content.data(), (std::streamsize)content.size());
    out.close();
    return !out.fail();
#endif
}

bool IsLaunchAtStartupEnabled(const wchar_t* regSubKey) {
#if defined(_WIN32)
    const bool customKey = (regSubKey != nullptr && regSubKey[0] != L'\0');
    if (!customKey) {
        wchar_t exe[32768]{};
        const DWORD len = GetModuleFileNameW(nullptr, exe, 32768);
        const wchar_t* baseName = (len > 0 && len < 32768) ? PathFindFileNameW(exe) : nullptr;
        if (!baseName || _wcsicmp(baseName, L"FullTerminal.exe") != 0) {
            return false;
        }
    }

    HKEY hKey = nullptr;
    const wchar_t* subKey = customKey ? regSubKey : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* kValueName = L"FullTerminal";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD cb = 0;
    const LSTATUS st = RegQueryValueExW(hKey, kValueName, nullptr, &type, nullptr, &cb);
    RegCloseKey(hKey);
    return (st == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && cb > sizeof(wchar_t));
#else
    namespace fs = std::filesystem;
    const bool customKey = (regSubKey != nullptr && regSubKey[0] != L'\0');
    if (!customKey) {
        const char* appimage = getenv("APPIMAGE");
        if (!(appimage && appimage[0] != '\0')) {
            char buf[4096]{};
            const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            const std::string baseName = (n > 0) ? fs::path(std::string(buf, (size_t)n)).filename().string() : std::string{};
            if (baseName != "fullterminal" && baseName != "FullTerminal") {
                return false;
            }
        }
    }

    fs::path desktopPath;
    if (customKey) {
        std::string sub = WideToUtf8(regSubKey);
        for (char& c : sub) if (c == '\\') c = '/';
        desktopPath = fs::temp_directory_path() / ("fullterminal-autostart-" + std::to_string((long)getuid())) / sub / "fullterminal.desktop";
    } else {
        const char* xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && xdg[0] != '\0') {
            desktopPath = fs::path(xdg) / "autostart" / "fullterminal.desktop";
        } else {
            const char* home = getenv("HOME");
            if (!home || !*home) {
                if (struct passwd* pw = getpwuid(getuid())) home = pw->pw_dir;
            }
            if (!home || !*home) return false;
            desktopPath = fs::path(home) / ".config" / "autostart" / "fullterminal.desktop";
        }
    }

    std::ifstream in(desktopPath, std::ios::binary);
    if (!in.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(in)), {});
    if (content.find("Hidden=true") != std::string::npos ||
        content.find("X-GNOME-Autostart-enabled=false") != std::string::npos) {
        return false;
    }
    return content.find("Exec=") != std::string::npos;
#endif
}

} // namespace ft
