#pragma once
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>
#include <cwctype>
#include <cstdint>

namespace ft::pipeio {
constexpr DWORD MaxFrame = 4 * 1024 * 1024;

// Each portable installation and Windows user has its own namespace.
inline std::wstring Scope() {
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    uint64_t hash = 14695981039346656037ull;
    for (auto p = exe; *p; ++p) { hash ^= towlower(*p); hash *= 1099511628211ull; }
    wchar_t user[256]{}; DWORD userSize = 256;
    GetUserNameW(user, &userSize);
    for (auto p = user; *p; ++p) { hash ^= towlower(*p); hash *= 1099511628211ull; }
    return std::to_wstring(hash);
}
inline std::wstring DaemonName() { return L"\\\\.\\pipe\\FullTerminal.Daemon." + Scope(); }
inline bool ValidId(const std::string& id) {
    if (id.empty() || id.size() > 96) return false;
    for (unsigned char c : id) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}
inline std::wstring SessionName(const std::string& id) {
    return L"\\\\.\\pipe\\FullTerminal.Session." + Scope() + L"." + std::wstring(id.begin(), id.end());
}

struct Security {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attrs{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    Security() {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return;
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<BYTE> data(size);
        if (GetTokenInformation(token, TokenUser, data.data(), size, &size)) {
            LPWSTR sid = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) {
                std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
                ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr);
                LocalFree(sid);
            }
        }
        CloseHandle(token);
        attrs.lpSecurityDescriptor = descriptor;
    }
    ~Security() { if (descriptor) LocalFree(descriptor); }
};

// Every pending operation is cancelled AND drained before its stack storage dies.
inline bool Transfer(HANDLE pipe, void* data, DWORD count, bool write, HANDLE stop, DWORD timeout = 5000) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;
    DWORD total = 0;
    const ULONGLONG deadline = GetTickCount64() + timeout;
    while (total < count) {
        ResetEvent(ov.hEvent);
        DWORD done = 0;
        BOOL ok = write ? WriteFile(pipe, static_cast<char*>(data) + total, count - total, nullptr, &ov)
                        : ReadFile(pipe, static_cast<char*>(data) + total, count - total, nullptr, &ov);
        if (!ok && GetLastError() != ERROR_IO_PENDING) break;
        if (!ok) {
            HANDLE waits[] = {ov.hEvent, stop};
            const ULONGLONG now = GetTickCount64();
            DWORD remaining = timeout == INFINITE ? INFINITE :
                static_cast<DWORD>(deadline > now ? deadline - now : 0);
            if (WaitForMultipleObjects(stop ? 2 : 1, waits, FALSE, remaining) != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &ov);
                GetOverlappedResult(pipe, &ov, &done, TRUE);
                break;
            }
        }
        if (!GetOverlappedResult(pipe, &ov, &done, FALSE) || !done) break;
        total += done;
    }
    CloseHandle(ov.hEvent);
    return total == count;
}
inline bool Connect(HANDLE pipe, HANDLE stop) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool ok = ConnectNamedPipe(pipe, &ov) != FALSE;
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED) ok = true;
        else if (err == ERROR_IO_PENDING) {
            HANDLE waits[] = {ov.hEvent, stop};
            DWORD bytes = 0;
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0)
                ok = GetOverlappedResult(pipe, &ov, &bytes, FALSE) != FALSE;
            else { CancelIoEx(pipe, &ov); GetOverlappedResult(pipe, &ov, &bytes, TRUE); }
        }
    }
    CloseHandle(ov.hEvent);
    return ok;
}
}
