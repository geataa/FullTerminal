#include "transport/daemon/PtySessionClient.h"
#include "transport/daemon/SessionDaemon.h"
#include "transport/daemon/PipeIo.h"
#include "mcp/Json.h"
#include "core/Utf8.h"
#include <objbase.h>
#include <cstring>

namespace ft {
PtySessionClient::PtySessionClient() { m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
PtySessionClient::~PtySessionClient() { Detach(); if (m_stopEvent) CloseHandle(m_stopEvent); }
bool PtySessionClient::Start(const std::wstring& command, const std::wstring& cwd,
    const std::vector<std::pair<std::wstring, std::wstring>>& env, short cols, short rows, std::wstring* err) {
    Kill();
    GUID guid{}; if (FAILED(CoCreateGuid(&guid))) return false;
    wchar_t id[40]{}; StringFromGUID2(guid, id, 40);
    std::wstring clean(id + 1, id + 37);
    std::string sid = WideToUtf8(clean);
    json::Value req = json::Value::Object(), resp;
    req["action"] = "create"; req["id"] = sid;
    req["command"] = WideToUtf8(command); req["cwd"] = WideToUtf8(cwd);
    req["cols"] = static_cast<int>(cols); req["rows"] = static_cast<int>(rows);
    auto vars = json::Value::Object();
    for (const auto& [key, value] : env) vars[WideToUtf8(key)] = WideToUtf8(value);
    req["env"] = vars;
    if (!SessionDaemon::Request(req, resp, true) || !resp["success"].as_bool()) {
        if (err) *err = Utf8ToWide(resp["error"].as_string("Could not start terminal daemon."));
        return false;
    }
    if (Attach(sid, err)) return true;
    req = json::Value::Object(); req["action"] = "kill"; req["id"] = sid;
    SessionDaemon::Request(req, resp);
    return false;
}
bool PtySessionClient::Attach(const std::string& id, std::wstring* err) {
    Detach();
    if (!pipeio::ValidId(id)) return false;
    const auto name = pipeio::SessionName(id);
    for (int i = 0; i < 60; ++i) {
        m_pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (m_pipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() == ERROR_ACCESS_DENIED) break;
        WaitNamedPipeW(name.c_str(), 50); Sleep(20);
    }
    if (m_pipe == INVALID_HANDLE_VALUE) { if (err) *err = L"Terminal session unavailable or already attached."; return false; }
    m_sessionId = id; m_exited = false; m_exitCode = 0; m_connected = true;
    ResetEvent(m_stopEvent);
    if (!SendFrame(PtyMsgType::Attach, nullptr, 0)) { Detach(); return false; }
    m_readerThread = std::thread(&PtySessionClient::ReaderLoop, this);
    return true;
}
void PtySessionClient::Write(const char* data, size_t len) { if (Running() && len) SendFrame(PtyMsgType::Input, data, len); }
void PtySessionClient::Resize(short cols, short rows) {
    if (Running()) { short dims[] = {cols, rows}; SendFrame(PtyMsgType::Resize, dims, sizeof(dims)); }
}
void PtySessionClient::Detach() {
    if (m_connected) SendFrame(PtyMsgType::Detach, nullptr, 0);
    m_connected = false;
    if (m_stopEvent) SetEvent(m_stopEvent);
    // Reader owns its OVERLAPPED storage until cancellation has completed.
    if (m_readerThread.joinable()) m_readerThread.join();
    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (m_pipe != INVALID_HANDLE_VALUE) { CloseHandle(m_pipe); m_pipe = INVALID_HANDLE_VALUE; }
    m_sessionId.clear();
}
void PtySessionClient::Kill() {
    const auto id = m_sessionId;
    if (m_connected) SendFrame(PtyMsgType::Kill, nullptr, 0);
    Detach();
    // Also clean up when a broken output pipe prevented the Kill frame reaching the daemon.
    if (!id.empty()) {
        auto request = json::Value::Object(); json::Value response;
        request["action"] = "kill"; request["id"] = id;
        SessionDaemon::Request(request, response);
    }
}
bool PtySessionClient::SendFrame(PtyMsgType type, const void* data, size_t len) {
    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (m_pipe == INVALID_HANDLE_VALUE || len > pipeio::MaxFrame) return false;
    PtyFrameHeader hdr{static_cast<uint32_t>(type), static_cast<uint32_t>(len)};
    std::vector<char> frame(sizeof(hdr) + len);
    memcpy(frame.data(), &hdr, sizeof(hdr));
    if (len) memcpy(frame.data() + sizeof(hdr), data, len);
    return pipeio::Transfer(m_pipe, frame.data(), static_cast<DWORD>(frame.size()), true, m_stopEvent);
}
void PtySessionClient::ReaderLoop() {
    while (m_connected) {
        PtyFrameHeader hdr{};
        if (!pipeio::Transfer(m_pipe, &hdr, sizeof(hdr), false, m_stopEvent, INFINITE) || hdr.length > pipeio::MaxFrame) break;
        std::vector<char> payload(hdr.length);
        if (hdr.length && !pipeio::Transfer(m_pipe, payload.data(), hdr.length, false, m_stopEvent)) break;
        if (hdr.type == static_cast<uint32_t>(PtyMsgType::Output)) {
            if (onOutput && !payload.empty()) onOutput(payload.data(), payload.size());
        } else if (hdr.type == static_cast<uint32_t>(PtyMsgType::Exit) && payload.size() == sizeof(DWORD)) {
            DWORD code; memcpy(&code, payload.data(), sizeof(code));
            m_exitCode = code; m_exited = true;
            if (onExit) onExit(code);
            break;
        }
    }
    m_connected = false;
}
} // namespace ft
