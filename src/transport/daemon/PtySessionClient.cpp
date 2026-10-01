#include "transport/daemon/PtySessionClient.h"
#include "core/Utf8.h"
#include <iostream>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <fcntl.h>
#endif

namespace ft {

#if defined(_WIN32)
namespace {

bool ReadExact(HANDLE hPipe, void* dst, DWORD count, HANDLE hStop, OVERLAPPED& ov) {
    DWORD total = 0;
    while (total < count) {
        ResetEvent(ov.hEvent);
        DWORD chunk = 0;
        BOOL ok = ReadFile(hPipe, static_cast<char*>(dst) + total, count - total, nullptr, &ov);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waitHandles[2] = { ov.hEvent, hStop };
                DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (waitRes != WAIT_OBJECT_0) {
                    CancelIo(hPipe);
                    return false;
                }
            } else {
                return false;
            }
        }
        if (!GetOverlappedResult(hPipe, &ov, &chunk, TRUE) || chunk == 0) {
            return false;
        }
        total += chunk;
    }
    return true;
}

} // namespace
#endif

namespace {
std::string GetLinuxSessionSocket(const std::string& id) {
    const char* user = getenv("USER");
    if (!user) user = "default";
    return "/tmp/ft-" + std::string(user) + "/session-" + id + ".sock";
}
}

PtySessionClient::PtySessionClient() {
#if defined(_WIN32)
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#endif
}

PtySessionClient::~PtySessionClient() {
    Detach();
#if defined(_WIN32)
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
#endif
}

bool PtySessionClient::Attach(const std::string& sessionId, std::wstring* errorOut) {
    Detach();

    m_sessionId = sessionId;

#if defined(_WIN32)
    const std::wstring pipeName = L"\\\\.\\pipe\\FullTerminal.Session." + Utf8ToWide(sessionId);

    bool pipeReady = false;
    for (int i = 0; i < 50; ++i) {
        if (WaitNamedPipeW(pipeName.c_str(), 50)) {
            pipeReady = true;
            break;
        }
        Sleep(20);
    }

    if (!pipeReady) {
        if (errorOut) *errorOut = L"Oturum borusu (pipe) bulunamadi: " + pipeName;
        return false;
    }

    m_pipe = CreateFileW(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (m_pipe == INVALID_HANDLE_VALUE) {
        if (errorOut) *errorOut = L"Oturum borusuna baglanilamadi (kod " + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    ResetEvent(m_stopEvent);
    m_connected = true;

    SendFrame(PtyMsgType::Attach, nullptr, 0);
    m_readerThread = std::thread(&PtySessionClient::ReaderLoop, this);
    return true;
#else
    std::string sockPath = GetLinuxSessionSocket(sessionId);
    int fd = -1;

    for (int i = 0; i < 50; ++i) {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0) {
            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);
            if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                break;
            }
            close(fd);
            fd = -1;
        }
        usleep(30000); // 30ms
    }

    if (fd < 0) {
        if (errorOut) *errorOut = L"Oturum soketi bulunamadi: " + Utf8ToWide(sockPath);
        return false;
    }

    m_pipe = (HANDLE)(intptr_t)fd;
    m_connected = true;

    SendFrame(PtyMsgType::Attach, nullptr, 0);
    m_readerThread = std::thread(&PtySessionClient::ReaderLoop, this);
    return true;
#endif
}

void PtySessionClient::Write(const char* data, size_t len) {
    if (!m_connected || !data || len == 0) return;
    SendFrame(PtyMsgType::Input, data, len);
}

void PtySessionClient::Resize(short cols, short rows) {
    if (!m_connected) return;
    short sz[2] = { cols, rows };
    SendFrame(PtyMsgType::Resize, sz, sizeof(sz));
}

void PtySessionClient::Detach() {
    if (m_connected) {
        SendFrame(PtyMsgType::Detach, nullptr, 0);
        m_connected = false;
    }

#if defined(_WIN32)
    if (m_stopEvent) {
        SetEvent(m_stopEvent);
    }
    if (m_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_pipe, nullptr);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
#else
    if (m_pipe != INVALID_HANDLE_VALUE) {
        int fd = (int)(intptr_t)m_pipe;
        shutdown(fd, SHUT_RDWR);
        close(fd);
        m_pipe = INVALID_HANDLE_VALUE;
    }
#endif

    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
}

void PtySessionClient::Kill() {
    if (m_connected) {
        SendFrame(PtyMsgType::Kill, nullptr, 0);
        m_connected = false;
    }
    Detach();
}

bool PtySessionClient::SendFrame(PtyMsgType type, const void* data, size_t len) {
    if (m_pipe == INVALID_HANDLE_VALUE) return false;

    PtyFrameHeader hdr{};
    hdr.type = static_cast<uint32_t>(type);
    hdr.length = static_cast<uint32_t>(len);

    std::vector<uint8_t> frame(sizeof(PtyFrameHeader) + len);
    memcpy(frame.data(), &hdr, sizeof(PtyFrameHeader));
    if (len > 0 && data) {
        memcpy(frame.data() + sizeof(PtyFrameHeader), data, len);
    }

#if defined(_WIN32)
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD written = 0;
    BOOL ok = WriteFile(m_pipe, frame.data(), static_cast<DWORD>(frame.size()), &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        GetOverlappedResult(m_pipe, &ov, &written, TRUE);
    }
    CloseHandle(ov.hEvent);
    return written == frame.size();
#else
    int fd = (int)(intptr_t)m_pipe;
    ssize_t n = write(fd, frame.data(), frame.size());
    return n == (ssize_t)frame.size();
#endif
}

void PtySessionClient::ReaderLoop() {
#if defined(_WIN32)
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (m_connected && m_pipe != INVALID_HANDLE_VALUE) {
        PtyFrameHeader hdr{};
        if (!ReadExact(m_pipe, &hdr, sizeof(PtyFrameHeader), m_stopEvent, ov)) {
            break;
        }

        std::vector<char> payload(hdr.length);
        if (hdr.length > 0) {
            if (!ReadExact(m_pipe, payload.data(), hdr.length, m_stopEvent, ov)) {
                break;
            }
        }

        const auto type = static_cast<PtyMsgType>(hdr.type);
        if (type == PtyMsgType::Output) {
            if (onOutput && !payload.empty()) {
                onOutput(payload.data(), payload.size());
            }
        } else if (type == PtyMsgType::Exit) {
            DWORD code = 0;
            if (payload.size() >= sizeof(DWORD)) {
                memcpy(&code, payload.data(), sizeof(DWORD));
            }
            if (onExit) onExit(code);
            break;
        }
    }

    CloseHandle(ov.hEvent);
#else
    int fd = (int)(intptr_t)m_pipe;
    while (m_connected && fd >= 0) {
        PtyFrameHeader hdr{};
        ssize_t n = read(fd, &hdr, sizeof(PtyFrameHeader));
        if (n != sizeof(PtyFrameHeader)) break;

        std::vector<char> payload(hdr.length);
        if (hdr.length > 0) {
            size_t got = 0;
            while (got < hdr.length) {
                ssize_t pr = read(fd, payload.data() + got, hdr.length - got);
                if (pr <= 0) break;
                got += pr;
            }
            if (got < hdr.length) break;
        }

        const auto type = static_cast<PtyMsgType>(hdr.type);
        if (type == PtyMsgType::Output) {
            if (onOutput && !payload.empty()) {
                onOutput(payload.data(), payload.size());
            }
        } else if (type == PtyMsgType::Exit) {
            DWORD code = 0;
            if (payload.size() >= sizeof(DWORD)) {
                memcpy(&code, payload.data(), sizeof(DWORD));
            }
            if (onExit) onExit(code);
            break;
        }
    }
#endif
    m_connected = false;
}

} // namespace ft
