#pragma once
//
// FullTerminal — SessionDaemon (ftagent)
// Arka planda yasayan ConPTY oturumlarinin merkezi yoneticisi.
//

#include "transport/daemon/PtySession.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

namespace ft {

struct SessionInfo {
    std::string id;
    bool isAlive = false;
    bool hasClient = false;
    DWORD exitCode = 0;
};

class SessionDaemon {
public:
    static SessionDaemon& Instance();

    // Yeni bir oturum olusturur ve baslatir
    std::shared_ptr<PtySession> CreateSession(
        const std::string& sessionId,
        const std::wstring& commandLine,
        const std::wstring& startDir,
        const std::vector<std::pair<std::wstring, std::wstring>>& envOverrides,
        short cols, short rows,
        std::wstring* errorOut = nullptr);

    std::shared_ptr<PtySession> GetSession(const std::string& sessionId);
    bool KillSession(const std::string& sessionId);
    std::vector<SessionInfo> ListSessions();
    void PruneDead();

    // Arka plan daemon servisi dongusu (FullTerminal --daemon / --ftagent)
    static int RunStandalone();

    void StartDaemonPipe();
    void StopDaemonPipe();

private:
    SessionDaemon() = default;
    ~SessionDaemon();

    void DaemonPipeLoop();

    std::mutex m_mutex;
    std::map<std::string, std::shared_ptr<PtySession>> m_sessions;
    std::atomic<bool> m_pipeRunning{ false };
    std::thread m_pipeThread;
    HANDLE m_stopEvent = nullptr;
};

} // namespace ft
