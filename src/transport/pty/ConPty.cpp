#include "transport/pty/ConPty.h"

#include <algorithm>
#include <map>
#include <cwctype>
#include <iostream>

// Eski SDK'larda tanimli olmayabilir.
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

namespace ft {
namespace {

using PfnCreatePseudoConsole = HRESULT (WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using PfnResizePseudoConsole = HRESULT (WINAPI*)(HPCON, COORD);
using PfnClosePseudoConsole  = void    (WINAPI*)(HPCON);
using PfnReleasePseudoConsole = HRESULT (WINAPI*)(HPCON);

struct ConPtyApi {
    PfnCreatePseudoConsole Create = nullptr;
    PfnResizePseudoConsole ResizeFn = nullptr;
    PfnClosePseudoConsole  CloseFn = nullptr;
    PfnReleasePseudoConsole ReleaseFn = nullptr; // Win11 24H2+, istege bagli
    bool ok = false;

    ConPtyApi() {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (!k32) return;
        Create   = (PfnCreatePseudoConsole)GetProcAddress(k32, "CreatePseudoConsole");
        ResizeFn = (PfnResizePseudoConsole)GetProcAddress(k32, "ResizePseudoConsole");
        CloseFn  = (PfnClosePseudoConsole)GetProcAddress(k32, "ClosePseudoConsole");
        ReleaseFn = (PfnReleasePseudoConsole)GetProcAddress(k32, "ReleasePseudoConsole");
        ok = Create && ResizeFn && CloseFn;
    }
};

const ConPtyApi& Api() {
    static ConPtyApi api;
    return api;
}

struct CaseInsensitiveLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    }
};

// Mevcut ortami alip ustune override'lari yazar, CreateProcessW'nin bekledigi
// cift-null sonlu bloga cevirir. Blogun sirali olmasi gerekiyor.
std::vector<wchar_t> BuildEnvironmentBlock(
    const std::vector<std::pair<std::wstring, std::wstring>>& overrides) {

    std::map<std::wstring, std::wstring, CaseInsensitiveLess> vars;

    if (LPWCH env = GetEnvironmentStringsW()) {
        for (LPWCH p = env; *p; ) {
            std::wstring entry(p);
            p += entry.size() + 1;
            // "=C:=C:\..." gibi gizli surucu degiskenlerini oldugu gibi tasiriz
            size_t eq = entry.find(L'=', 1);
            if (eq == std::wstring::npos) continue;
            vars[entry.substr(0, eq)] = entry.substr(eq + 1);
        }
        FreeEnvironmentStringsW(env);
    }

    for (const auto& kv : overrides) {
        if (kv.first.empty()) continue;
        vars[kv.first] = kv.second;
    }

    std::vector<wchar_t> block;
    for (const auto& kv : vars) {
        block.insert(block.end(), kv.first.begin(), kv.first.end());
        block.push_back(L'=');
        block.insert(block.end(), kv.second.begin(), kv.second.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

std::wstring FormatWin32Error(DWORD code) {
    LPWSTR msg = nullptr;
    DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
    std::wstring out = (n && msg) ? std::wstring(msg, n) : L"bilinmeyen hata";
    if (msg) LocalFree(msg);
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n')) out.pop_back();
    return out + L" (0x" + std::to_wstring(code) + L")";
}

} // namespace

bool ConPty::Available() {
    return Api().ok;
}

ConPty::~ConPty() {
    Close();
}

bool ConPty::Start(const std::wstring& commandLine,
                   const std::wstring& startDir,
                   const std::vector<std::pair<std::wstring, std::wstring>>& envOverrides,
                   short cols, short rows,
                   std::wstring* errorOut) {
    auto fail = [&](const std::wstring& what) {
        if (errorOut) *errorOut = what;
        return false;
    };

    if (!Api().ok) {
        return fail(L"Bu Windows surumunde ConPTY yok. Windows 10 1809 veya ustu gerekiyor.");
    }
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    HANDLE inRead = nullptr, outWrite = nullptr;
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };

    if (!CreatePipe(&inRead, &m_inWrite, &sa, 0)) {
        return fail(L"Girdi borusu olusturulamadi: " + FormatWin32Error(GetLastError()));
    }
    if (!CreatePipe(&m_outRead, &outWrite, &sa, 0)) {
        DWORD e = GetLastError();
        CloseHandle(inRead); CloseHandle(m_inWrite); m_inWrite = nullptr;
        return fail(L"Cikti borusu olusturulamadi: " + FormatWin32Error(e));
    }

    // Bizim uclarimiz cocuga miras kalmamali.
    SetHandleInformation(m_inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(m_outRead, HANDLE_FLAG_INHERIT, 0);

    COORD size{ cols, rows };
    HRESULT hr = Api().Create(size, inRead, outWrite, 0, &m_hPC);

    // ConPTY kendi kopyalarini aldi; bizim uclarimizi hemen kapatiyoruz.
    CloseHandle(inRead);
    CloseHandle(outWrite);

    if (FAILED(hr) || !m_hPC) {
        CloseHandle(m_inWrite); m_inWrite = nullptr;
        CloseHandle(m_outRead); m_outRead = nullptr;
        return fail(L"CreatePseudoConsole basarisiz: " + FormatWin32Error((DWORD)hr));
    }

    // PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE_HANDLE ile sureci pty'ye bagla.
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nullptr;
    si.StartupInfo.hStdOutput = nullptr;
    si.StartupInfo.hStdError = nullptr;

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());

    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize)) {
        DWORD e = GetLastError();
        Close();
        return fail(L"InitializeProcThreadAttributeList basarisiz: " + FormatWin32Error(e));
    }
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   m_hPC, sizeof(HPCON), nullptr, nullptr)) {
        DWORD e = GetLastError();
        DeleteProcThreadAttributeList(si.lpAttributeList);
        Close();
        return fail(L"UpdateProcThreadAttribute basarisiz: " + FormatWin32Error(e));
    }

    std::vector<wchar_t> cmd(commandLine.begin(), commandLine.end());
    cmd.push_back(L'\0');

    std::vector<wchar_t> envBlock;
    LPVOID envPtr = nullptr;
    if (!envOverrides.empty()) {
        envBlock = BuildEnvironmentBlock(envOverrides);
        envPtr = envBlock.data();
    }

    const wchar_t* cwd = startDir.empty() ? nullptr : startDir.c_str();

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(
        nullptr, cmd.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        envPtr, cwd, &si.StartupInfo, &pi);

    DWORD createErr = ok ? 0 : GetLastError();
    DeleteProcThreadAttributeList(si.lpAttributeList);

    if (!ok) {
        Close();
        return fail(L"Kabuk baslatilamadi: " + FormatWin32Error(createErr));
    }

    m_process = pi.hProcess;
    m_thread = pi.hThread;

    // ReleasePseudoConsole Win11 24H2'de CreateProcess'ten hemen sonra cagirildiginda
    // cocuk surec henuz HPCON'a baglanmadan conhost'u sonlandirabiliyor.
    // HPCON omru ConPty::Close icindeki ClosePseudoConsole ile guvenli sekilde yonetilir.
    // if (Api().ReleaseFn) Api().ReleaseFn(m_hPC);

    m_running.store(true, std::memory_order_release);
    m_reader = std::thread(&ConPty::ReaderLoop, this);
    return true;
}

bool ConPty::ProcessExited() {
    if (m_procExited) return true;
    if (!m_process || WaitForSingleObject(m_process, 0) != WAIT_OBJECT_0) return false;
    DWORD code = 0;
    if (GetExitCodeProcess(m_process, &code)) m_exitCode.store(code, std::memory_order_relaxed);
    m_procExited = true;
    return true;
}

void ConPty::ReaderLoop() {
    // 64 KB: kucuk tamponlarda yuksek hizli ciktida ReadFile sayisi patliyor.
    std::vector<char> buf(64 * 1024);
    for (;;) {
        DWORD read = 0;
        BOOL ok = ReadFile(m_outRead, buf.data(), (DWORD)buf.size(), &read, nullptr);
        if (!ok || read == 0) break;
        if (onOutput) onOutput(buf.data(), read);
    }

    DWORD code = m_exitCode.load(std::memory_order_relaxed);
    if (m_process) {
        // Boru, surec handle'i sinyallenmeden hemen once kirilabiliyor.
        WaitForSingleObject(m_process, 200);
        DWORD c = 0;
        if (GetExitCodeProcess(m_process, &c) && c != STILL_ACTIVE) {
            code = c;
            m_exitCode.store(c, std::memory_order_relaxed);
        }
    }
    m_running.store(false, std::memory_order_release);
    if (onExit && !m_closing.load(std::memory_order_relaxed)) onExit(code);
}

void ConPty::Write(const char* data, size_t len) {
    if (!m_inWrite || !data || len == 0) return;
    size_t off = 0;
    while (off < len) {
        DWORD written = 0;
        if (!WriteFile(m_inWrite, data + off, (DWORD)(len - off), &written, nullptr)) break;
        if (written == 0) break;
        off += written;
    }
}

void ConPty::Resize(short cols, short rows) {
    if (!m_hPC || !Api().ok) return;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    COORD size{ cols, rows };
    Api().ResizeFn(m_hPC, size);
}

//
// Kapatma sirasi onemli. Yanlis sirada kilitleniyor:
//   1) girdi borusunun yazma ucunu kapat  -> istemci EOF gorur, cikar
//   2) surecin cikmasini bekle, takilirsa oldur
//   3) ClosePseudoConsole              -> pty kalan ciktiyi bosaltir,
//                                         okuyucu hala calistigi icin bloke olmaz
//   4) okuyucuyu iptal et ve birlestir
//   5) kalan handle'lari kapat
//
void ConPty::Close() {
    if (m_closing.exchange(true)) return;

    if (m_inWrite) { CloseHandle(m_inWrite); m_inWrite = nullptr; }

    if (m_process) {
        if (WaitForSingleObject(m_process, 1500) == WAIT_TIMEOUT) {
            TerminateProcess(m_process, 0);
            WaitForSingleObject(m_process, 1000);
        }
        DWORD code = 0;
        if (GetExitCodeProcess(m_process, &code)) m_exitCode.store(code, std::memory_order_relaxed);
    }

    if (m_hPC && Api().ok) { Api().CloseFn(m_hPC); m_hPC = nullptr; }

    if (m_outRead) CancelIoEx(m_outRead, nullptr);
    if (m_reader.joinable()) m_reader.join();

    if (m_outRead) { CloseHandle(m_outRead); m_outRead = nullptr; }
    if (m_thread)  { CloseHandle(m_thread);  m_thread = nullptr; }
    if (m_process) { CloseHandle(m_process); m_process = nullptr; }

    m_running.store(false, std::memory_order_release);
}

} // namespace ft
