#include "core/ShellProfiles.h"
#include "ui/Theme.h"

#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <cwctype>

namespace ft {
namespace {

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring EnvVar(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(name, buf, (DWORD)std::size(buf));
    if (n == 0 || n >= std::size(buf)) return {};
    return std::wstring(buf, n);
}

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

// WSL dagitim adindan tanidik bir renk uret.
uint32_t DistroAccent(const std::wstring& name) {
    const std::wstring n = ToLower(name);
    if (n.find(L"ubuntu") != std::wstring::npos)  return 0xE95420;
    if (n.find(L"debian") != std::wstring::npos)  return 0xD70A53;
    if (n.find(L"kali") != std::wstring::npos)    return 0x367BF0;
    if (n.find(L"alpine") != std::wstring::npos)  return 0x0D8FC4;
    if (n.find(L"fedora") != std::wstring::npos)  return 0x3C6EB4;
    if (n.find(L"suse") != std::wstring::npos)    return 0x73BA25;
    if (n.find(L"arch") != std::wstring::npos)    return 0x1793D1;
    if (n.find(L"oracle") != std::wstring::npos)  return 0xC74634;
    if (n.find(L"docker") != std::wstring::npos)  return 0x2496ED;
    return theme::Violet;
}

std::wstring RegString(HKEY key, const wchar_t* value) {
    wchar_t buf[512];
    DWORD size = sizeof(buf);
    DWORD type = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)buf, &size) != ERROR_SUCCESS) return {};
    if (type != REG_SZ && type != REG_EXPAND_SZ) return {};
    size_t chars = size / sizeof(wchar_t);
    while (chars > 0 && buf[chars - 1] == L'\0') --chars;
    return std::wstring(buf, chars);
}

DWORD RegDword(HKEY key, const wchar_t* value, DWORD fallback) {
    DWORD v = 0, size = sizeof(v), type = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)&v, &size) != ERROR_SUCCESS) return fallback;
    if (type != REG_DWORD) return fallback;
    return v;
}

// HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss altindaki GUID anahtarlari.
void AddWslProfiles(std::vector<ShellProfile>& out) {
    HKEY lxss = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss",
                      0, KEY_READ, &lxss) != ERROR_SUCCESS) {
        return;
    }

    const std::wstring defaultGuid = RegString(lxss, L"DefaultDistribution");

    for (DWORD i = 0;; ++i) {
        wchar_t sub[128];
        DWORD subLen = (DWORD)std::size(sub);
        LONG r = RegEnumKeyExW(lxss, i, sub, &subLen, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;

        HKEY distro = nullptr;
        if (RegOpenKeyExW(lxss, sub, 0, KEY_READ, &distro) != ERROR_SUCCESS) continue;

        std::wstring name = RegString(distro, L"DistributionName");
        DWORD state = RegDword(distro, L"State", 1);
        DWORD version = RegDword(distro, L"Version", 2);
        RegCloseKey(distro);

        if (name.empty() || state != 1) continue;

        ShellProfile p;
        p.kind = ProfileKind::Wsl;
        p.id = L"wsl:" + name;
        p.name = name;
        p.exe = EnvVar(L"SystemRoot") + L"\\System32\\wsl.exe";
        p.args = L"-d \"" + name + L"\"";
        p.badge = (version == 1) ? L"WSL1" : L"WSL";
        p.accent = DistroAccent(name);
        p.wslDistro = name;
        p.wslVersion = (int)version;
        p.wslDefault = (!defaultGuid.empty() && _wcsicmp(defaultGuid.c_str(), sub) == 0);
        if (FileExists(p.exe)) out.push_back(std::move(p));
    }

    RegCloseKey(lxss);
}

void AddIfExists(std::vector<ShellProfile>& out, ShellProfile p) {
    if (FileExists(p.exe)) out.push_back(std::move(p));
}

} // namespace

std::wstring UserHomeDir() {
    std::wstring h = EnvVar(L"USERPROFILE");
    if (!h.empty()) return h;
    std::wstring drive = EnvVar(L"HOMEDRIVE");
    std::wstring path = EnvVar(L"HOMEPATH");
    if (!drive.empty() && !path.empty()) return drive + path;
    return L"C:\\";
}

std::wstring QuoteArg(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') {
            // Tirnaktan onceki ters bolular ciftlenir, tirnagin kendisi de kacislanir
            out.append(slashes * 2 + 1, L'\\');
        } else {
            out.append(slashes, L'\\');
        }
        out.push_back(c);
        slashes = 0;
    }
    // Kapanis tirnagindan onceki ters bolular ciftlenmezse tirnagi yutarlar ("C:\dir\")
    out.append(slashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::vector<ShellProfile> DiscoverShellProfiles() {
    std::vector<ShellProfile> out;

    const std::wstring sysRoot = EnvVar(L"SystemRoot");
    const std::wstring pf      = EnvVar(L"ProgramFiles");
    const std::wstring pf86    = EnvVar(L"ProgramFiles(x86)");
    const std::wstring localApp= EnvVar(L"LOCALAPPDATA");

    // --- PowerShell 7+ ---------------------------------------------------
    {
        ShellProfile p;
        p.kind = ProfileKind::Pwsh;
        p.id = L"pwsh";
        p.name = L"PowerShell";
        p.args = L"-NoLogo";
        p.badge = L"PS7";
        p.accent = 0x7BB6F5;
        for (const std::wstring& root : { pf, pf86, localApp }) {
            if (root.empty()) continue;
            std::wstring cand = root + L"\\PowerShell\\7\\pwsh.exe";
            if (FileExists(cand)) { p.exe = cand; break; }
        }
        if (p.exe.empty() && !localApp.empty()) {
            std::wstring cand = localApp + L"\\Microsoft\\WindowsApps\\pwsh.exe";
            if (FileExists(cand)) p.exe = cand;
        }
        AddIfExists(out, std::move(p));
    }

    // --- Windows PowerShell 5.1 -------------------------------------------
    {
        ShellProfile p;
        p.kind = ProfileKind::PowerShell;
        p.id = L"powershell";
        p.name = L"Windows PowerShell";
        p.exe = sysRoot + L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        p.args = L"-NoLogo";
        p.badge = L"PS";
        p.accent = 0x5A9CE8;
        AddIfExists(out, std::move(p));
    }

    // --- Komut istemi ------------------------------------------------------
    {
        ShellProfile p;
        p.kind = ProfileKind::Cmd;
        p.id = L"cmd";
        p.name = L"Komut Istemi";
        p.exe = sysRoot + L"\\System32\\cmd.exe";
        p.badge = L"CMD";
        p.accent = 0x8994A5;
        AddIfExists(out, std::move(p));
    }

    // --- WSL dagitimlari ---------------------------------------------------
    AddWslProfiles(out);

    // --- Git Bash ----------------------------------------------------------
    {
        ShellProfile p;
        p.kind = ProfileKind::GitBash;
        p.id = L"gitbash";
        p.name = L"Git Bash";
        p.args = L"--login -i";
        p.badge = L"GIT";
        p.accent = theme::Amber;
        for (const std::wstring& root : { pf, pf86 }) {
            if (root.empty()) continue;
            std::wstring cand = root + L"\\Git\\bin\\bash.exe";
            if (FileExists(cand)) { p.exe = cand; break; }
        }
        AddIfExists(out, std::move(p));
    }

    // --- MSYS2 -------------------------------------------------------------
    {
        ShellProfile p;
        p.kind = ProfileKind::Msys2;
        p.id = L"msys2";
        p.name = L"MSYS2 UCRT64";
        p.args = L"--login -i";
        p.badge = L"MSYS";
        p.accent = 0x74E0A6;
        for (const wchar_t* root : { L"C:\\msys64", L"C:\\msys32" }) {
            std::wstring cand = std::wstring(root) + L"\\usr\\bin\\bash.exe";
            if (FileExists(cand)) { p.exe = cand; p.env.emplace_back(L"MSYSTEM", L"UCRT64"); break; }
        }
        AddIfExists(out, std::move(p));
    }

    // --- Nushell -----------------------------------------------------------
    {
        ShellProfile p;
        p.kind = ProfileKind::Nushell;
        p.id = L"nu";
        p.name = L"Nushell";
        p.badge = L"NU";
        p.accent = theme::Green;
        wchar_t found[MAX_PATH] = L"nu.exe";
        if (PathFindOnPathW(found, nullptr)) p.exe = found;
        AddIfExists(out, std::move(p));
    }

    // Varsayilan WSL dagitimi PowerShell'den hemen sonra gelsin.
    std::stable_sort(out.begin(), out.end(), [](const ShellProfile& a, const ShellProfile& b) {
        auto rank = [](const ShellProfile& p) {
            if (p.kind == ProfileKind::Pwsh) return 0;
            if (p.kind == ProfileKind::PowerShell) return 1;
            if (p.kind == ProfileKind::Wsl && p.wslDefault) return 2;
            if (p.kind == ProfileKind::Cmd) return 3;
            if (p.kind == ProfileKind::Wsl) return 4;
            return 5;
        };
        return rank(a) < rank(b);
    });

    const std::wstring home = UserHomeDir();
    for (auto& p : out) {
        if (p.startDir.empty()) p.startDir = home;
    }

    return out;
}

} // namespace ft
