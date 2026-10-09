#pragma once
//
// FullTerminal — PtySessionClient
// Named Pipe uzerinden calisan bir PtySession daemon oturumuna attach/detach
// olan, cikti okuyan ve girdi yazan istemci sarmalayici.
//

#include "transport/daemon/PtySession.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <windows.h>

namespace ft {

class PtySessionClient {
public:
    PtySessionClient();
    ~PtySessionClient();

    using OutputFn = std::function<void(const char*, size_t)>;
    using ExitFn   = std::function<void(DWORD)>;

    OutputFn onOutput;
    ExitFn   onExit;

    // Belirtilen oturuma named pipe ile baglanir (Attach)
    bool Attach(const std::string& sessionId, std::wstring* errorOut = nullptr);
    bool Start(const std::wstring& command, const std::wstring& cwd,
        const std::vector<std::pair<std::wstring, std::wstring>>& env, short cols, short rows,
        std::wstring* errorOut = nullptr);
    void Close() { Kill(); }
    bool Running() const { return m_connected && !m_exited; }
    bool ProcessExited() const { return m_exited; }
    DWORD ExitCode() const { return m_exitCode; }

    // Girdi ve boyutlandirma mesajlari
    void Write(const char* data, size_t len);
    void Resize(short cols, short rows);

    // Oturumu arkada yasatarak ayril (Detach)
    void Detach();

    // Oturumu ve surec agacini sonlandirarak cik (Kill)
    void Kill();

    bool IsConnected() const { return m_connected.load(); }
    const std::string& SessionId() const { return m_sessionId; }

private:
    void ReaderLoop();
    bool SendFrame(PtyMsgType type, const void* data, size_t len);

    std::string m_sessionId;
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_stopEvent = nullptr;
    std::thread m_readerThread;
    std::atomic<bool> m_connected{ false };
    std::atomic<bool> m_exited{ false };
    std::atomic<DWORD> m_exitCode{ 0 };
    std::mutex m_writeMutex;
};

} // namespace ft
