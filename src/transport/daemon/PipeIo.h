#pragma once
#if defined(_WIN32)
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
#else
// ---------------------------------------------------------------------------
// Linux: ayni pipeio arayuzu + daemon kodunun kullandigi Win32 named-pipe
// fonksiyonlarinin UNIX domain socket karsiliklari. Boylece PtySession,
// PtySessionClient ve SessionDaemon Windows ile ayni kaynaktan derleniyor.
//   \\.\pipe\FullTerminal.Daemon.<scope>      -> $XDG_RUNTIME_DIR/fullterminal/d.<scope>.sock
//   \\.\pipe\FullTerminal.Session.<scope>.<id> -> .../s.<scope>.<id>.sock
// Guvenlik: dizin 0700, soket 0600 ve accept sonrasi SO_PEERCRED ile ayni uid
// kontrolu (Windows'taki "yalnizca bu kullanici" DACL'inin karsiligi).
// ---------------------------------------------------------------------------
#include "core/Platform.h"
#include <string>
#include <vector>
#include <cwctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <random>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0u
#endif
#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND 2u
#endif
#ifndef ERROR_ACCESS_DENIED
#define ERROR_ACCESS_DENIED 5u
#endif
#ifndef ERROR_BROKEN_PIPE
#define ERROR_BROKEN_PIPE 109u
#endif
#ifndef ERROR_PIPE_BUSY
#define ERROR_PIPE_BUSY 231u
#endif
#ifndef ERROR_ALREADY_EXISTS
#define ERROR_ALREADY_EXISTS 183u
#endif
#ifndef ERROR_PIPE_CONNECTED
#define ERROR_PIPE_CONNECTED 535u
#endif
#ifndef ERROR_IO_PENDING
#define ERROR_IO_PENDING 997u
#endif
#ifndef GENERIC_READ
#define GENERIC_READ 0x80000000u
#endif
#ifndef GENERIC_WRITE
#define GENERIC_WRITE 0x40000000u
#endif
#ifndef OPEN_EXISTING
#define OPEN_EXISTING 3u
#endif
#ifndef FILE_FLAG_OVERLAPPED
#define FILE_FLAG_OVERLAPPED 0x40000000u
#endif
#ifndef FILE_FLAG_FIRST_PIPE_INSTANCE
#define FILE_FLAG_FIRST_PIPE_INSTANCE 0x00080000u
#endif
#ifndef PIPE_ACCESS_DUPLEX
#define PIPE_ACCESS_DUPLEX 0x3u
#endif
#ifndef PIPE_TYPE_BYTE
#define PIPE_TYPE_BYTE 0x0u
#endif
#ifndef PIPE_READMODE_BYTE
#define PIPE_READMODE_BYTE 0x0u
#endif
#ifndef PIPE_WAIT
#define PIPE_WAIT 0x0u
#endif
#ifndef PIPE_REJECT_REMOTE_CLIENTS
#define PIPE_REJECT_REMOTE_CLIENTS 0x8u
#endif
#ifndef STARTF_USESHOWWINDOW
#define STARTF_USESHOWWINDOW 0x1u
#endif
#ifndef CREATE_NO_WINDOW
#define CREATE_NO_WINDOW 0x08000000u
#endif
#ifndef CREATE_NEW_PROCESS_GROUP
#define CREATE_NEW_PROCESS_GROUP 0x00000200u
#endif

struct SECURITY_ATTRIBUTES { DWORD nLength; void* lpSecurityDescriptor; BOOL bInheritHandle; };
struct STARTUPINFOW { DWORD cb = 0; DWORD dwFlags = 0; WORD wShowWindow = 0; };
struct PROCESS_INFORMATION { HANDLE hProcess = nullptr; HANDLE hThread = nullptr; DWORD dwProcessId = 0; DWORD dwThreadId = 0; };
struct GUID { uint32_t Data1; uint16_t Data2; uint16_t Data3; uint8_t Data4[8]; };

inline thread_local DWORD g_ftLastError = 0;
inline DWORD GetLastError() { return g_ftLastError; }
inline void SetLastError(DWORD e) { g_ftLastError = e; }

namespace ft::pipeio {
constexpr DWORD MaxFrame = 4 * 1024 * 1024;

inline std::string Narrow(const std::wstring& w) {
    std::string out;
    for (wchar_t c : w) {
        const uint32_t cp = (uint32_t)c;
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    return out;
}

// Her tasinabilir kurulum ve kullanici kendi isim alanina sahip (Windows ile ayni fikir).
// AppImage her calistirmada farkli bir /tmp/.mount_* altindan calistigi icin $APPIMAGE yolu esas alinir.
inline std::wstring Scope() {
    std::string exe;
    if (const char* appimage = getenv("APPIMAGE"); appimage && *appimage) exe = appimage;
    else {
        char buf[4096]{};
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) exe.assign(buf, (size_t)n);
    }
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : exe) { hash ^= c; hash *= 1099511628211ull; }
    const std::string uid = std::to_string((unsigned long)getuid());
    for (unsigned char c : uid) { hash ^= c; hash *= 1099511628211ull; }
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

// Soketlerin yasadigi kullaniciya ozel dizin (0700, sahibi biz olmali).
inline std::string RuntimeDir() {
    std::string base;
    if (const char* x = getenv("XDG_RUNTIME_DIR"); x && *x) base = std::string(x) + "/fullterminal";
    else base = "/tmp/fullterminal-" + std::to_string((unsigned long)getuid());
    ::mkdir(base.c_str(), 0700);
    struct stat st{};
    if (::lstat(base.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) return {};
    if ((st.st_mode & 077) != 0) ::chmod(base.c_str(), 0700);
    return base;
}
// "\\.\pipe\FullTerminal.Session.X.Y" -> "<dir>/s.X.Y.sock" (sun_path 108 bayta sigmali)
inline std::string SocketPath(const wchar_t* name) {
    if (!name) return {};
    std::wstring n(name);
    const std::wstring prefix = L"\\\\.\\pipe\\";
    if (n.rfind(prefix, 0) == 0) n = n.substr(prefix.size());
    auto shorten = [&](const std::wstring& from, const std::wstring& to) {
        if (n.rfind(from, 0) == 0) n = to + n.substr(from.size());
    };
    shorten(L"FullTerminal.Session.", L"s.");
    shorten(L"FullTerminal.Daemon.", L"d.");
    for (auto& c : n) if (c == L'/' || c == L'\\') c = L'_';
    const std::string dir = RuntimeDir();
    if (dir.empty()) return {};
    std::string path = dir + "/" + Narrow(n) + ".sock";
    if (path.size() >= sizeof(sockaddr_un{}.sun_path)) {
        uint64_t hash = 14695981039346656037ull;
        for (wchar_t c : n) { hash ^= (uint32_t)c; hash *= 1099511628211ull; }
        char buf[40]; snprintf(buf, sizeof(buf), "h.%016llx.sock", (unsigned long long)hash);
        path = dir + "/" + buf;
    }
    return path;
}

struct Security {
    void* descriptor = (void*)1; // Linux'ta izinler dizin/soket modlari + SO_PEERCRED ile saglanir
    SECURITY_ATTRIBUTES attrs{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
};

struct LinuxThreadListener {
    int listenFd = -1;
    std::string path;
    ino_t inode = 0;
    void Close() {
        if (listenFd >= 0) {
            ::close(listenFd);
            listenFd = -1;
            struct stat st{};
            if (!path.empty() && ::stat(path.c_str(), &st) == 0 && st.st_ino == inode) {
                ::unlink(path.c_str());
            }
            path.clear();
            inode = 0;
        }
    }
    ~LinuxThreadListener() { Close(); }
};
inline thread_local LinuxThreadListener g_threadListener;

// Named pipe karsiligi: sunucu (dinleyen + bagli soket) veya istemci (bagli soket).
struct LinuxPipe : LinuxKernelObject {
    int listenFd = -1;
    int connFd = -1;
    bool server = false;
    bool disconnected = false;
    std::string path;
    ino_t inode = 0;
    ~LinuxPipe() override {
        if (connFd >= 0) ::close(connFd);
        if (server && !disconnected && g_threadListener.listenFd == listenFd) {
            g_threadListener.Close();
        }
    }
    int PollFd() const override { return connFd >= 0 ? connFd : listenFd; }
};

inline int StopFd(HANDLE stop) {
    auto ev = LinuxHandleAs<LinuxEvent>(stop);
    return ev ? ev->PollFd() : -1;
}
inline bool StopSignaled(HANDLE stop) {
    auto ev = LinuxHandleAs<LinuxEvent>(stop);
    if (!ev) return false;
    std::lock_guard<std::mutex> lock(ev->mtx);
    return ev->signaled;
}

// Every pending operation is cancelled AND drained before its stack storage dies.
inline bool Transfer(HANDLE pipe, void* data, DWORD count, bool write, HANDLE stop, DWORD timeout = 5000) {
    auto p = LinuxHandleAs<LinuxPipe>(pipe);
    if (!p || p->connFd < 0) return false;
    const int stopFd = StopFd(stop);
    DWORD total = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout == INFINITE ? 0 : timeout);
    while (total < count) {
        if (StopSignaled(stop)) return false;
        const ssize_t n = write
            ? ::send(p->connFd, static_cast<char*>(data) + total, count - total, MSG_NOSIGNAL | MSG_DONTWAIT)
            : ::recv(p->connFd, static_cast<char*>(data) + total, count - total, MSG_DONTWAIT);
        if (n > 0) { total += (DWORD)n; continue; }
        if (n == 0 && !write) { SetLastError(ERROR_BROKEN_PIPE); return false; }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { SetLastError(ERROR_BROKEN_PIPE); return false; }
        int wait = -1;
        if (timeout != INFINITE) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) return false;
            wait = (int)left;
        }
        pollfd fds[2] = {{p->connFd, (short)(write ? POLLOUT : POLLIN), 0}, {stopFd, POLLIN, 0}};
        const int r = ::poll(fds, stopFd >= 0 ? 2 : 1, wait);
        if (r < 0 && errno != EINTR) return false;
        if (stopFd >= 0 && (fds[1].revents & POLLIN)) return false;
    }
    return true;
}
inline bool Connect(HANDLE pipe, HANDLE stop) {
    auto p = LinuxHandleAs<LinuxPipe>(pipe);
    if (!p || p->listenFd < 0) return false;
    if (p->connFd >= 0) return true;
    const int stopFd = StopFd(stop);
    for (;;) {
        if (StopSignaled(stop)) return false;
        pollfd fds[2] = {{p->listenFd, POLLIN, 0}, {stopFd, POLLIN, 0}};
        const int r = ::poll(fds, stopFd >= 0 ? 2 : 1, -1);
        if (r < 0) { if (errno == EINTR) continue; return false; }
        if (stopFd >= 0 && (fds[1].revents & POLLIN)) return false;
        if (!(fds[0].revents & POLLIN)) continue;
        const int fd = ::accept4(p->listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) { if (errno == EINTR || errno == EAGAIN || errno == ECONNABORTED) continue; return false; }
        ucred cred{}; socklen_t len = sizeof(cred);
        if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0 || cred.uid != getuid()) {
            ::close(fd); // baska kullanici: reddet ve beklemeye devam et
            continue;
        }
        p->connFd = fd;
        return true;
    }
}
} // namespace ft::pipeio

// ---- Daemon kodunun kullandigi Win32 fonksiyonlarinin Linux karsiliklari ----
inline HANDLE CreateNamedPipeW(const wchar_t* name, DWORD openMode, DWORD, DWORD, DWORD, DWORD, DWORD, SECURITY_ATTRIBUTES*) {
    const std::string path = ft::pipeio::SocketPath(name);
    if (path.empty()) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    auto& tl = ft::pipeio::g_threadListener;
    if (tl.listenFd >= 0 && tl.path == path) {
        auto pipe = std::make_shared<ft::pipeio::LinuxPipe>();
        pipe->listenFd = tl.listenFd;
        pipe->server = true;
        pipe->path = tl.path;
        pipe->inode = tl.inode;
        SetLastError(ERROR_SUCCESS);
        return LinuxRegisterHandle(pipe);
    }
    tl.Close();
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return INVALID_HANDLE_VALUE;
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    const mode_t old = ::umask(0177);
    int rc = ::bind(fd, (sockaddr*)&addr, sizeof(addr));
    if (rc != 0 && errno == EADDRINUSE) {
        // Dosya var: canli bir ornek mi, yoksa coken surecten kalan bayat soket mi?
        const int probe = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        const bool alive = probe >= 0 && ::connect(probe, (sockaddr*)&addr, sizeof(addr)) == 0;
        if (probe >= 0) ::close(probe);
        if (alive && (openMode & FILE_FLAG_FIRST_PIPE_INSTANCE)) {
            ::umask(old); ::close(fd); SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE;
        }
        if (!alive) { ::unlink(path.c_str()); rc = ::bind(fd, (sockaddr*)&addr, sizeof(addr)); }
    }
    ::umask(old);
    if (rc != 0 || ::listen(fd, 16) != 0) { ::close(fd); SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    ::chmod(path.c_str(), 0600);
    tl.listenFd = fd;
    tl.path = path;
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) tl.inode = st.st_ino;
    auto pipe = std::make_shared<ft::pipeio::LinuxPipe>();
    pipe->listenFd = fd; pipe->server = true; pipe->path = path; pipe->inode = tl.inode;
    SetLastError(ERROR_SUCCESS);
    return LinuxRegisterHandle(pipe);
}
inline BOOL DisconnectNamedPipe(HANDLE h) {
    auto p = LinuxHandleAs<ft::pipeio::LinuxPipe>(h);
    if (!p) return FALSE;
    p->disconnected = true;
    if (p->connFd >= 0) { ::shutdown(p->connFd, SHUT_RDWR); ::close(p->connFd); p->connFd = -1; }
    return TRUE;
}
// Istemci tarafi: yalnizca \\.\pipe\ isimlerini destekler.
inline HANDLE CreateFileW(const wchar_t* name, DWORD, DWORD, SECURITY_ATTRIBUTES*, DWORD, DWORD, HANDLE) {
    const std::string path = ft::pipeio::SocketPath(name);
    if (path.empty()) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
    if (st.st_uid != getuid()) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return INVALID_HANDLE_VALUE;
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        const int e = errno;
        ::close(fd);
        if (e == ENOENT) {
            SetLastError(ERROR_FILE_NOT_FOUND);
        } else if (e == EACCES || e == EPERM) {
            SetLastError(ERROR_ACCESS_DENIED);
        } else if (e == ECONNREFUSED) {
            // pkill / SIGKILL / cokme sonrasi diskte bayat .sock kalmis olabilir:
            // Daemon flock kilidi sahipsizse bayat soketi sil ve ERROR_FILE_NOT_FOUND don ki yeni daemon baslatilsin.
            bool daemonAlive = false;
            const std::string dir = ft::pipeio::RuntimeDir();
            if (!dir.empty()) {
                const std::string lockPath = dir + "/Local_FullTerminal.Daemon." + ft::pipeio::Narrow(ft::pipeio::Scope()) + ".lock";
                const int lfd = ::open(lockPath.c_str(), O_RDWR | O_CLOEXEC, 0600);
                if (lfd >= 0) {
                    if (::flock(lfd, LOCK_EX | LOCK_NB) == 0) {
                        ::flock(lfd, LOCK_UN);
                    } else {
                        daemonAlive = true;
                    }
                    ::close(lfd);
                }
            }
            if (!daemonAlive) {
                ::unlink(path.c_str());
                SetLastError(ERROR_FILE_NOT_FOUND);
            } else {
                SetLastError(ERROR_PIPE_BUSY);
            }
        } else {
            SetLastError(ERROR_PIPE_BUSY);
        }
        return INVALID_HANDLE_VALUE;
    }
    auto pipe = std::make_shared<ft::pipeio::LinuxPipe>();
    pipe->connFd = fd;
    SetLastError(ERROR_SUCCESS);
    return LinuxRegisterHandle(pipe);
}
inline BOOL WaitNamedPipeW(const wchar_t* name, DWORD ms) {
    const std::string path = ft::pipeio::SocketPath(name);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
        struct stat st{};
        if (!path.empty() && ::stat(path.c_str(), &st) == 0) return TRUE;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    return FALSE;
}
// Okunabilir bayt sayisi; karsi taraf kapandiysa FALSE (Windows: ERROR_BROKEN_PIPE).
inline BOOL PeekNamedPipe(HANDLE h, void*, DWORD, DWORD*, DWORD* available, DWORD*) {
    auto sp = LinuxHandleAs<ft::pipeio::LinuxPipe>(h);
    ft::pipeio::LinuxPipe* p = sp.get();
    if (!p || p->connFd < 0) { SetLastError(ERROR_BROKEN_PIPE); return FALSE; }
    int n = 0;
    if (::ioctl(p->connFd, FIONREAD, &n) != 0) { SetLastError(ERROR_BROKEN_PIPE); return FALSE; }
    if (n == 0) {
        pollfd fd{p->connFd, POLLIN | POLLRDHUP, 0};
        if (::poll(&fd, 1, 0) > 0 && (fd.revents & (POLLHUP | POLLERR | POLLRDHUP | POLLIN))) {
            char c;
            const ssize_t r = ::recv(p->connFd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
            if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) { SetLastError(ERROR_BROKEN_PIPE); return FALSE; }
            if (r > 0) n = 1;
        }
    }
    if (available) *available = (DWORD)n;
    return TRUE;
}
// Tek ornek kilidi: Windows'taki isimli mutex yerine flock.
struct LinuxNamedMutex : LinuxKernelObject {
    int fd = -1;
    ~LinuxNamedMutex() override { if (fd >= 0) { ::flock(fd, LOCK_UN); ::close(fd); } }
};
inline HANDLE CreateMutexW(SECURITY_ATTRIBUTES*, BOOL, const wchar_t* name) {
    std::wstring n = name ? name : L"FullTerminal.mutex";
    for (auto& c : n) if (c == L'\\' || c == L'/') c = L'_';
    const std::string dir = ft::pipeio::RuntimeDir();
    if (dir.empty()) return nullptr;
    const std::string path = dir + "/" + ft::pipeio::Narrow(n) + ".lock";
    auto m = std::make_shared<LinuxNamedMutex>();
    m->fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (m->fd < 0) return nullptr;
    const bool owned = ::flock(m->fd, LOCK_EX | LOCK_NB) == 0;
    SetLastError(owned ? ERROR_SUCCESS : ERROR_ALREADY_EXISTS);
    if (!owned) { ::close(m->fd); m->fd = -1; }
    return LinuxRegisterHandle(m);
}
// Sadece daemon baslatmak icin: "\"exe\" --daemon" -> cift fork + setsid, tamamen ayrik.
inline BOOL CreateProcessW(const wchar_t* app, wchar_t* cmdLine, SECURITY_ATTRIBUTES*, SECURITY_ATTRIBUTES*, BOOL,
                           DWORD, void*, const wchar_t*, STARTUPINFOW*, PROCESS_INFORMATION* pi) {
    std::vector<std::string> args;
    {
        const std::wstring cl = cmdLine ? cmdLine : L"";
        std::wstring cur; bool quoted = false, any = false;
        for (wchar_t c : cl) {
            if (c == L'"') { quoted = !quoted; any = true; continue; }
            if (!quoted && (c == L' ' || c == L'\t')) { if (any || !cur.empty()) args.push_back(ft::pipeio::Narrow(cur)); cur.clear(); any = false; continue; }
            cur += c;
        }
        if (any || !cur.empty()) args.push_back(ft::pipeio::Narrow(cur));
    }
    std::string exe = app ? ft::pipeio::Narrow(app) : (args.empty() ? std::string() : args[0]);
    // AppImage icinden calisiyorsak daemon'u AppImage dosyasi uzerinden baslat: GUI kapaninca
    // /tmp/.mount_* baglantisi kalkar, daemon yine de calisabilsin.
    if (const char* appimage = getenv("APPIMAGE"); appimage && *appimage) exe = appimage;
    if (exe.empty()) return FALSE;
    if (args.empty()) args.push_back(exe);
    const pid_t pid = ::fork();
    if (pid < 0) return FALSE;
    if (pid == 0) {
        ::setsid();
        const pid_t inner = ::fork();
        if (inner != 0) _exit(inner < 0 ? 1 : 0);
        const int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) { ::dup2(devnull, 0); ::dup2(devnull, 1); ::dup2(devnull, 2); if (devnull > 2) ::close(devnull); }
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        ::execv(exe.c_str(), argv.data());
        _exit(127);
    }
    int status = 0;
    ::waitpid(pid, &status, 0); // ara surec hemen cikar; daemon init'e devredilir
    if (pi) *pi = PROCESS_INFORMATION{};
    return TRUE;
}
inline HRESULT CoCreateGuid(GUID* g) {
    if (!g) return (HRESULT)0x80070057L;
    std::random_device rd;
    uint8_t b[16];
    for (auto& x : b) x = (uint8_t)rd();
    b[6] = (uint8_t)((b[6] & 0x0F) | 0x40); b[8] = (uint8_t)((b[8] & 0x3F) | 0x80);
    g->Data1 = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    g->Data2 = (uint16_t)(b[4] << 8 | b[5]);
    g->Data3 = (uint16_t)(b[6] << 8 | b[7]);
    for (int i = 0; i < 8; ++i) g->Data4[i] = b[8 + i];
    return S_OK;
}
inline int StringFromGUID2(const GUID& g, wchar_t* out, int cch) {
    if (!out || cch < 39) return 0;
    swprintf(out, (size_t)cch, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
             g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
             g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return 39;
}
#endif
