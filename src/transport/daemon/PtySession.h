#pragma once
//
// FullTerminal — ConPTY Session Daemon (ftagent altyapisi)
// ConPTY sureclerini GUI penceresinden soyutlayan, oturumlari arka planda
// yasatan, named pipe uzerinden attach/detach destekleyen oturum yoneticisi.
//

#include "transport/pty/ConPty.h"
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <windows.h>

namespace ft {

enum class PtyMsgType : uint32_t {
    Output = 1,   // Daemon -> Client (PTY stdout/stderr verisi)
    Input  = 2,   // Client -> Daemon (klavye girdisi / komut)
    Resize = 3,   // Client -> Daemon (short cols, short rows)
    Exit   = 4,   // Daemon -> Client (islem cikis kodu)
    Attach = 5,   // Client -> Daemon (baglanma ve gecmis tekrari istegi)
    Detach = 6,   // Client -> Daemon (baglantiyi kes, sureci arkada yasat)
    Kill   = 7    // Client -> Daemon (oturumu ve surec agacini sonlandir)
};

#pragma pack(push, 1)
struct PtyFrameHeader {
    uint32_t type;    // PtyMsgType
    uint32_t length;  // Takip eden veri boyutu (bayt)
};
#pragma pack(pop)

class PtySession {
public:
    explicit PtySession(const std::string& sessionId);
    ~PtySession();

    const std::string& Id() const { return m_id; }
    std::wstring PipeName() const;

    // ConPTY surecini ve Named Pipe sunucusunu baslatir
    bool Start(const std::wstring& commandLine,
               const std::wstring& startDir,
               const std::vector<std::pair<std::wstring, std::wstring>>& envOverrides,
               short cols, short rows,
               std::wstring* errorOut = nullptr);

    void Write(const char* data, size_t len);
    void Resize(short cols, short rows);
    void Terminate();

    bool IsAlive() const;
    bool HasClient() const { return m_hasClient.load(); }
    DWORD ExitCode() const { return m_pty.ExitCode(); }

    // Yeniden baglanan istemciler icin gecmis tamponu
    std::string GetHistoryReplay() const;

private:
    void ServerPipeLoop();
    void SenderLoop();
    void EnqueueFrame(PtyMsgType type, const void* data, size_t len);

    std::string m_id;
    ConPty m_pty;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_hasClient{ false };
    std::atomic<bool> m_shouldTerminate{ false };

    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_stopEvent = nullptr;
    std::thread m_pipeThread;
    std::thread m_senderThread;

    mutable std::mutex m_bufMutex;
    static constexpr size_t kMaxHistoryBytes = 256 * 1024; // 256 KB replay tamponu
    std::deque<char> m_historyBuffer;

    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<std::vector<uint8_t>> m_sendQueue;
};

} // namespace ft
