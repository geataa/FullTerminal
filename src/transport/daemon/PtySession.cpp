#include "transport/daemon/PtySession.h"
#include "transport/daemon/PipeIo.h"
#include "core/Utf8.h"
#include <cstring>

namespace ft {
PtySession::PtySession(const std::string& id) : m_id(id) {
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}
PtySession::~PtySession() { Terminate(); if (m_stopEvent) CloseHandle(m_stopEvent); }
std::wstring PtySession::PipeName() const { return pipeio::SessionName(m_id); }

bool PtySession::Start(const std::wstring& command, const std::wstring& cwd,
    const std::vector<std::pair<std::wstring, std::wstring>>& env, short cols, short rows, std::wstring* err) {
    if (!pipeio::ValidId(m_id) || !m_stopEvent) return false;
    m_startedAt = GetTickCount64();
    m_command = command; m_cwd = cwd; m_env = env;
    m_shouldTerminate = false;
    ResetEvent(m_stopEvent);
    m_pty.onOutput = [this](const char* data, size_t len) {
        std::lock_guard<std::mutex> lock(m_bufMutex);
        m_historyBuffer.insert(m_historyBuffer.end(), data, data + len);
        while (m_historyBuffer.size() > kMaxHistoryBytes) m_historyBuffer.pop_front();
        if (m_hasClient) EnqueueFrame(PtyMsgType::Output, data, len);
    };
    m_pty.onExit = [this](DWORD code) { PublishExit(code); };
    m_running = true;
    if (!m_pty.Start(command, cwd, env, cols, rows, err)) { m_running = false; return false; }
    m_pipeThread = std::thread(&PtySession::ServerPipeLoop, this);
    return true;
}
void PtySession::Write(const char* data, size_t len) { std::lock_guard<std::mutex> lock(m_ptyMutex); if (IsAlive()) m_pty.Write(data, len); }
void PtySession::Resize(short cols, short rows) { std::lock_guard<std::mutex> lock(m_ptyMutex); if (IsAlive()) m_pty.Resize(cols, rows); }
void PtySession::PublishExit(DWORD code) {
    if (!m_running.exchange(false)) return;
    std::lock_guard<std::mutex> lock(m_bufMutex);
    if (m_hasClient) EnqueueFrame(PtyMsgType::Exit, &code, sizeof(code));
}
void PtySession::PollExit() {
    std::lock_guard<std::mutex> lock(m_ptyMutex);
    if (m_running && m_pty.ProcessExited()) {
        m_pty.Close(); // drains final output; ConPty suppresses onExit on explicit Close
        PublishExit(m_pty.ExitCode());
    }
}
bool PtySession::IsAlive() const { return m_running && m_pty.Running(); }
json::Value PtySession::LaunchInfo() const {
    auto info = json::Value::Object();
    info["command"] = WideToUtf8(m_command); info["cwd"] = WideToUtf8(m_cwd);
    auto env = json::Value::Object();
    for (const auto& [key, value] : m_env) env[WideToUtf8(key)] = WideToUtf8(value);
    info["env"] = env;
    return info;
}
void PtySession::Terminate() {
    m_shouldTerminate = true;
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_pipeThread.joinable()) m_pipeThread.join();
    { std::lock_guard<std::mutex> lock(m_ptyMutex); m_pty.Close(); }
    m_running = false;
}
std::string PtySession::GetHistoryReplay() const {
    std::lock_guard<std::mutex> lock(m_bufMutex);
    return {m_historyBuffer.begin(), m_historyBuffer.end()};
}
// Caller holds m_bufMutex. A slow/disconnected consumer cannot grow memory forever.
void PtySession::EnqueueFrame(PtyMsgType type, const void* data, size_t len) {
    if (m_queuedBytes + len > pipeio::MaxFrame) { m_hasClient = false; return; }
    PtyFrameHeader hdr{static_cast<uint32_t>(type), static_cast<uint32_t>(len)};
    std::vector<uint8_t> frame(sizeof(hdr) + len);
    memcpy(frame.data(), &hdr, sizeof(hdr));
    if (len) memcpy(frame.data() + sizeof(hdr), data, len);
    m_queuedBytes += frame.size();
    m_sendQueue.push_back(std::move(frame));
}
void PtySession::ServerPipeLoop() {
    pipeio::Security security;
    if (!security.descriptor) return;
    while (!m_shouldTerminate) {
        HANDLE pipe = CreateNamedPipeW(PipeName().c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, &security.attrs);
        if (pipe == INVALID_HANDLE_VALUE) break;
        if (!pipeio::Connect(pipe, m_stopEvent)) { CloseHandle(pipe); break; }
        // Exactly one handshake and replay. Output during attach queues after history.
        PtyFrameHeader hdr{};
        bool attached = pipeio::Transfer(pipe, &hdr, sizeof(hdr), false, m_stopEvent) &&
            hdr.type == static_cast<uint32_t>(PtyMsgType::Attach) && hdr.length == 0;
        if (attached) {
            std::lock_guard<std::mutex> lock(m_bufMutex);
            m_sendQueue.clear(); m_queuedBytes = 0;
            m_hasClient = true;
            std::string history(m_historyBuffer.begin(), m_historyBuffer.end());
            if (!history.empty()) EnqueueFrame(PtyMsgType::Output, history.data(), history.size());
            if (!IsAlive()) { DWORD code = ExitCode(); EnqueueFrame(PtyMsgType::Exit, &code, sizeof(code)); }
        }
        while (attached && m_hasClient && !m_shouldTerminate) {
            std::vector<uint8_t> frame;
            {
                std::lock_guard<std::mutex> lock(m_bufMutex);
                if (!m_sendQueue.empty()) {
                    frame = std::move(m_sendQueue.front()); m_sendQueue.pop_front(); m_queuedBytes -= frame.size();
                }
            }
            if (!frame.empty() && !pipeio::Transfer(pipe, frame.data(), static_cast<DWORD>(frame.size()), true, m_stopEvent)) break;
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) break;
            if (!available) { if (frame.empty()) WaitForSingleObject(m_stopEvent, 8); continue; }
            if (!pipeio::Transfer(pipe, &hdr, sizeof(hdr), false, m_stopEvent) || hdr.length > pipeio::MaxFrame) break;
            std::vector<char> payload(hdr.length);
            if (hdr.length && !pipeio::Transfer(pipe, payload.data(), hdr.length, false, m_stopEvent)) break;
            auto type = static_cast<PtyMsgType>(hdr.type);
            if (type == PtyMsgType::Input) Write(payload.data(), payload.size());
            else if (type == PtyMsgType::Resize && payload.size() == 4) {
                short dims[2]; memcpy(dims, payload.data(), 4);
                if (dims[0] > 0 && dims[1] > 0 && dims[0] <= 1000 && dims[1] <= 1000) Resize(dims[0], dims[1]);
            } else if (type == PtyMsgType::Detach) break;
            else if (type == PtyMsgType::Kill) { m_shouldTerminate = true; break; }
        }
        {
            std::lock_guard<std::mutex> lock(m_bufMutex);
            m_hasClient = false; m_sendQueue.clear(); m_queuedBytes = 0;
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    if (m_shouldTerminate) {
        std::lock_guard<std::mutex> lock(m_ptyMutex);
        m_pty.Close(); PublishExit(m_pty.ExitCode());
    }
}
} // namespace ft
