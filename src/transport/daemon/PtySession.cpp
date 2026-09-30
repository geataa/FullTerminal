#include "transport/daemon/PtySession.h"
#include "core/Utf8.h"
#include <algorithm>
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

PtySession::PtySession(const std::string& sessionId)
    : m_id(sessionId) {
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

PtySession::~PtySession() {
    Terminate();
}

std::wstring PtySession::PipeName() const {
    return L"\\\\.\\pipe\\FullTerminal.Session." + Utf8ToWide(m_id);
}

bool PtySession::Start(const std::wstring& commandLine,
                       const std::wstring& startDir,
                       const std::vector<std::pair<std::wstring, std::wstring>>& envOverrides,
                       short cols, short rows,
                       std::wstring* errorOut) {
    m_shouldTerminate = false;
    ResetEvent(m_stopEvent);

    m_pty.onOutput = [this](const char* data, size_t len) {
        if (len == 0) return;
        {
            std::lock_guard<std::mutex> lock(m_bufMutex);
            for (size_t i = 0; i < len; ++i) {
                m_historyBuffer.push_back(data[i]);
            }
            while (m_historyBuffer.size() > kMaxHistoryBytes) {
                m_historyBuffer.pop_front();
            }
        }
        EnqueueFrame(PtyMsgType::Output, data, len);
    };

    m_pty.onExit = [this](DWORD code) {
        EnqueueFrame(PtyMsgType::Exit, &code, sizeof(DWORD));
        m_running = false;
    };

    if (!m_pty.Start(commandLine, startDir, envOverrides, cols, rows, errorOut)) {
        return false;
    }

    m_running = true;
    m_senderThread = std::thread(&PtySession::SenderLoop, this);
    m_pipeThread = std::thread(&PtySession::ServerPipeLoop, this);
    return true;
}

void PtySession::Write(const char* data, size_t len) {
    if (m_running && len > 0) {
        m_pty.Write(data, len);
    }
}

void PtySession::Resize(short cols, short rows) {
    if (m_running) {
        m_pty.Resize(cols, rows);
    }
}

void PtySession::Terminate() {
    m_shouldTerminate = true;
    if (m_stopEvent) {
        SetEvent(m_stopEvent);
    }

    m_queueCv.notify_all();

    if (m_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_pipe, nullptr);
        DisconnectNamedPipe(m_pipe);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }

    m_pty.Close();

    if (m_pipeThread.joinable()) {
        m_pipeThread.join();
    }
    if (m_senderThread.joinable()) {
        m_senderThread.join();
    }

    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

bool PtySession::IsAlive() const {
    return m_running.load() && m_pty.Running();
}

std::string PtySession::GetHistoryReplay() const {
    std::lock_guard<std::mutex> lock(m_bufMutex);
    return std::string(m_historyBuffer.begin(), m_historyBuffer.end());
}

void PtySession::EnqueueFrame(PtyMsgType type, const void* data, size_t len) {
    PtyFrameHeader hdr{};
    hdr.type = static_cast<uint32_t>(type);
    hdr.length = static_cast<uint32_t>(len);

    std::vector<uint8_t> frame(sizeof(PtyFrameHeader) + len);
    memcpy(frame.data(), &hdr, sizeof(PtyFrameHeader));
    if (len > 0 && data) {
        memcpy(frame.data() + sizeof(PtyFrameHeader), data, len);
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_sendQueue.push_back(std::move(frame));
    }
    m_queueCv.notify_one();
}

void PtySession::SenderLoop() {
    while (!m_shouldTerminate) {
        std::vector<uint8_t> frame;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this] {
                return !m_sendQueue.empty() || m_shouldTerminate.load();
            });
            if (m_shouldTerminate) break;
            frame = std::move(m_sendQueue.front());
            m_sendQueue.pop_front();
        }

        if (m_hasClient && m_pipe != INVALID_HANDLE_VALUE) {
            OVERLAPPED ovWrite{};
            ovWrite.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            DWORD written = 0;
            BOOL bOk = WriteFile(m_pipe, frame.data(), static_cast<DWORD>(frame.size()), &written, &ovWrite);
            if (!bOk) {
                if (GetLastError() == ERROR_IO_PENDING) {
                    GetOverlappedResult(m_pipe, &ovWrite, &written, TRUE);
                } else {
                    m_hasClient = false;
                }
            }
            CloseHandle(ovWrite.hEvent);
        }
    }
}

void PtySession::ServerPipeLoop() {
    const std::wstring pipeName = PipeName();

    while (!m_shouldTerminate) {
        m_pipe = CreateNamedPipeW(
            pipeName.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536,
            65536,
            0,
            nullptr
        );

        if (m_pipe == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }

        OVERLAPPED ovConnect{};
        ovConnect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(m_pipe, &ovConnect);
        if (!connected) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waitHandles[2] = { ovConnect.hEvent, m_stopEvent };
                DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (waitRes == WAIT_OBJECT_0 + 1 || m_shouldTerminate) {
                    CancelIo(m_pipe);
                    CloseHandle(ovConnect.hEvent);
                    CloseHandle(m_pipe);
                    m_pipe = INVALID_HANDLE_VALUE;
                    break;
                }
                connected = TRUE;
            } else if (err == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
        }
        CloseHandle(ovConnect.hEvent);

        if (!connected) {
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
            continue;
        }

        m_hasClient = true;

        // Istemci baglandi: Gecmis tamponunu yeniden baglanan istemciye replay et
        std::string history = GetHistoryReplay();
        if (!history.empty()) {
            EnqueueFrame(PtyMsgType::Output, history.data(), history.size());
        }

        OVERLAPPED ovRead{};
        ovRead.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        // Istemciden komut/girdi okuma dongusu
        while (!m_shouldTerminate && m_hasClient) {
            PtyFrameHeader hdr{};
            if (!ReadExact(m_pipe, &hdr, sizeof(PtyFrameHeader), m_stopEvent, ovRead)) {
                m_hasClient = false;
                break;
            }

            std::vector<char> payload(hdr.length);
            if (hdr.length > 0) {
                if (!ReadExact(m_pipe, payload.data(), hdr.length, m_stopEvent, ovRead)) {
                    m_hasClient = false;
                    break;
                }
            }

            const auto type = static_cast<PtyMsgType>(hdr.type);
            if (type == PtyMsgType::Input) {
                if (!payload.empty()) {
                    m_pty.Write(payload.data(), payload.size());
                }
            } else if (type == PtyMsgType::Resize) {
                if (payload.size() >= sizeof(short) * 2) {
                    short cols = 0, rows = 0;
                    memcpy(&cols, payload.data(), sizeof(short));
                    memcpy(&rows, payload.data() + sizeof(short), sizeof(short));
                    m_pty.Resize(cols, rows);
                }
            } else if (type == PtyMsgType::Detach) {
                // Istemci bilincli olarak ayrildi: PTY surecini ARKADA YASATMAYA DEVAM ET
                m_hasClient = false;
                break;
            } else if (type == PtyMsgType::Kill) {
                // Istemci oturumu gercekten yok etmek istedi
                m_hasClient = false;
                m_shouldTerminate = true;
                m_pty.Close();
                break;
            } else if (type == PtyMsgType::Attach) {
                // Yeniden gecmis replay'i iste
                std::string currentHistory = GetHistoryReplay();
                if (!currentHistory.empty()) {
                    EnqueueFrame(PtyMsgType::Output, currentHistory.data(), currentHistory.size());
                }
            }
        }

        CloseHandle(ovRead.hEvent);

        DisconnectNamedPipe(m_pipe);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;

        // Eger ConPTY sureci bittiyse ve kimse dinlemiyorsa donguyu sonlandir
        if (!m_pty.Running() && m_shouldTerminate) {
            break;
        }
    }
}

} // namespace ft
