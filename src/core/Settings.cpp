#include "core/Settings.h"
#include "core/Utf8.h"

#include <windows.h>
#include <shlwapi.h>
#include <sstream>
#include <algorithm>
#include <wincrypt.h>
#include <vector>
#include <cmath>

namespace ft {
namespace {

std::wstring ReadAll(const std::wstring& path) {
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
}

void WriteAll(const std::wstring& path, const std::wstring& text) {
    const std::string utf8 = WideToUtf8(text);
    const std::wstring temp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    bool ok = WriteFile(h, utf8.data(), (DWORD)utf8.size(), &w, nullptr) && w == utf8.size();
    if (ok) ok = FlushFileBuffers(h) != FALSE;
    CloseHandle(h);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) DeleteFileW(temp.c_str());
}

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

} // namespace

std::wstring PortableDataDir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    PathRemoveFileSpecW(exe);
    std::wstring dir = std::wstring(exe) + L"\\portable_data";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void Settings::Load(const std::wstring& dir) {
    std::wistringstream in(ReadAll(dir + L"\\settings.ini"));
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
        else if (k == L"fontPt")            fontPt = (float)_wtof(v.c_str());
        else if (k == L"opacity")           opacity = (float)_wtof(v.c_str());
        else if (k == L"accentChoice")      accentChoice = _wtoi(v.c_str());
        else if (k == L"cursorStyle")       cursorStyle = _wtoi(v.c_str());
        else if (k == L"cursorBlink")       cursorBlink = on;
        else if (k == L"colorScheme")       colorScheme = _wtoi(v.c_str());
        else if (k == L"highlightPlainText") highlightPlainText = v == L"1";
        else if (k == L"customBgColor")     customBgColor = (uint32_t)wcstoul(v.c_str(), nullptr, 16);
        else if (k == L"customFgColor")     customFgColor = (uint32_t)wcstoul(v.c_str(), nullptr, 16);
        else if (k == L"quakeMode")         quakeMode = on;
        else if (k == L"quakeHeightPercent")quakeHeightPercent = _wtoi(v.c_str());
        else if (k == L"quakeWidthPercent") quakeWidthPercent = _wtoi(v.c_str());
        else if (k == L"quakeAlign")        quakeAlign = _wtoi(v.c_str());
        else if (k == L"quakeMonitor")      quakeMonitor = _wtoi(v.c_str());
        else if (k == L"quakeHotkeyVk")     quakeHotkeyVk = _wtoi(v.c_str());
        else if (k == L"quakeHotkeyMods")   quakeHotkeyMods = _wtoi(v.c_str());
        else if (k == L"quakeHideOnLoseFocus") quakeHideOnLoseFocus = on;
        else if (k == L"quakeAnimDurationMs") quakeAnimDurationMs = _wtoi(v.c_str());
        else if (k == L"k8sYamlDir")        k8sYamlDir = v;
        else if (k == L"k8sAutoExport")     k8sAutoExport = on;
        else if (k == L"k8sAutoEnv")        k8sAutoEnv = on;
        else if (k == L"scrollbackLines")   scrollbackLines = _wtoi(v.c_str());
        else if (k == L"copyOnSelect")      copyOnSelect = on;
        else if (k == L"bellSound")         bellSound = on;
        else if (k == L"pasteGuard")        pasteGuard = on;
        else if (k == L"defaultProfile")    defaultProfile = v;
        else if (k == L"language")          language = v;
        else if (k == L"runInBackground")   runInBackground = on;
        else if (k == L"minimizeToTray")    minimizeToTray = on;
        else if (k == L"confirmClose")      confirmClose = on;
        else if (k == L"restoreSessions")   restoreSessions = on;
        else if (k == L"mcpEnabled")        mcpEnabled = on;
        else if (k == L"mcpStdio")          mcpStdio = on;
        else if (k == L"mcpHttp")           mcpHttp = on;
        else if (k == L"mcpPort")           mcpPort = _wtoi(v.c_str());
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
    if (quakeHotkeyVk <= 0 || quakeHotkeyVk > 0xFE) { quakeHotkeyVk = 0x7B; quakeHotkeyMods = 0; } // VK_F12
    quakeHotkeyMods &= 0x0F;
    if (quakeAnimDurationMs < 0 || quakeAnimDurationMs > 1000) quakeAnimDurationMs = 200;
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
        swprintf_s(hexbuf, L"%06X", customBgColor);
        o += L"customBgColor=" + std::wstring(hexbuf) + L"\n";
        swprintf_s(hexbuf, L"%06X", customFgColor);
        o += L"customFgColor=" + std::wstring(hexbuf) + L"\n";
    }
    o += L"quakeMode=" + b(quakeMode) + L"\n";
    o += L"quakeHeightPercent=" + std::to_wstring(quakeHeightPercent) + L"\n";
    o += L"quakeWidthPercent=" + std::to_wstring(quakeWidthPercent) + L"\n";
    o += L"quakeAlign=" + std::to_wstring(quakeAlign) + L"\n";
    o += L"quakeMonitor=" + std::to_wstring(quakeMonitor) + L"\n";
    o += L"quakeHotkeyVk=" + std::to_wstring(quakeHotkeyVk) + L"\n";
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
    o += L"runInBackground=" + b(runInBackground) + L"\n";
    o += L"minimizeToTray=" + b(minimizeToTray) + L"\n";
    o += L"confirmClose=" + b(confirmClose) + L"\n";
    o += L"restoreSessions=" + b(restoreSessions) + L"\n";
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
    WriteAll(dir + L"\\settings.ini", o);
}

} // namespace ft
