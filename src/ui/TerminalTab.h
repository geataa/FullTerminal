#pragma once
//
// Bir oturum: ConPTY + ekran modeli + ayristirici.
// Okuyucu is parcacigi ham baytlari kuyruga birakir, ana is parcacigi
// Pump() ile ayristiriciya verir. VT durumu tek is parcaciginda kalir.
//
#include "core/ShellProfiles.h"
#include "transport/pty/ConPty.h"
#include "vt/Screen.h"
#include "vt/VtParser.h"

#include "model/Inventory.h"
#include "model/SftpModel.h"
#include "services/AgentDetector.h"
#include "services/SessionRecorder.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace ft {

enum class SshStage {
    None,        // Yerel kabuk (SSH degil)
    Connecting,  // SSH baglantisi kuruluyor / kimlik dogrulaniyor (Loading + loglar)
    Connected,   // SSH baglantisi basarili, dogrudan uzak terminal acik
    Failed       // SSH baglantisi basarisiz oldu (Gorsel 3 hata ekrani)
};

struct SshLogStep {
    std::wstring icon; // e.g. L"🔮", L"❗", L"⚙️", L"🚀", L"🔒", L"🔑", L"😨"
    std::wstring text;
    uint32_t color = 0;
};

class TerminalTab {
public:
    TerminalTab() = default;
    ~TerminalTab();
    TerminalTab(const TerminalTab&) = delete;
    TerminalTab& operator=(const TerminalTab&) = delete;

    bool Start(const ShellProfile& profile, int cols, int rows,
               HWND notifyHwnd, UINT notifyMsg, std::wstring* err);
    void Close();

    // Ana is parcaciginda cagrilir. Yeni veri islendiyse true doner.
    bool Pump();

    void Write(const char* data, size_t len);
    void Write(const std::string& s) { Write(s.data(), s.size()); }
    void Resize(int cols, int rows);

    Screen&       screen()       { return m_screen; }
    const Screen& screen() const { return m_screen; }
    const ShellProfile& profile() const { return m_profile; }
    int  CursorStyle() const { return m_parser.CursorStyle(); }

    // Biten oturumda conhost (24H2 oncesi) okuyucuyu acik tutabilir; canli sayilmaz.
    bool Alive() const { return !m_exited && !m_dead && m_pty.Running(); }
    // Kabuk 0 koduyla bitti: MainWindow sekmeyi hemen kapatir.
    bool Exited() const { return m_exited; }
    // Kabuk sifir disi kodla bitti (orn. ssh baglanamadi): hata metni okunabilsin
    // diye sekme acik kalir, ekrana soluk bir "[islem N koduyla sonlandi ...]"
    // satiri basilir; MainWindow Enter/Esc/Ctrl+D ile kapatir.
    bool Dead() const { return m_dead; }
    DWORD ExitCode() const { return m_pty.ExitCode(); }

    std::wstring Title() const;
    void SetCustomTitle(const std::wstring& title) { m_customTitle = title; }
    const std::wstring& CustomTitle() const { return m_customTitle; }

    uint64_t TotalBytes() const { return m_totalBytes; }
    double   ThroughputMBs() const { return m_mbps; }
    bool     BellPending();

    // SSH oturum yonetimi
    void SetSshSession(const Host& h);
    SshStage GetSshStage() const { return m_sshStage; }
    void SetSshStage(SshStage s) { m_sshStage = s; }
    const Host& GetSshHost() const { return m_sshHost; }
    const std::vector<SshLogStep>& GetSshLogs() const { return m_sshLogs; }
    std::wstring GetFormattedSshLogs() const;
    std::wstring GetCurrentSshStep() const;
    void AddSshLog(const std::wstring& icon, const std::wstring& text, uint32_t color = 0);
    bool RestartSsh(std::wstring* err = nullptr);

    // ssh.exe parola sordugunda bir kez gonderilir (FR-PROD-013 otomatik yanit).
    // Yalnizca cikti, hedefin kendi OpenSSH istemiyle bittiginde yanitlanir:
    // "<user>@<host>'s password:" ya da "(<user>@<host>) Password:". Baska bir
    // host (jump host) adini tasiyan isteme asla gitmez. 60 sn, ilk yanit ya da
    // kabuk istemi gorulunce parola bellekten silinir. host = ssh'a verilen adres,
    // user bos olabilir (o zaman host'un herhangi bir kullanicisi kabul edilir).
    void SetAutoPassword(const std::wstring& pw, const std::wstring& user,
                         const std::wstring& host);
    void SetScrollbackLimit(int lines) { (void)lines; }

    // Ajan durum tespiti (Herdr ilhami Agent State Engine)
    const AgentDetectionResult& GetAgentStatus() const { return m_agentStatus; }
    const AgentDetectionResult& AgentStatus() const { return m_agentStatus; }
    void UpdateAgentStatus();

    // Oturum Kaydi (Enterprise Session Recording & Compliance)
    bool StartRecording(const std::wstring& filePath = L"");
    bool StopRecording(std::wstring* savedPath = nullptr);
    bool IsRecording() const;
    SessionRecorder* Recorder() { return m_recorder.get(); }

    // SFTP Tab Ozelligi (Coklu SFTP Sekmeleri)
    bool IsSftp() const { return m_isSftp; }
    SftpController* Sftp() const { return m_sftp.get(); }
    void StartSftp(const Host* host, const Inventory& inv);

    // SSH Welcome Banner & MOTD Renk Koruma / Yeniden Uygulama
    void ReapplySshWelcome();
    void ClearBeforeThemeApply();
    void ApplyTheme();
    std::string BuildSshBanner() const;

    friend class SshLoadingTest;

private:
    void AutoPasswordStep(bool gotOutput);
    void WipeAutoPassword();
    void FinishSession(bool haveExitCode);
    void ProcessSshOutput(const std::string& chunk);
    void ProcessSshPostConnect(const std::string& chunk);
    void InjectSshWelcomeBanner();

    ShellProfile m_profile;
    ConPty       m_pty;
    Screen       m_screen;
    VtParser     m_parser{ m_screen };

    std::mutex        m_mtx;
    std::string       m_queue;
    std::atomic<bool> m_posted{ false };
    std::atomic<bool> m_bell{ false };
    bool              m_exited = false;
    bool              m_dead = false;
    int64_t           m_endSeenAt = 0;  // surec/okuyucu bitisinin ilk goruldugu an

    HWND m_notifyHwnd = nullptr;
    UINT m_notifyMsg = 0;
    int  m_cols = 80;
    int  m_rows = 24;

    uint64_t m_totalBytes = 0;
    uint64_t m_windowBytes = 0;
    int64_t  m_windowStart = 0;
    double   m_mbps = 0.0;

    std::wstring m_autoPassword;
    std::string  m_autoUser;      // kucuk harf, UTF-8
    std::string  m_autoHost;
    int64_t      m_autoDeadline = 0;

    SshStage                m_sshStage = SshStage::None;
    Host                    m_sshHost;
    std::vector<SshLogStep> m_sshLogs;
    std::string             m_sshLineBuf;
    bool                    m_seenAuthMethods = false;
    bool                    m_sshBannerInjected = false;
    bool                    m_sshMotdDone = false;
    std::string             m_cachedSshBanner;
    std::string             m_cachedSshMotd;
    std::string             m_cachedSshPrompt;
    bool                    m_userHasRunCommand = false;
    bool                    m_sshInResizeRepaint = false;
    int                     m_sshResizeChunks = 0;
    std::wstring            m_serverCipher;
    std::wstring            m_serverMac;
    std::wstring            m_serverComp;

    AgentDetectionResult    m_agentStatus;
    uint64_t                m_lastAgentCheckRev = 0;
    bool                    m_wasBlocked = false;

    std::unique_ptr<SessionRecorder> m_recorder;
    std::wstring                     m_customTitle;

    bool                             m_isSftp = false;
    std::unique_ptr<SftpController>  m_sftp;
};

} // namespace ft
