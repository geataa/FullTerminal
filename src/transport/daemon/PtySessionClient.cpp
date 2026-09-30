#include "transport/daemon/PtySessionClient.h"
#include "core/Utf8.h"
#include <iostream>

namespace ft {

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

PtySessionClient::PtySessionClient() {
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

PtySessionClient::~PtySessionClient() {
    Detach();
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

bool PtySessionClient::Attach(const std::string& sessionId, std::wstring* errorOut) {
    Detach();

    m_sessionId = sessionId;
    const std::wstring pipeName = L"\\\\.\\pipe\\FullTerminal.Session." + Utf8ToWide(sessionId);

    // Named pipe sunucusunun hazir olmasini bekle (pipe olusana kadar dene)
    bool pipeReady = false;
    for (int i = 0; i < 50; ++i) {
        if (WaitNamedPipeW(pipeName.c_str(), 50)) {
            pipeReady = true;
            break;
        }
        Sleep(50);
    }

    if (!pipeReady) {
        if (errorOut) *errorOut = L"Oturum pipe'i bulunamadi veya zaman asimina ugradi: " + pipeName;
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
        if (errorOut) *errorOut = L"Oturum pipe'i acilamadi (hata: " + std::to_wstring(GetLastError()) + L")";
        return false;
    }

    ResetEvent(m_stopEvent);
    m_connected = true;

    // Attach istegi gonder (gecmis replay'i tetikler)
    SendFrame(PtyMsgType::Attach, nullptr, 0);

    m_readerThread = std::thread(&PtySessionClient::ReaderLoop, this);
    return true;
}

void PtySessionClient::Write(const char* data, size_t len) {
    if (m_connected && len > 0) {
        SendFrame(PtyMsgType::Input, data, len);
    }
}

void PtySessionClient::Resize(short cols, short rows) {
    if (m_connected) {
        short dims[2] = { cols, rows };
        SendFrame(PtyMsgType::Resize, dims, sizeof(dims));
    }
}

void PtySessionClient::Detach() {
    if (!m_connected) return;

    SendFrame(PtyMsgType::Detach, nullptr, 0);

    m_connected = false;
    if (m_stopEvent) SetEvent(m_stopEvent);

    if (m_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_pipe, nullptr);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }

    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
}

void PtySessionClient::Kill() {
    if (!m_connected) return;

    SendFrame(PtyMsgType::Kill, nullptr, 0);

    m_connected = false;
    if (m_stopEvent) SetEvent(m_stopEvent);

    if (m_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_pipe, nullptr);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }

    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
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

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD written = 0;
    BOOL ok = WriteFile(m_pipe, frame.data(), static_cast<DWORD>(frame.size()), &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        GetOverlappedResult(m_pipe, &ov, &written, TRUE);
    }
    CloseHandle(ov.hEvent);
    return true;
}

void PtySessionClient::ReaderLoop() {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (m_connected) {
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
            DWORD exitCode = 0;
            if (payload.size() >= sizeof(DWORD)) {
                memcpy(&exitCode, payload.data(), sizeof(DWORD));
            }
            if (onExit) {
                onExit(exitCode);
            }
            break;
        }
    }

    CloseHandle(ov.hEvent);
    m_connected = false;
}

} // namespace ft
