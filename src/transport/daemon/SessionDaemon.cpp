#include "transport/daemon/SessionDaemon.h"
#include "transport/daemon/PipeIo.h"
#include "mcp/Json.h"
#include "core/Utf8.h"

namespace ft {
SessionDaemon& SessionDaemon::Instance() { static SessionDaemon instance; return instance; }
SessionDaemon::~SessionDaemon() {
    StopDaemonPipe();
    for (auto& [id, s] : m_sessions) if (s) s->Terminate();
}
std::shared_ptr<PtySession> SessionDaemon::CreateSession(const std::string& id,
    const std::wstring& command, const std::wstring& cwd,
    const std::vector<std::pair<std::wstring, std::wstring>>& env,
    short cols, short rows, std::wstring* err) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!pipeio::ValidId(id) || command.empty() || cols < 1 || rows < 1 || cols > 1000 || rows > 1000 ||
        m_sessions.size() >= 128 || m_sessions.count(id)) {
        if (err) *err = L"Invalid or duplicate terminal session.";
        return nullptr;
    }
    auto session = std::make_shared<PtySession>(id);
    if (!session->Start(command, cwd, env, cols, rows, err)) return nullptr;
    m_sessions[id] = session;
    return session;
}
std::shared_ptr<PtySession> SessionDaemon::GetSession(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(id);
    return it == m_sessions.end() ? nullptr : it->second;
}
bool SessionDaemon::KillSession(const std::string& id) {
    std::shared_ptr<PtySession> session;
    { std::lock_guard<std::mutex> lock(m_mutex);
      auto it = m_sessions.find(id); if (it == m_sessions.end()) return false;
      session = it->second; m_sessions.erase(it); }
    session->Terminate(); return true;
}
std::vector<SessionInfo> SessionDaemon::ListSessions() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SessionInfo> result;
    for (auto& [id, s] : m_sessions) result.push_back({id, s->IsAlive(), s->HasClient(), s->ExitCode()});
    return result;
}
void SessionDaemon::PruneDead() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        it->second->PollExit();
        if (it->second->CanPrune()) it = m_sessions.erase(it);
        else ++it;
    }
}
void SessionDaemon::StartDaemonPipe() {
    if (m_pipeRunning.exchange(true)) return;
    m_shutdown = false;
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_pipeThread = std::thread(&SessionDaemon::DaemonPipeLoop, this);
}
void SessionDaemon::StopDaemonPipe() {
    m_pipeRunning = false;
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_pipeThread.joinable()) m_pipeThread.join();
    if (m_stopEvent) { CloseHandle(m_stopEvent); m_stopEvent = nullptr; }
}
void SessionDaemon::DaemonPipeLoop() {
    pipeio::Security security;
    if (!security.descriptor) { m_shutdown = true; return; }
    while (m_pipeRunning) {
        HANDLE pipe = CreateNamedPipeW(pipeio::DaemonName().c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 65536, 65536, 0, &security.attrs);
        if (pipe == INVALID_HANDLE_VALUE) { m_shutdown = true; break; }
        if (!pipeio::Connect(pipe, m_stopEvent)) { CloseHandle(pipe); break; }
        uint32_t size = 0;
        if (pipeio::Transfer(pipe, &size, sizeof(size), false, m_stopEvent) && size > 0 && size <= pipeio::MaxFrame) {
            std::string data(size, '\0');
            if (pipeio::Transfer(pipe, data.data(), size, false, m_stopEvent)) {
                bool valid = false;
                auto req = json::Value::parse(data, &valid);
                auto resp = json::Value::Object();
                resp["success"] = false;
                const auto action = req["action"].as_string();
                if (!valid || !req.is_object()) resp["error"] = "Invalid JSON.";
                else if (action == "ping") resp["success"] = true;
                else if (action == "create") {
                    const int64_t cols = req["cols"].as_int(), rows = req["rows"].as_int();
                    if (cols < 1 || cols > 1000 || rows < 1 || rows > 1000) resp["error"] = "Invalid terminal dimensions.";
                    else {
                        std::vector<std::pair<std::wstring, std::wstring>> env;
                        for (const auto& [k, v] : req["env"].objVal)
                            if (v.is_string() && k.find('=') == std::string::npos)
                                env.emplace_back(Utf8ToWide(k), Utf8ToWide(v.as_string()));
                        std::wstring err;
                        auto session = CreateSession(req["id"].as_string(), Utf8ToWide(req["command"].as_string()),
                            Utf8ToWide(req["cwd"].as_string()), env, static_cast<short>(cols), static_cast<short>(rows), &err);
                        resp["success"] = session != nullptr;
                        if (!session) resp["error"] = WideToUtf8(err);
                    }
                } else if (action == "describe") {
                    if (auto session = GetSession(req["id"].as_string())) {
                        resp["success"] = true;
                        resp["launch"] = session->LaunchInfo();
                    }
                } else if (action == "list") {
                    resp["success"] = true;
                    auto arr = json::Value::Array();
                    for (auto s : ListSessions()) {
                        auto item = json::Value::Object();
                        item["id"] = s.id; item["alive"] = s.isAlive; item["has_client"] = s.hasClient;
                        item["exit_code"] = static_cast<int64_t>(s.exitCode); arr.push_back(item);
                    }
                    resp["sessions"] = arr;
                } else if (action == "kill") resp["success"] = KillSession(req["id"].as_string());
                else if (action == "stop" && ListSessions().empty()) { resp["success"] = true; m_shutdown = true; }
                else resp["error"] = "Unknown action or active sessions.";
                data = resp.dump(); size = static_cast<uint32_t>(data.size());
                pipeio::Transfer(pipe, &size, sizeof(size), true, m_stopEvent);
                pipeio::Transfer(pipe, data.data(), size, true, m_stopEvent);
                // Wait for client to read the response without an unbounded FlushFileBuffers.
                for (int i = 0; i < 100 && m_pipeRunning; ++i) {
                    DWORD avail = 0;
                    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) break;
                    Sleep(2);
                }
            }
        }
        DisconnectNamedPipe(pipe); CloseHandle(pipe);
    }
}
bool SessionDaemon::Request(const json::Value& request, json::Value& response, bool startIfMissing) {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    bool launched = false;
    for (int i = 0; i < 100; ++i) {
        pipe = CreateFileW(pipeio::DaemonName().c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;
        DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) return false;
        if (error == ERROR_FILE_NOT_FOUND && !launched) {
            if (!startIfMissing) return false;
            wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
            std::wstring command = L"\"" + std::wstring(exe) + L"\" --daemon";
            STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                nullptr, nullptr, &si, &pi)) return false;
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess); launched = true;
        }
        WaitNamedPipeW(pipeio::DaemonName().c_str(), 50); Sleep(20);
    }
    if (pipe == INVALID_HANDLE_VALUE) return false;
    std::string data = request.dump();
    uint32_t size = static_cast<uint32_t>(data.size());
    bool ok = size <= pipeio::MaxFrame && pipeio::Transfer(pipe, &size, sizeof(size), true, nullptr) &&
        pipeio::Transfer(pipe, data.data(), size, true, nullptr) &&
        pipeio::Transfer(pipe, &size, sizeof(size), false, nullptr) && size <= pipeio::MaxFrame;
    if (ok) {
        data.resize(size);
        ok = pipeio::Transfer(pipe, data.data(), size, false, nullptr);
        if (ok) response = json::Value::parse(data, &ok);
    }
    CloseHandle(pipe);
    return ok;
}
int SessionDaemon::RunStandalone() {
    std::wstring name = L"Local\\FullTerminal.Daemon." + pipeio::Scope();
    HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) { if (mutex) CloseHandle(mutex); return 0; }
    auto& daemon = Instance(); daemon.StartDaemonPipe();
    ULONGLONG emptySince = GetTickCount64();
    while (!daemon.m_shutdown) {
        Sleep(100);
        daemon.PruneDead();
        if (!daemon.ListSessions().empty()) emptySince = GetTickCount64();
        if (GetTickCount64() - emptySince > 60000) break;
    }
    daemon.StopDaemonPipe(); CloseHandle(mutex);
    return 0;
}
} // namespace ft
