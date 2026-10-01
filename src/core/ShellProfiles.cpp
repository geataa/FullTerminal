#include "core/ShellProfiles.h"
#include "core/Utf8.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#include <shlwapi.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

namespace ft {

#if defined(_WIN32)
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

        const std::wstring name = RegString(distro, L"DistributionName");
        const DWORD ver = RegDword(distro, L"Version", 2);
        RegCloseKey(distro);

        if (name.empty()) continue;

        ShellProfile p;
        p.kind = ProfileKind::Wsl;
        p.id = L"wsl:" + ToLower(name);
        p.name = name;
        p.exe = L"wsl.exe";
        p.args = L"-d " + name;
        p.badge = L"WSL";
        p.accent = DistroAccent(name);
        p.wslDistro = name;
        p.wslVersion = (int)ver;
        p.wslDefault = (_wcsicmp(sub, defaultGuid.c_str()) == 0);
        out.push_back(std::move(p));
    }

    RegCloseKey(lxss);
}

void AddIfExists(std::vector<ShellProfile>& out, ShellProfile p) {
    if (!p.exe.empty() && FileExists(p.exe)) out.push_back(std::move(p));
}

} // namespace

std::wstring UserHomeDir() {
    std::wstring h = EnvVar(L"USERPROFILE");
    if (!h.empty()) return h;
    const std::wstring d = EnvVar(L"HOMEDRIVE");
    const std::wstring p = EnvVar(L"HOMEPATH");
    if (!d.empty() && !p.empty()) return d + p;
    return L"C:\\";
}

std::vector<ShellProfile> DiscoverShellProfiles() {
    std::vector<ShellProfile> out;
    const std::wstring sys32 = EnvVar(L"SystemRoot") + L"\\System32";
    const std::wstring pf = EnvVar(L"ProgramFiles");
    const std::wstring pf86 = EnvVar(L"ProgramFiles(x86)");
    const std::wstring localApp = EnvVar(L"LOCALAPPDATA");

    // Pwsh 7+
    {
        ShellProfile p;
        p.kind = ProfileKind::Pwsh;
        p.id = L"pwsh";
        p.name = L"PowerShell";
        p.args = L"-NoLogo";
        p.badge = L"PS";
        p.accent = 0x2671BE;
        for (const std::wstring& root : { pf, pf86, localApp + L"\\Microsoft\\PowerShell" }) {
            if (root.empty()) continue;
            std::wstring cand = root + L"\\PowerShell\\7\\pwsh.exe";
            if (FileExists(cand)) { p.exe = cand; break; }
            cand = root + L"\\PowerShell\\7-preview\\pwsh.exe";
            if (FileExists(cand)) { p.exe = cand; break; }
        }
        AddIfExists(out, std::move(p));
    }

    // Windows PowerShell 5.1
    {
        ShellProfile p;
        p.kind = ProfileKind::PowerShell;
        p.id = L"powershell";
        p.name = L"Windows PowerShell";
        p.exe = sys32 + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        p.args = L"-NoLogo";
        p.badge = L"PS";
        p.accent = 0x012456;
        AddIfExists(out, std::move(p));
    }

    // Command Prompt
    {
        ShellProfile p;
        p.kind = ProfileKind::Cmd;
        p.id = L"cmd";
        p.name = L"Command Prompt";
        p.exe = sys32 + L"\\cmd.exe";
        p.badge = L"CMD";
        p.accent = 0x4D4D4D;
        AddIfExists(out, std::move(p));
    }

    // WSL
    AddWslProfiles(out);

    // Git Bash
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

    // MSYS2
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

    // Nushell
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

#else

// Linux shell detection
namespace {

bool FileExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec);
}

} // namespace

std::wstring UserHomeDir() {
    const char* h = getenv("HOME");
    if (!h) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) h = pw->pw_dir;
    }
    return h ? Utf8ToWide(h) : L"/home";
}

std::vector<ShellProfile> DiscoverShellProfiles() {
    std::vector<ShellProfile> out;
    const std::wstring home = UserHomeDir();

    // Detect user's current shell
    std::string userShell;
    if (const char* envS = getenv("SHELL")) {
        userShell = envS;
    } else {
        struct passwd* pw = getpwuid(getuid());
        if (pw && pw->pw_shell) userShell = pw->pw_shell;
    }

    // 1. Bash
    for (const char* path : { "/bin/bash", "/usr/bin/bash" }) {
        if (FileExists(path)) {
            ShellProfile p;
            p.kind = ProfileKind::Bash;
            p.id = L"bash";
            p.name = L"Bash";
            p.exe = Utf8ToWide(path);
            p.args = L"-l";
            p.badge = L"BASH";
            p.accent = 0x4EAA25; // GNU Green
            p.startDir = home;
            out.push_back(std::move(p));
            break;
        }
    }

    // 2. Zsh
    for (const char* path : { "/bin/zsh", "/usr/bin/zsh" }) {
        if (FileExists(path)) {
            ShellProfile p;
            p.kind = ProfileKind::Zsh;
            p.id = L"zsh";
            p.name = L"Zsh";
            p.exe = Utf8ToWide(path);
            p.args = L"-l";
            p.badge = L"ZSH";
            p.accent = 0xF15A24; // Zsh Orange
            p.startDir = home;
            out.push_back(std::move(p));
            break;
        }
    }

    // 3. Fish
    if (FileExists("/usr/bin/fish") || FileExists("/bin/fish")) {
        ShellProfile p;
        p.kind = ProfileKind::Fish;
        p.id = L"fish";
        p.name = L"Fish";
        p.exe = FileExists("/usr/bin/fish") ? L"/usr/bin/fish" : L"/bin/fish";
        p.args = L"-l";
        p.badge = L"FISH";
        p.accent = 0x0073B7; // Fish Blue
        p.startDir = home;
        out.push_back(std::move(p));
    }

    // 4. Tmux
    if (FileExists("/usr/bin/tmux") || FileExists("/bin/tmux")) {
        ShellProfile p;
        p.kind = ProfileKind::Custom;
        p.id = L"tmux";
        p.name = L"tmux";
        p.exe = FileExists("/usr/bin/tmux") ? L"/usr/bin/tmux" : L"/bin/tmux";
        p.badge = L"TMUX";
        p.accent = 0x1BB954;
        p.startDir = home;
        out.push_back(std::move(p));
    }

    // 5. Nushell
    if (FileExists("/usr/bin/nu") || FileExists("/usr/local/bin/nu")) {
        ShellProfile p;
        p.kind = ProfileKind::Nushell;
        p.id = L"nu";
        p.name = L"Nushell";
        p.exe = FileExists("/usr/bin/nu") ? L"/usr/bin/nu" : L"/usr/local/bin/nu";
        p.badge = L"NU";
        p.accent = 0x3AA99F;
        p.startDir = home;
        out.push_back(std::move(p));
    }

    // 6. PowerShell Core (pwsh on Linux)
    if (FileExists("/usr/bin/pwsh") || FileExists("/usr/local/bin/pwsh")) {
        ShellProfile p;
        p.kind = ProfileKind::Pwsh;
        p.id = L"pwsh";
        p.name = L"PowerShell Core";
        p.exe = FileExists("/usr/bin/pwsh") ? L"/usr/bin/pwsh" : L"/usr/local/bin/pwsh";
        p.badge = L"PS";
        p.accent = 0x2671BE;
        p.startDir = home;
        out.push_back(std::move(p));
    }

    // 7. Sh Fallback
    if (out.empty()) {
        ShellProfile p;
        p.kind = ProfileKind::Custom;
        p.id = L"sh";
        p.name = L"POSIX Shell";
        p.exe = L"/bin/sh";
        p.badge = L"SH";
        p.accent = 0x888888;
        p.startDir = home;
        out.push_back(std::move(p));
    }

    // Move default shell to the front if found
    if (!userShell.empty()) {
        std::wstring wsUserShell = Utf8ToWide(userShell);
        auto it = std::find_if(out.begin(), out.end(), [&](const ShellProfile& p) {
            return p.exe == wsUserShell;
        });
        if (it != out.end() && it != out.begin()) {
            std::rotate(out.begin(), it, it + 1);
        }
    }

    return out;
}
#endif

std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    bool hasSpace = false;
    for (wchar_t c : arg) {
        if (c == L' ' || c == L'\t' || c == L'\"' || c == L'\'' || c == L'\\') {
            hasSpace = true;
            break;
        }
    }
    if (!hasSpace) return arg;

    std::wstring out = L"\"";
    for (wchar_t c : arg) {
        if (c == L'\"' || c == L'\\') out.push_back(L'\\');
        out.push_back(c);
    }
    out += L"\"";
    return out;
}

} // namespace ft
