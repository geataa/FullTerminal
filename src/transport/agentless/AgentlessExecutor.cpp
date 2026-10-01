#include "transport/agentless/AgentlessExecutor.h"
#include "core/Platform.h"
#include "core/Utf8.h"
#include "core/Settings.h"

#if defined(_WIN32)
#include <windows.h>
#include <shlwapi.h>
#else
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#endif

#include <filesystem>
#include <chrono>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <cstring>

namespace ft {

std::wstring AgentlessExecutor::s_dataDir;

void AgentlessExecutor::SetDataDir(const std::wstring& dir) {
    s_dataDir = dir;
}

std::wstring AgentlessExecutor::GetDataDir() {
    // Goreli "portable_data" MCP'de istemcinin calisma dizinine (kullanicinin reposuna) yazardi.
    if (s_dataDir.empty()) s_dataDir = PortableDataDir();
    return s_dataDir;
}

namespace {

constexpr size_t kMaxOutput = 5 * 1024 * 1024; // 5 MB safety cap

#if defined(_WIN32)
std::wstring System32Dir() {
    wchar_t buf[MAX_PATH]{};
    const UINT n = GetSystemDirectoryW(buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"C:\\Windows\\System32";
    return buf;
}
#endif

// Calisma dizinine bakmadan yalniz PATH'te arar: MCP'de calisma dizini istemcinin
// (guvenilmeyen) reposudur, oradaki bir docker/kubectl calismamali.
std::wstring FindOnPath(const wchar_t* exeName) {
#if defined(_WIN32)
    DWORD n = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (n == 0) return {};
    std::wstring path(n, L'\0');
    n = GetEnvironmentVariableW(L"PATH", path.data(), n);
    path.resize(n);
    wchar_t found[MAX_PATH]{};
    const DWORD r = SearchPathW(path.c_str(), exeName, nullptr, MAX_PATH, found, nullptr);
    if (r == 0 || r >= MAX_PATH) return {};
    return found;
#else
    std::string exe = WideToUtf8(exeName);
    const char* pathEnv = getenv("PATH");
    if (!pathEnv) pathEnv = "/usr/local/bin:/usr/bin:/bin";
    std::string pe(pathEnv);
    size_t start = 0;
    while (start < pe.size()) {
        size_t end = pe.find(':', start);
        if (end == std::string::npos) end = pe.size();
        std::string dir = pe.substr(start, end - start);
        start = end + 1;
        if (dir.empty()) continue;
        std::filesystem::path cand = std::filesystem::path(dir) / exe;
        if (access(cand.string().c_str(), X_OK) == 0) {
            return cand.wstring();
        }
    }
    return {};
#endif
}

#if defined(_WIN32)
std::wstring BuildEnvBlock(const EnvList& extra) {
    std::vector<std::wstring> vars;
    if (wchar_t* env = GetEnvironmentStringsW()) {
        for (const wchar_t* p = env; *p; p += wcslen(p) + 1) vars.emplace_back(p);
        FreeEnvironmentStringsW(env);
    }
    for (const auto& [k, v] : extra) {
        const std::wstring prefix = k + L"=";
        auto it = std::find_if(vars.begin(), vars.end(), [&](const std::wstring& s) {
            return s.size() >= prefix.size() && _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
        });
        if (it != vars.end()) *it = prefix + v;
        else vars.push_back(prefix + v);
    }
    std::wstring block;
    for (const auto& s : vars) {
        block += s;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}
#endif

bool IsValidUtf8(const std::string& s) {
    if (s.empty()) return true;
#if defined(_WIN32)
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                               nullptr, 0) > 0;
#else
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(s.data());
    size_t i = 0, len = s.size();
    while (i < len) {
        if (bytes[i] <= 0x7F) {
            i += 1;
        } else if ((bytes[i] & 0xE0) == 0xC0) {
            if (i + 1 >= len || (bytes[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        } else if ((bytes[i] & 0xF0) == 0xE0) {
            if (i + 2 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((bytes[i] & 0xF8) == 0xF0) {
            if (i + 3 >= len || (bytes[i + 1] & 0xC0) != 0x80 || (bytes[i + 2] & 0xC0) != 0x80 || (bytes[i + 3] & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
#endif
}

// Sonda yarim kalmis UTF-8 dizisini atar (cikti siniri bir karakterin ortasina denk gelebilir).
void TrimPartialUtf8Tail(std::string& s) {
    const size_t n = s.size();
    for (size_t back = 1; back <= 3 && back <= n; ++back) {
        const unsigned char c = static_cast<unsigned char>(s[n - back]);
        if ((c & 0xC0) == 0x80) continue;
        const size_t need = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        if (need > back) s.resize(n - back);
        return;
    }
}

bool LooksUtf16Le(const std::string& s) {
    if (s.size() >= 2 && static_cast<unsigned char>(s[0]) == 0xFF && static_cast<unsigned char>(s[1]) == 0xFE) {
        return true;
    }
    const size_t n = std::min<size_t>(s.size(), 1024) & ~static_cast<size_t>(1);
    if (n < 4) return false;
    size_t oddZero = 0, evenZero = 0;
    for (size_t i = 0; i < n; i += 2) {
        if (s[i] == 0) ++evenZero;
        if (s[i + 1] == 0) ++oddZero;
    }
    const size_t pairs = n / 2;
    return oddZero * 10 >= pairs * 3 && evenZero * 10 < pairs;
}

std::string Utf16LeToUtf8(const std::string& s) {
    const size_t off = (s.size() >= 2 && static_cast<unsigned char>(s[0]) == 0xFF &&
                        static_cast<unsigned char>(s[1]) == 0xFE) ? 2 : 0;
    std::wstring w((s.size() - off) / 2, L'\0');
    if (!w.empty()) memcpy(w.data(), s.data() + off, w.size() * sizeof(wchar_t));
    return WideToUtf8(w);
}

#if defined(_WIN32)
// Manifestteki activeCodePage=UTF-8 yuzunden CP_ACP/CP_OEMCP bu surecte 65001'dir;
// sistemin gercek kod sayfalari yerel ayardan okunur (tr-TR: OEM 857, ANSI 1254).
UINT SystemCodePage(bool oem) {
    DWORD cp = 0;
    const LCTYPE type = (oem ? LOCALE_IDEFAULTCODEPAGE : LOCALE_IDEFAULTANSICODEPAGE) | LOCALE_RETURN_NUMBER;
    if (GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, type, reinterpret_cast<LPWSTR>(&cp),
                        sizeof(cp) / sizeof(wchar_t)) == 0) {
        return 0;
    }
    if (cp == CP_UTF8 || cp == CP_ACP || cp == CP_OEMCP) return 0;
    return cp;
}

std::string CodePageToUtf8(const std::string& s, UINT cp) {
    const int n = MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return s;
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return WideToUtf8(w);
}
#endif

// Arac ciktisini UTF-8'e cevirir. Yerel Windows araclari OEM kod sayfasinda (tr-TR: 857),
// wsl.exe hatalari UTF-16LE yazabilir. Uzak cikti zaten UTF-8 ya da ikilidir; ikili baytlari
// JSON yazici U+FFFD'ye cevirir.
std::string NormalizeOutput(std::string raw, TargetKind kind) {
    if (raw.empty()) return raw;
#if defined(_WIN32)
    const bool localWin = kind == TargetKind::PowerShell || kind == TargetKind::Cmd ||
                          kind == TargetKind::LocalShell;
    if ((localWin || kind == TargetKind::Wsl) && LooksUtf16Le(raw)) return Utf16LeToUtf8(raw);
    if (!localWin || IsValidUtf8(raw)) return raw;
    const UINT oem = SystemCodePage(true);
    return oem ? CodePageToUtf8(raw, oem) : raw;
#else
    if (LooksUtf16Le(raw)) return Utf16LeToUtf8(raw);
    return raw;
#endif
}

// Yerel dosya icerigini UTF-8 metne cevirir; ikili dosyada false doner.
bool DecodeTextFile(std::string& data) {
    const auto b = [&](size_t i) { return static_cast<unsigned char>(data[i]); };
    if (data.size() >= 3 && b(0) == 0xEF && b(1) == 0xBB && b(2) == 0xBF) {
        data.erase(0, 3);
    } else if (data.size() >= 2 && b(0) == 0xFE && b(1) == 0xFF) { // UTF-16BE
        std::string le = data.substr(2);
        for (size_t i = 0; i + 1 < le.size(); i += 2) std::swap(le[i], le[i + 1]);
        data = Utf16LeToUtf8(le);
        return true;
    } else if (LooksUtf16Le(data)) { // Windows PowerShell Out-File varsayilani
        data = Utf16LeToUtf8(data);
        return true;
    }
    if (memchr(data.data(), 0, data.size()) != nullptr) return false;
#if defined(_WIN32)
    if (!IsValidUtf8(data)) {
        const UINT acp = SystemCodePage(false);
        if (acp) data = CodePageToUtf8(data, acp);
    }
#endif
    return true;
}

std::string ExeFromCmdLine(const std::wstring& cmdLine) {
    std::wstring exe;
    if (!cmdLine.empty() && cmdLine[0] == L'"') {
        const size_t q = cmdLine.find(L'"', 1);
        exe = cmdLine.substr(1, q == std::wstring::npos ? std::wstring::npos : q - 1);
    } else {
        exe = cmdLine.substr(0, cmdLine.find(L' '));
    }
    return WideToUtf8(exe);
}

// Hedefe gore tam komut satirini kurar. posixScript: betik sh sozdizimindedir;
// ssh'ta uzak kullanicinin giris kabugu (fish, csh...) yerine sh -c ile calistirilir.
bool BuildCommandLine(const ExecTarget& t, const std::string& script, bool posixScript,
                      std::wstring& cl, std::string& err) {
    const std::wstring w = Utf8ToWide(script);
    using AE = AgentlessExecutor;
    switch (t.kind) {
        case TargetKind::PowerShell: {
#if defined(_WIN32)
            const std::wstring exe = t.exe.empty()
                ? System32Dir() + L"\\WindowsPowerShell\\v1.0\\powershell.exe" : t.exe;
            const std::wstring prefix =
                L"$ProgressPreference='SilentlyContinue';"
                L"[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding $false;"
                L"$OutputEncoding=[Console]::OutputEncoding;\n";
            cl = AE::QuoteArg(exe) + L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command " +
                 AE::QuoteArg(prefix + w);
#else
            std::wstring exe = t.exe.empty() ? FindOnPath(L"pwsh") : t.exe;
            if (!exe.empty()) {
                cl = AE::QuoteArg(exe) + L" -NoProfile -NonInteractive -Command " + AE::QuoteArg(w);
            } else {
                cl = L"/bin/sh -c " + AE::QuoteArg(w);
            }
#endif
            break;
        }
        case TargetKind::Cmd: {
#if defined(_WIN32)
            const std::wstring exe = t.exe.empty() ? System32Dir() + L"\\cmd.exe" : t.exe;
            cl = AE::QuoteArg(exe) + L" /d /s /c \"" + w + L"\"";
#else
            cl = L"/bin/sh -c " + AE::QuoteArg(w);
#endif
            break;
        }
        case TargetKind::LocalShell:
#if defined(_WIN32)
            if (t.exe.empty()) { err = "Kabuk exe'si bulunamadi (" + t.id + ")."; return false; }
            cl = AE::QuoteArg(t.exe) + L" " + t.shellFlag + L" " + AE::QuoteArg(w);
#else
            {
                std::wstring exe = t.exe.empty() ? L"/bin/bash" : t.exe;
                std::wstring flag = t.shellFlag.empty() ? L"-c" : t.shellFlag;
                cl = AE::QuoteArg(exe) + L" " + flag + L" " + AE::QuoteArg(w);
            }
#endif
            break;
        case TargetKind::Wsl:
#if defined(_WIN32)
            cl = AE::QuoteArg(System32Dir() + L"\\wsl.exe") + L" -d " + AE::QuoteArg(t.name) +
                 L" --exec sh -c " + AE::QuoteArg(w);
#else
            cl = L"/bin/sh -c " + AE::QuoteArg(w);
#endif
            break;
        case TargetKind::Docker: {
            const std::wstring exe = FindOnPath(
#if defined(_WIN32)
                L"docker.exe"
#else
                L"docker"
#endif
            );
            if (exe.empty()) { err = "docker PATH'te bulunamadi."; return false; }
            cl = AE::QuoteArg(exe) + L" exec -i " + AE::QuoteArg(t.name) + L" sh -c " + AE::QuoteArg(w);
            break;
        }
        case TargetKind::K8s: {
            const std::wstring exe = FindOnPath(
#if defined(_WIN32)
                L"kubectl.exe"
#else
                L"kubectl"
#endif
            );
            if (exe.empty()) { err = "kubectl PATH'te bulunamadi."; return false; }
            cl = AE::QuoteArg(exe) + L" exec -i";
            if (!t.ns.empty()) cl += L" -n " + AE::QuoteArg(t.ns);
            cl += L" " + AE::QuoteArg(t.name) + L" -- sh -c " + AE::QuoteArg(w);
            break;
        }
        case TargetKind::Ssh:
            if (t.sshPrefix.empty()) { err = "ssh hedefi hazirlanamadi (" + t.id + ")."; return false; }
            cl = t.sshPrefix + L" " +
                 AE::QuoteArg(posixScript ? Utf8ToWide("sh -c " + AE::ShQuote(script)) : w);
            break;
    }
    if (cl.size() >= 32767) {
        err = "Komut satiri cok uzun (" + std::to_string(cl.size()) + " karakter, Windows siniri 32767).";
        return false;
    }
    return true;
}

int RunTarget(const ExecTarget& t, const std::string& script, bool posixScript,
              const std::string* stdinData, int timeoutMs, std::string& out) {
    std::wstring cl;
    std::string err;
    if (!BuildCommandLine(t, script, posixScript, cl, err)) {
        out = "Error: " + err;
        return -1;
    }
    const int code = AgentlessExecutor::RunLocalProcess(cl, L"", out, timeoutMs, stdinData,
                                                        t.env.empty() ? nullptr : &t.env);
    out = NormalizeOutput(std::move(out), t.kind);
    return code;
}

} // namespace

std::wstring AgentlessExecutor::FindExecutableOnPath(const wchar_t* exeName) {
    return FindOnPath(exeName);
}

std::wstring AgentlessExecutor::QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t bs = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++bs;
            continue;
        }
        if (c == L'"') {
            out.append(bs * 2 + 1, L'\\');
        } else {
            out.append(bs, L'\\');
        }
        out += c;
        bs = 0;
    }
    out.append(bs * 2, L'\\'); // kapanis tirnagindan onceki ters bolular ikilenir
    out += L'"';
    return out;
}

std::string AgentlessExecutor::ShQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += '\'';
    return out;
}

int AgentlessExecutor::RunLocalProcess(const std::wstring& cmdLine,
                                      const std::wstring& startDir,
                                      std::string& outStd,
                                      int timeoutMs,
                                      const std::string* stdinData,
                                      const EnvList* env) {
    outStd.clear();
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 1024 * 1024)) {
        outStd = "Error: CreatePipe failed (GetLastError=" + std::to_string(GetLastError()) + ").";
        return -1;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE hIn = nullptr;
    HANDLE hInWrite = nullptr;
    if (stdinData) {
        if (!CreatePipe(&hIn, &hInWrite, &sa, 0)) {
            outStd = "Error: CreatePipe failed (GetLastError=" + std::to_string(GetLastError()) + ").";
            CloseHandle(hRead);
            CloseHandle(hWrite);
            return -1;
        }
        SetHandleInformation(hInWrite, HANDLE_FLAG_INHERIT, 0);
    } else {
        hIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          OPEN_EXISTING, 0, nullptr);
        if (hIn == INVALID_HANDLE_VALUE) hIn = nullptr;
    }

    HANDLE inherit[2];
    DWORD nInherit = 0;
    inherit[nInherit++] = hWrite;
    if (hIn) inherit[nInherit++] = hIn;

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize ? attrSize : 1);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    bool attrsOk = attrSize != 0 && InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize);
    if (attrsOk && !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                              nInherit * sizeof(HANDLE), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attrs);
        attrsOk = false;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdOutput = hWrite;
    si.StartupInfo.hStdError = hWrite;
    si.StartupInfo.hStdInput = hIn;
    if (attrsOk) si.lpAttributeList = attrs;

    std::wstring envBlock;
    if (env && !env->empty()) envBlock = BuildEnvBlock(*env);

    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    if (attrsOk) flags |= EXTENDED_STARTUPINFO_PRESENT;
    if (!envBlock.empty()) flags |= CREATE_UNICODE_ENVIRONMENT;

    PROCESS_INFORMATION pi{};
    std::wstring cmdCopy = cmdLine;
    const wchar_t* pDir = startDir.empty() ? nullptr : startDir.c_str();

    const BOOL ok = CreateProcessW(nullptr, cmdCopy.data(), nullptr, nullptr, TRUE, flags,
                                   envBlock.empty() ? nullptr : envBlock.data(), pDir,
                                   reinterpret_cast<STARTUPINFOW*>(&si), &pi);
    const DWORD createErr = ok ? 0 : GetLastError();

    if (attrsOk) DeleteProcThreadAttributeList(attrs);
    CloseHandle(hWrite);
    if (hIn) CloseHandle(hIn);

    if (!ok) {
        CloseHandle(hRead);
        if (hInWrite) CloseHandle(hInWrite);
        outStd = "Error: failed to launch '" + ExeFromCmdLine(cmdLine) +
                 "' (GetLastError=" + std::to_string(createErr) + ").";
        return -1;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li)) ||
            !AssignProcessToJobObject(job, pi.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    ResumeThread(pi.hThread);

    std::thread writer;
    if (hInWrite) {
        writer = std::thread([hInWrite, stdinData]() {
            size_t off = 0;
            while (off < stdinData->size()) {
                const DWORD chunk = static_cast<DWORD>(std::min<size_t>(stdinData->size() - off, 64 * 1024));
                DWORD w = 0;
                if (!::WriteFile(hInWrite, stdinData->data() + off, chunk, &w, nullptr) || w == 0) break;
                off += w;
            }
            CloseHandle(hInWrite);
        });
    }

    const bool hasDeadline = timeoutMs > 0;
    const ULONGLONG deadline = GetTickCount64() + (hasDeadline ? static_cast<ULONGLONG>(timeoutMs) : 0);
    bool timedOut = false;
    bool capped = false;
    bool exited = false;
    char buf[8192];
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        if (hasDeadline && now >= deadline) {
            if (!exited) timedOut = true;
            break;
        }
        DWORD avail = 0;
        if (!PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail > 0) {
            DWORD got = 0;
            if (!::ReadFile(hRead, buf, std::min<DWORD>(avail, sizeof(buf)), &got, nullptr) || got == 0) break;
            outStd.append(buf, got);
            if (outStd.size() > kMaxOutput) {
                capped = true;
                break;
            }
            continue;
        }
        if (exited) break;
        const DWORD waitMs = hasDeadline ? static_cast<DWORD>(std::min<ULONGLONG>(20, deadline - now)) : 20;
        if (WaitForSingleObject(pi.hProcess, waitMs) == WAIT_OBJECT_0) exited = true;
    }

    if (!timedOut && !capped && !exited) {
        DWORD waitMs = INFINITE;
        if (hasDeadline) {
            const ULONGLONG now = GetTickCount64();
            waitMs = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
        }
        if (WaitForSingleObject(pi.hProcess, waitMs) == WAIT_TIMEOUT) timedOut = true;
    }

    if (timedOut || capped) {
        if (job) TerminateJobObject(job, 124);
        else TerminateProcess(pi.hProcess, 124);
        WaitForSingleObject(pi.hProcess, 2000);
        if (capped) {
            outStd.resize(kMaxOutput);
            TrimPartialUtf8Tail(outStd);
        } else {
            DWORD avail = 0;
            while (outStd.size() < kMaxOutput &&
                   PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD got = 0;
                if (!::ReadFile(hRead, buf, std::min<DWORD>(avail, sizeof(buf)), &got, nullptr) || got == 0) break;
                outStd.append(buf, got);
            }
        }
    }
    CloseHandle(hRead);

    if (writer.joinable()) {
        while (WaitForSingleObject(writer.native_handle(), 2000) == WAIT_TIMEOUT) {
            CancelSynchronousIo(writer.native_handle());
        }
        writer.join();
    }

    int exitCode = 0;
    if (timedOut) {
        exitCode = 124;
        outStd += "\n[timeout after " + std::to_string(timeoutMs) + " ms - process tree killed]\n";
    } else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        exitCode = static_cast<int>(code);
        if (capped) outStd += "\n[output truncated at 5 MB - process tree killed]\n";
    }

    if (job) {
        if (!timedOut && !capped) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
        }
        CloseHandle(job);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exitCode;
#else
    int outPipe[2];
    if (pipe(outPipe) != 0) {
        outStd = "Error: pipe failed (" + std::string(strerror(errno)) + ").";
        return -1;
    }

    int inPipe[2];
    if (stdinData) {
        if (pipe(inPipe) != 0) {
            outStd = "Error: pipe failed (" + std::string(strerror(errno)) + ").";
            close(outPipe[0]);
            close(outPipe[1]);
            return -1;
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        outStd = "Error: fork failed (" + std::string(strerror(errno)) + ").";
        close(outPipe[0]);
        close(outPipe[1]);
        if (stdinData) {
            close(inPipe[0]);
            close(inPipe[1]);
        }
        return -1;
    }

    if (pid == 0) {
        // Child: become leader of new process group to allow killing entire tree on timeout
        setpgid(0, 0);

        dup2(outPipe[1], STDOUT_FILENO);
        dup2(outPipe[1], STDERR_FILENO);
        close(outPipe[0]);
        close(outPipe[1]);

        if (stdinData) {
            dup2(inPipe[0], STDIN_FILENO);
            close(inPipe[0]);
            close(inPipe[1]);
        } else {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }

        if (!startDir.empty()) {
            if (chdir(WideToUtf8(startDir).c_str()) != 0) {}
        }

        if (env) {
            for (const auto& [k, v] : *env) {
                setenv(WideToUtf8(k).c_str(), WideToUtf8(v).c_str(), 1);
            }
        }

        std::string cmd = WideToUtf8(cmdLine);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    // Parent
    close(outPipe[1]);

    std::thread writer;
    if (stdinData) {
        close(inPipe[0]);
        writer = std::thread([fd = inPipe[1], stdinData]() {
            size_t off = 0;
            while (off < stdinData->size()) {
                ssize_t w = write(fd, stdinData->data() + off, std::min<size_t>(stdinData->size() - off, 64 * 1024));
                if (w <= 0) break;
                off += w;
            }
            close(fd);
        });
    }

    int pflags = fcntl(outPipe[0], F_GETFL, 0);
    if (pflags >= 0) fcntl(outPipe[0], F_SETFL, pflags | O_NONBLOCK);

    const bool hasDeadline = timeoutMs > 0;
    const uint64_t deadline = GetTickCount64() + (hasDeadline ? static_cast<uint64_t>(timeoutMs) : 0);
    bool timedOut = false;
    bool capped = false;
    bool exited = false;
    char buf[8192];

    for (;;) {
        const uint64_t now = GetTickCount64();
        if (hasDeadline && now >= deadline) {
            if (!exited) timedOut = true;
            break;
        }

        int waitMs = hasDeadline ? static_cast<int>(std::min<uint64_t>(20, deadline - now)) : 20;
        struct pollfd pfd{ outPipe[0], POLLIN, 0 };
        int pr = poll(&pfd, 1, waitMs);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            while (true) {
                ssize_t got = read(outPipe[0], buf, sizeof(buf));
                if (got > 0) {
                    outStd.append(buf, got);
                    if (outStd.size() > kMaxOutput) {
                        capped = true;
                        break;
                    }
                } else {
                    break;
                }
            }
            if (capped) break;
        }

        int status = 0;
        pid_t wp = waitpid(pid, &status, WNOHANG);
        if (wp == pid) {
            exited = true;
            while (true) {
                ssize_t got = read(outPipe[0], buf, sizeof(buf));
                if (got > 0) {
                    outStd.append(buf, got);
                    if (outStd.size() > kMaxOutput) {
                        capped = true;
                        break;
                    }
                } else {
                    break;
                }
            }
            break;
        }
    }

    if (timedOut || capped) {
        killpg(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        if (capped) {
            outStd.resize(kMaxOutput);
            TrimPartialUtf8Tail(outStd);
        }
    }

    close(outPipe[0]);
    if (writer.joinable()) writer.join();

    int exitCode = 0;
    if (timedOut) {
        exitCode = 124;
        outStd += "\n[timeout after " + std::to_string(timeoutMs) + " ms - process tree killed]\n";
    } else if (capped) {
        outStd += "\n[output truncated at 5 MB - process tree killed]\n";
    } else {
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) exitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) exitCode = 128 + WTERMSIG(status);
    }

    return exitCode;
#endif
}

ExecResult AgentlessExecutor::ApplyContextGuard(const std::string& rawOutput,
                                               int exitCode,
                                               int maxLines,
                                               int maxChars) {
    ExecResult res;
    res.exitCode = exitCode;
    res.totalChars = static_cast<int>(rawOutput.size());

    int lineCount = 0;
    for (char c : rawOutput) {
        if (c == '\n') ++lineCount;
    }
    if (!rawOutput.empty() && rawOutput.back() != '\n') ++lineCount;
    res.totalLines = lineCount;

    if (res.totalChars <= maxChars && res.totalLines <= maxLines) {
        res.output = rawOutput;
        res.truncated = false;
        return res;
    }

    res.truncated = true;

    const std::wstring dataDir = GetDataDir();
    std::filesystem::path scratchDir = std::filesystem::path(dataDir) / "scratch";
    std::error_code ec;
    std::filesystem::create_directories(scratchDir, ec);

    auto now = std::chrono::system_clock::now();
    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    uint32_t hash = 5381;
    for (char c : rawOutput) hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);

    char dumpFileName[128];
    snprintf(dumpFileName, sizeof(dumpFileName), "dump_%lld_%06x.txt", (long long)epoch, hash & 0xFFFFFF);

    std::filesystem::path dumpPath = scratchDir / dumpFileName;
    bool dumped = false;
    {
        std::ofstream dumpFile(dumpPath, std::ios::binary);
        if (dumpFile.is_open()) {
            dumpFile.write(rawOutput.data(), rawOutput.size());
            dumpFile.close();
            dumped = !dumpFile.fail();
        }
    }
    if (dumped) res.dumpFile = WideToUtf8(dumpPath.wstring());

    const size_t budget = static_cast<size_t>(std::max(256, maxChars));
    std::string preview;
    size_t pos = 0;
    int curLine = 0;
    bool lineCut = false;
    int previewMax = std::min(maxLines, 35);
    while (pos < rawOutput.size() && curLine < previewMax) {
        const size_t nextNl = rawOutput.find('\n', pos);
        const size_t len = (nextNl == std::string::npos) ? rawOutput.size() - pos : nextNl - pos + 1;
        if (preview.size() + len > budget) {
            size_t cut = budget > preview.size() ? budget - preview.size() : 0;
            while (cut > 0 && (static_cast<unsigned char>(rawOutput[pos + cut]) & 0xC0) == 0x80) --cut;
            preview.append(rawOutput, pos, cut);
            preview += "\n...[line truncated, " + std::to_string(len) + " bytes total]\n";
            lineCut = true;
            ++curLine;
            break;
        }
        preview.append(rawOutput, pos, len);
        if (nextNl == std::string::npos) {
            preview += '\n';
            ++curLine;
            break;
        }
        pos = nextNl + 1;
        ++curLine;
    }

    std::ostringstream oss;
    oss << "[CONTEXT_BUDGET_GUARD INTERCEPTED]\n"
        << "Output size was " << res.totalChars << " chars (" << res.totalLines << " lines), exceeding safe context budget.\n";
    if (dumped) {
        oss << "Full raw dump offloaded to disk: " << res.dumpFile << "\n\n";
    } else {
        oss << "Full output could NOT be saved to disk (" << WideToUtf8(dumpPath.wstring()) << " is not writable).\n\n";
    }
    oss << "Preview (First " << curLine << " lines, " << preview.size() << " of " << res.totalChars << " bytes):\n"
        << "----------------------------------------\n"
        << preview
        << "----------------------------------------\n";
    if (lineCut) {
        oss << "Tip: Lines are very long; use a byte-oriented filter in ft_exec (e.g. head -c, cut -c, Select-String) to inspect them.";
    } else {
        oss << "Tip: Use offset/limit in ft_fs or specific filter queries in ft_exec to inspect remaining sections.";
    }

    res.output = oss.str();
    return res;
}

ExecResult AgentlessExecutor::Exec(const ExecTarget& target,
                                  const std::string& command,
                                  int timeoutMs,
                                  int maxLines,
                                  int maxChars) {
    std::string rawOutput;
    const int exitCode = RunTarget(target, command, false, nullptr, timeoutMs, rawOutput);
    if (target.kind == TargetKind::Ssh && exitCode == 255) {
        rawOutput += "\n[ssh baglantisi kurulamadi (cikis 255). ft_exec ssh'i BatchMode ile calistirir: "
                     "parola sorulamaz, anahtar ya da ssh-agent gerekir.]\n";
    }
    return ApplyContextGuard(rawOutput, exitCode, maxLines, maxChars);
}

ExecResult AgentlessExecutor::ListFiles(const ExecTarget& target,
                                       const std::string& path,
                                       int maxItems) {
    if (target.IsLocal()) {
        std::filesystem::path targetPath = path.empty() ? std::filesystem::path(".") : std::filesystem::path(path);
        std::error_code ec;
        if (!std::filesystem::exists(targetPath, ec) || !std::filesystem::is_directory(targetPath, ec)) {
            ExecResult res;
            res.exitCode = 1;
            res.output = "Error: Directory not found or cannot be accessed: " + path;
            return res;
        }

        std::ostringstream oss;
        oss << std::left << std::setw(6) << "TYPE"
            << std::right << std::setw(12) << "SIZE" << "  "
            << std::left << std::setw(18) << "MODIFIED"
            << "NAME\n";
        oss << std::string(65, '-') << "\n";

        int count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(targetPath, ec)) {
            bool isDir = entry.is_directory(ec);
            uint64_t size = isDir ? 0 : entry.file_size(ec);

            auto ftime = entry.last_write_time(ec);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
            tm ltm{};
#if defined(_WIN32)
            localtime_s(&ltm, &cftime);
#else
            localtime_r(&cftime, &ltm);
#endif
            char dateBuf[32];
            snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d %02d:%02d",
                     ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday, ltm.tm_hour, ltm.tm_min);

            oss << std::left << std::setw(6) << (isDir ? "DIR" : "FILE")
                << std::right << std::setw(12) << (isDir ? "-" : std::to_string(size)) << "  "
                << std::left << std::setw(18) << dateBuf
                << entry.path().filename().string() << "\n";
            ++count;
            if (count >= maxItems) break;
        }

        return ApplyContextGuard(oss.str(), 0, maxItems + 5, 4000);
    } else {
        const std::string p = path.empty() ? "." : path;
        const std::string sh =
            "p=" + ShQuote(p) + "; "
            "if [ ! -e \"$p\" ] && [ ! -L \"$p\" ]; then printf 'Error: no such file or directory: %s\\n' \"$p\" >&2; exit 1; fi; "
            "out=$(ls -la -- \"$p\" 2>&1); rc=$?; printf '%s\\n' \"$out\" | head -n " + std::to_string(maxItems + 1) +
            "; exit $rc";
        std::string out;
        const int code = RunTarget(target, sh, true, nullptr, 15000, out);
        return ApplyContextGuard(out, code, maxItems + 5, 4000);
    }
}

ExecResult AgentlessExecutor::ReadFile(const ExecTarget& target,
                                      const std::string& path,
                                      int offsetLines,
                                      int limitLines) {
    if (target.IsLocal()) {
        std::filesystem::path p(path);
        std::error_code ec;
        if (!std::filesystem::exists(p, ec) || !std::filesystem::is_regular_file(p, ec)) {
            ExecResult res;
            res.exitCode = 1;
            res.output = "Error: Failed to open local file '" + path + "'.";
            return res;
        }

        auto sz = std::filesystem::file_size(p, ec);
        if (sz > 10 * 1024 * 1024) {
            ExecResult res;
            res.exitCode = 1;
            res.output = "Error: File exceeds 10 MB. Use targeted tools.";
            return res;
        }

        std::ifstream in(p, std::ios::binary);
        if (!in.is_open()) {
            ExecResult res;
            res.exitCode = 1;
            res.output = "Error: Failed to read local file '" + path + "'.";
            return res;
        }

        std::string fileContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();

        if (!DecodeTextFile(fileContent)) {
            ExecResult res;
            res.exitCode = 1;
            res.output = "Error: '" + path + "' appears to be a binary file (" + std::to_string(fileContent.size()) +
                         " bytes); only text files can be read.";
            return res;
        }

        std::istringstream stream(fileContent);
        std::string line;
        std::string filtered;
        int curLine = 0;
        int captured = 0;
        while (std::getline(stream, line)) {
            if (curLine >= offsetLines) {
                filtered += line;
                filtered += '\n';
                ++captured;
                if (limitLines > 0 && captured >= limitLines) break;
            }
            ++curLine;
        }
        return ApplyContextGuard(filtered, 0, limitLines > 0 ? limitLines : 50, 3000);
    } else {
        const int startLine = offsetLines + 1;
        const std::string cmd =
            "p=" + ShQuote(path) + "; "
            "if [ ! -f \"$p\" ] || [ ! -r \"$p\" ]; then printf 'Error: not a readable file: %s\\n' \"$p\" >&2; exit 1; fi; "
            "tail -n +" + std::to_string(startLine) + " -- \"$p\" | head -n " + std::to_string(limitLines);
        std::string out;
        const int code = RunTarget(target, cmd, true, nullptr, 15000, out);
        return ApplyContextGuard(out, code, limitLines, 3000);
    }
}

ExecResult AgentlessExecutor::WriteFile(const ExecTarget& target,
                                       const std::string& path,
                                       const std::string& content,
                                       bool append) {
    if (target.IsLocal()) {
        std::filesystem::path p(path);
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);

        std::ofstream out(p, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        if (!out.is_open()) {
            ExecResult res;
            res.exitCode = 1;
            res.output = "Error: Failed to create or open file '" + path + "'.";
            return res;
        }

        out.write(content.data(), content.size());
        bool ok = !out.fail();
        out.close();

        ExecResult res;
        if (!ok) {
            res.exitCode = 1;
            res.output = "Error: Write to '" + path + "' failed.";
            return res;
        }
        res.exitCode = 0;
        res.output = "Successfully wrote " + std::to_string(content.size()) + " bytes to '" + path + "'.";
        return res;
    } else {
        const std::string cmd = std::string("cat ") + (append ? ">> " : "> ") + ShQuote(path);
        std::string out;
        const int code = RunTarget(target, cmd, true, &content, 60000, out);
        ExecResult res;
        res.exitCode = code;
        if (code == 0) {
            res.output = "Successfully wrote " + std::to_string(content.size()) + " bytes to '" + path +
                         "' on " + target.id + "." + (out.empty() ? "" : "\n" + out);
        } else {
            res.output = out.empty() ? "Error: write to '" + path + "' failed (exit code " + std::to_string(code) + ")."
                                     : out;
        }
        return res;
    }
}

ExecResult AgentlessExecutor::GetFileInfo(const ExecTarget& target,
                                         const std::string& path) {
    if (target.IsLocal()) {
        std::filesystem::path p(path.empty() ? "." : path);
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) {
            ExecResult res;
            res.exitCode = 1;
            res.output = "Error: File or directory not found: " + path;
            return res;
        }

        bool isDir = std::filesystem::is_directory(p, ec);
        uint64_t size = isDir ? 0 : std::filesystem::file_size(p, ec);

        auto ftime = std::filesystem::last_write_time(p, ec);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
        tm ltm{};
#if defined(_WIN32)
        localtime_s(&ltm, &cftime);
#else
        localtime_r(&cftime, &ltm);
#endif
        char dateBuf[32];
        snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                 ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday, ltm.tm_hour, ltm.tm_min, ltm.tm_sec);

        std::ostringstream oss;
        oss << "Path: " << path << "\n"
            << "Type: " << (isDir ? "Directory" : "File") << "\n"
            << "Size: " << size << " bytes\n"
            << "Last Modified: " << dateBuf << "\n";

        return ApplyContextGuard(oss.str(), 0, 10, 1000);
    } else {
        const std::string shCmd = "p=" + ShQuote(path.empty() ? "." : path) +
                                  "; stat -- \"$p\" 2>/dev/null || ls -ld -- \"$p\"";
        std::string out;
        const int code = RunTarget(target, shCmd, true, nullptr, 15000, out);
        return ApplyContextGuard(out, code, 20, 2000);
    }
}

} // namespace ft
