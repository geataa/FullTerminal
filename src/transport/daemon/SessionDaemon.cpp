#include "transport/daemon/SessionDaemon.h"
#include "mcp/Json.h"
#include "core/Utf8.h"
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <signal.h>
#endif

namespace ft {

namespace {
std::string GetLinuxDaemonSocket() {
    const char* user = getenv("USER");
    if (!user) user = "default";
    std::string dir = "/tmp/ft-" + std::string(user);
#if !defined(_WIN32)
    mkdir(dir.c_str(), 0700);
#endif
    return dir + "/session-daemon.sock";
}
}

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
        if (it != m_sessions.end()) {
            target = it->second;
            m_sessions.erase(it);
        }
    }
    if (target) {
        target->Terminate();
        return true;
    }
    return false;
}

std::vector<SessionInfo> SessionDaemon::ListSessions() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SessionInfo> list;
    for (const auto& [id, s] : m_sessions) {
        SessionInfo info;
        info.id = id;
        info.isAlive = s ? s->IsAlive() : false;
        info.hasClient = s ? s->HasClient() : false;
        info.exitCode = s ? s->ExitCode() : 0;
        list.push_back(info);
    }
    return list;
}

void SessionDaemon::PruneDead() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ) {
        if (!it->second || (!it->second->IsAlive() && !it->second->HasClient())) {
            if (it->second) it->second->Terminate();
            it = m_sessions.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionDaemon::StartDaemonPipe() {
    if (m_pipeRunning.exchange(true)) return;
#if defined(_WIN32)
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#endif
    m_pipeThread = std::thread(&SessionDaemon::DaemonPipeLoop, this);
}

void SessionDaemon::StopDaemonPipe() {
    if (!m_pipeRunning.exchange(false)) return;
#if defined(_WIN32)
    if (m_stopEvent) SetEvent(m_stopEvent);
#endif
    if (m_pipeThread.joinable()) {
        m_pipeThread.join();
    }
#if defined(_WIN32)
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
#endif
}

void SessionDaemon::DaemonPipeLoop() {
#if defined(_WIN32)
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
#else
    std::string sockPath = GetLinuxDaemonSocket();
    unlink(sockPath.c_str());

    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) return;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(serverFd);
        return;
    }
    listen(serverFd, 5);

    while (m_pipeRunning) {
        struct pollfd pfd{};
        pfd.fd = serverFd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 100);
        if (ret <= 0) continue;

        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) continue;

        uint32_t reqLen = 0;
        if (read(clientFd, &reqLen, sizeof(uint32_t)) == sizeof(uint32_t) && reqLen < 65536) {
            std::string reqStr(reqLen, '\0');
            size_t got = 0;
            while (got < reqLen) {
                ssize_t n = read(clientFd, reqStr.data() + got, reqLen - got);
                if (n <= 0) break;
                got += n;
            }

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
            write(clientFd, &respLen, sizeof(uint32_t));
            write(clientFd, respStr.data(), respLen);
        }
        close(clientFd);
    }

    close(serverFd);
    unlink(sockPath.c_str());
#endif
}

int SessionDaemon::RunStandalone() {
    std::cout << "FullTerminal Session Daemon (ftagent) starting...\n";
    Instance().StartDaemonPipe();
    std::cout << "Daemon socket listening.\n";
    std::cout << "Press Ctrl+C to terminate daemon.\n";

#if defined(_WIN32)
    HANDLE hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    WaitForSingleObject(hEvent, INFINITE);
    CloseHandle(hEvent);
#else
    while (true) {
        sleep(10);
    }
#endif

    Instance().StopDaemonPipe();
    return 0;
}

} // namespace ft
