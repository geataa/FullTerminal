#include "transport/daemon/SessionDaemon.h"
#include "mcp/Json.h"
#include "core/Utf8.h"
#include <iostream>

namespace ft {

SessionDaemon& SessionDaemon::Instance() {
    static SessionDaemon instance;
    return instance;
}

SessionDaemon::~SessionDaemon() {
    StopDaemonPipe();
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, s] : m_sessions) {
        if (s) s->Terminate();
    }
    m_sessions.clear();
}

std::shared_ptr<PtySession> SessionDaemon::CreateSession(
    const std::string& sessionId,
    const std::wstring& commandLine,
    const std::wstring& startDir,
    const std::vector<std::pair<std::wstring, std::wstring>>& envOverrides,
    short cols, short rows,
    std::wstring* errorOut) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(sessionId);
    if (it != m_sessions.end()) {
        if (it->second->IsAlive()) {
            if (errorOut) *errorOut = L"Session already exists and is running: " + Utf8ToWide(sessionId);
            return nullptr;
        }
        it->second->Terminate();
        m_sessions.erase(it);
    }

    auto session = std::make_shared<PtySession>(sessionId);
    if (!session->Start(commandLine, startDir, envOverrides, cols, rows, errorOut)) {
        return nullptr;
    }

    m_sessions[sessionId] = session;
    return session;
}

std::shared_ptr<PtySession> SessionDaemon::GetSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(sessionId);
    if (it != m_sessions.end()) {
        return it->second;
    }
    return nullptr;
}

bool SessionDaemon::KillSession(const std::string& sessionId) {
    std::shared_ptr<PtySession> target;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(sessionId);
        if (it == m_sessions.end()) return false;
        target = it->second;
        m_sessions.erase(it);
    }
    if (target) {
        target->Terminate();
    }
    return true;
}

std::vector<SessionInfo> SessionDaemon::ListSessions() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SessionInfo> list;
    list.reserve(m_sessions.size());
    for (const auto& [id, s] : m_sessions) {
        if (s) {
            list.push_back({ id, s->IsAlive(), s->HasClient(), s->ExitCode() });
        }
    }
    return list;
}

void SessionDaemon::PruneDead() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (it->second && !it->second->IsAlive() && !it->second->HasClient()) {
            it->second->Terminate();
            it = m_sessions.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionDaemon::StartDaemonPipe() {
    if (m_pipeRunning.exchange(true)) return;
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_pipeThread = std::thread(&SessionDaemon::DaemonPipeLoop, this);
}

void SessionDaemon::StopDaemonPipe() {
    if (!m_pipeRunning.exchange(false)) return;
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_pipeThread.joinable()) {
        m_pipeThread.join();
    }
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

void SessionDaemon::DaemonPipeLoop() {
    const std::wstring pipeName = L"\\\\.\\pipe\\FullTerminal.Daemon";

    while (m_pipeRunning) {
        HANDLE hPipe = CreateNamedPipeW(
            pipeName.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            16384,
            16384,
            0,
            nullptr
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }

        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(hPipe, &ov);
        if (!connected) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waitHandles[2] = { ov.hEvent, m_stopEvent };
                DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (waitRes == WAIT_OBJECT_0 + 1 || !m_pipeRunning) {
                    CancelIo(hPipe);
                    CloseHandle(ov.hEvent);
                    CloseHandle(hPipe);
                    break;
                }
                connected = TRUE;
            } else if (err == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
        }
        CloseHandle(ov.hEvent);

        if (!connected) {
            CloseHandle(hPipe);
            continue;
        }

        // 4-bayt uzunluk ve JSON istegi oku
        uint32_t reqLen = 0;
        DWORD bytesRead = 0;
        if (ReadFile(hPipe, &reqLen, sizeof(uint32_t), &bytesRead, nullptr) && bytesRead == sizeof(uint32_t) && reqLen < 65536) {
            std::string reqStr(reqLen, '\0');
            DWORD payloadRead = 0;
            ReadFile(hPipe, reqStr.data(), reqLen, &payloadRead, nullptr);

            bool ok = false;
            json::Value req = json::Value::parse(reqStr, &ok);
            json::Value resp = json::Value::Object();

            if (ok && req.is_object()) {
                const std::string action = req["action"].as_string();
                if (action == "list") {
                    resp["success"] = true;
                    json::Value arr = json::Value::Array();
                    for (const auto& s : ListSessions()) {
                        json::Value item = json::Value::Object();
                        item["id"] = s.id;
                        item["alive"] = s.isAlive;
                        item["has_client"] = s.hasClient;
                        item["exit_code"] = static_cast<int64_t>(s.exitCode);
                        arr.push_back(item);
                    }
                    resp["sessions"] = arr;
                } else if (action == "kill") {
                    const std::string id = req["id"].as_string();
                    resp["success"] = KillSession(id);
                } else if (action == "prune") {
                    PruneDead();
                    resp["success"] = true;
                } else {
                    resp["success"] = false;
                    resp["error"] = "Unknown action: " + action;
                }
            } else {
                resp["success"] = false;
                resp["error"] = "Invalid JSON request";
            }

            const std::string respStr = resp.dump();
            const uint32_t respLen = static_cast<uint32_t>(respStr.size());
            DWORD written = 0;
            WriteFile(hPipe, &respLen, sizeof(uint32_t), &written, nullptr);
            WriteFile(hPipe, respStr.data(), respLen, &written, nullptr);
        }

        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
}

int SessionDaemon::RunStandalone() {
    std::cout << "FullTerminal ConPTY Session Daemon (ftagent) starting...\n";
    Instance().StartDaemonPipe();
    std::cout << "Daemon pipe listening on \\\\.\\pipe\\FullTerminal.Daemon\n";
    std::cout << "Press Ctrl+C or send stop action to terminate daemon.\n";

    // Standalone bekleme dongusu
    HANDLE hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    WaitForSingleObject(hEvent, INFINITE);
    CloseHandle(hEvent);

    Instance().StopDaemonPipe();
    return 0;
}

} // namespace ft
