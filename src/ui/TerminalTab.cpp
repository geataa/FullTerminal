#include "ui/TerminalTab.h"
#include "core/Utf8.h"
#include "core/I18n.h"
#include "transport/daemon/SessionDaemon.h"

#include <windows.h>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace ft {
namespace {

int64_t NowTicks() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}
int64_t TickFreq() {
    static int64_t f = [] { LARGE_INTEGER li; QueryPerformanceFrequency(&li); return li.QuadPart; }();
    return f;
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string TrimWs(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n\'\"");
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(" \t\r\n\'\".");
    if (e == std::string::npos || e < b) return std::string();
    return s.substr(b, e - b + 1);
}

bool EndsWith(const std::string& s, const char* suffix) {
    const size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// line "<user>@<host>" + tail ile mi bitiyor. Kullanici adinda bosluk olabilir
// (Windows hesaplari), o yuzden son kelime yerine sonek karsilastirilir.
// open == 0: kullanici satir basinda ya da bosluktan sonra; aksi halde open'dan
// sonra baslar. user bossa bos olmayan herhangi bir kullanici kabul edilir.
bool PromptFor(const std::string& line, const std::string& user, const std::string& host,
               const char* tail, char open) {
    const std::string suffix = "@" + host + tail;
    if (!EndsWith(line, suffix.c_str())) return false;
    const size_t at = line.size() - suffix.size();
    auto boundary = [&](size_t start) {
        if (open) return start > 0 && line[start - 1] == open;
        return start == 0 || line[start - 1] == ' ' || line[start - 1] == '\t';
    };
    if (!user.empty()) {
        return at >= user.size() && line.compare(at - user.size(), user.size(), user) == 0 &&
               boundary(at - user.size());
    }
    if (at == 0) return false;
    if (open) {
        const size_t o = line.rfind(open, at - 1);
        return o != std::string::npos && o + 1 < at;
    }
    return line[at - 1] != ' ' && line[at - 1] != '\t';
}

// Hedefin kendi OpenSSH parola istemi mi (line kucuk harfli, sonu kirpilmis):
//   "user@host's password:"   (password yontemi)
//   "(user@host) password:"   (keyboard-interactive, OpenSSH 8.4+)
bool TargetPrompt(const std::string& line, const std::string& user, const std::string& host) {
    if (PromptFor(line, user, host, "'s password:", 0) ||
        PromptFor(line, user, host, ") password:", '(')) return true;
    if (EndsWith(line, "password:") || EndsWith(line, "password: ") ||
        EndsWith(line, "parola:") || EndsWith(line, "parolası:")) return true;
    return false;
}

} // namespace

TerminalTab::~TerminalTab() {
    Close();
}

bool TerminalTab::Start(const ShellProfile& profile, int cols, int rows,
                        HWND notifyHwnd, UINT notifyMsg, std::wstring* err) {
    m_profile = profile;
    m_notifyHwnd = notifyHwnd;
    m_notifyMsg = notifyMsg;
    m_cols = cols;
    m_rows = rows;
    m_screen.Resize(cols, rows);
    m_screen.title = profile.name;
    m_screen.SetCursorVisible(true);
    m_windowStart = NowTicks();

    m_parser.onReply = [this](const std::string& s) { Write(s); };
    m_parser.onBell = [this] { m_bell.store(true, std::memory_order_relaxed); };

    m_pty.onOutput = [this](const char* data, size_t len) {
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_queue.append(data, len);
        }
        if (!m_posted.exchange(true, std::memory_order_acq_rel) && m_notifyHwnd) {
            PostMessageW(m_notifyHwnd, m_notifyMsg, 0, 0);
        }
    };
    m_pty.onExit = [this](DWORD) {
        if (m_notifyHwnd) PostMessageW(m_notifyHwnd, m_notifyMsg, 0, 0);
    };

    // Ortam degiskenleri: M0'da yalnizca profilden gelenler.
    // FR-K8S-010 bu listeyi manifestten dolduracak.
    auto env = profile.env;
    env.emplace_back(L"TERM", L"xterm-256color");
    env.emplace_back(L"COLORTERM", L"truecolor");
    env.emplace_back(L"TERM_PROGRAM", L"FullTerminal");

    if (!m_resumeId.empty()) {
        std::string id; id.swap(m_resumeId);
        if (!m_pty.Attach(id, err)) return false;
        m_pty.Resize((short)cols, (short)rows);
        return true;
    }
    return m_pty.Start(profile.CommandLine(), profile.startDir, env,
                       (short)cols, (short)rows, err);
}

bool TerminalTab::Resume(const ShellProfile& profile, const std::string& id, int cols, int rows,
                         HWND hwnd, UINT msg, std::wstring* err) {
    if (id.empty()) return false;
    auto request = json::Value::Object(); json::Value response;
    request["action"] = "describe"; request["id"] = id;
    if (!SessionDaemon::Request(request, response) || !response["success"].as_bool()) return false;
    // Recover launch metadata from daemon memory, never persist commands/env secrets in session.json.
    ShellProfile restored = profile;
    const auto& launch = response["launch"];
    restored.rawCommand = Utf8ToWide(launch["command"].as_string());
    restored.startDir = Utf8ToWide(launch["cwd"].as_string());
    restored.env.clear();
    for (const auto& [key, value] : launch["env"].objVal)
        restored.env.emplace_back(Utf8ToWide(key), Utf8ToWide(value.as_string()));
    m_resumeId = id;
    if (!m_sshHost.address.empty()) { m_sshStage = SshStage::Connected; }
    return Start(restored, cols, rows, hwnd, msg, err);
}

void TerminalTab::Detach() {
    if (m_recorder && m_recorder->IsRecording()) m_recorder->Stop();
    m_pty.Detach();
    m_pty.onOutput = nullptr;
    m_pty.onExit = nullptr;
    WipeAutoPassword();
}

void TerminalTab::Close() {
    m_database.reset();
    if (m_recorder && m_recorder->IsRecording()) {
        m_recorder->Stop();
    }
    if (m_isSftp && m_sftp) {
        m_sftp->DisconnectRemote();
    }
    // Once okuyucu durdurulup birlestirilir; geri cagirimlar ancak ondan sonra
    // sifirlanabilir. Tersi, okuyucu cagirirken std::function'i bosaltip
    // bad_function_call ile tum uygulamayi dusurebilir.
    m_pty.Close();
    m_pty.onOutput = nullptr;
    m_pty.onExit = nullptr;
    WipeAutoPassword();
}

void TerminalTab::SetSshSession(const Host& h) {
    m_sshHost = h;
    m_sshStage = SshStage::Connecting;
    m_showSshTerminal = false;
    m_sshLogs.clear();
    m_screen.Clear();
    m_parser.Reset();

    const std::wstring addr = h.address.empty() ? L"127.0.0.1" : h.address;
    const std::wstring port = std::to_wstring(h.port ? h.port : 22);

    AddSshLog(L"👤", L"Starting a new connection to: \"" + addr + L"\" port \"" + port + L"\"");
    AddSshLog(L"⚙️", L"Connecting to \"" + addr + L"\" port \"" + port + L"\"");
}

void TerminalTab::AddSshLog(const std::wstring& icon, const std::wstring& text, uint32_t color) {
    if (!m_sshLogs.empty() && m_sshLogs.back().text == text && m_sshLogs.back().icon == icon) return;
    m_sshLogs.push_back({ icon, text, color });
}

std::wstring TerminalTab::GetFormattedSshLogs() const {
    std::wstring out;
    for (const auto& log : m_sshLogs) {
        out += log.icon + L" " + log.text + L"\r\n";
    }
    return out;
}

std::wstring TerminalTab::GetCurrentSshStep() const {
    if (m_sshStage == SshStage::AwaitingInput)
        return TrText(L"SSH · waiting for your response", L"SSH · yanıtınız bekleniyor");
    if (m_sshLogs.empty()) return TrText(L"Connecting to server...", L"Sunucuya bağlanılıyor...");
    if (m_sshLogs.back().icon == L"…") return m_sshLogs.back().text;
    const std::wstring addr = m_sshHost.address.empty() ? L"127.0.0.1" : m_sshHost.address;
    const std::wstring user = m_sshHost.username.empty() ? TrText(L"user", L"kullanıcı") : m_sshHost.username;

    for (auto it = m_sshLogs.rbegin(); it != m_sshLogs.rend(); ++it) {
        const std::wstring& t = it->text;
        if (t.find(L"Authenticating to") != std::wstring::npos ||
            t.find(L"Authenticating using") != std::wstring::npos) {
            return std::wstring(TrText(L"Authenticating (", L"Kimlik doğrulanıyor (")) + user + L")...";
        }
        if (t.find(L"Agreed KEX") != std::wstring::npos ||
            t.find(L"Handshake") != std::wstring::npos ||
            t.find(L"Agreed client-to-server") != std::wstring::npos ||
            t.find(L"Checking host key") != std::wstring::npos) {
            return TrText(L"Performing secure handshake (KEX / Host Key)...", L"Güvenli şifreleme el sıkışması yapılıyor (KEX / Host Key)...");
        }
        if (t.find(L"Connection to") != std::wstring::npos && t.find(L"established") != std::wstring::npos) {
            return TrText(L"TCP connection established, starting SSH session...", L"TCP bağlantısı kuruldu, SSH oturumu başlatılıyor...");
        }
        if (t.find(L"Connecting to") != std::wstring::npos) {
            return std::wstring(TrText(L"Connecting to server (", L"Sunucuya bağlanılıyor (")) + addr + L")...";
        }
        if (t.find(L"Starting address resolution") != std::wstring::npos) {
            return TrText(L"Resolving server address (DNS)...", L"Sunucu adresi çözümleniyor (DNS)...");
        }
    }
    return m_sshLogs.back().text;
}

// Connection status observes the decoded screen. It must never edit the VT stream:
// ConPTY owns cursor coordinates and will replay that same screen after resize.
void TerminalTab::ProcessSshOutput(const std::string&) {
    if (!SshPending()) return;
    std::string visible;
    // Login starts at the top, even when the window has 80+ rows. Looking only
    // at the bottom 24 rows hides password/host-key prompts behind the loader.
    for (const auto& line : m_screen.GetTailLines(m_screen.Rows())) visible += line + "\n";
    const std::string lower = Lower(visible);
    for (const char* failure : {"connection refused", "connection timed out", "could not resolve hostname",
                                "host key verification failed", "permission denied ("}) {
        if (lower.find(failure) != std::string::npos) {
            AddSshLog(L"!", Utf8ToWide(visible), 0xF05454);
            m_sshStage = SshStage::Failed;
            return;
        }
    }
    std::string line = m_screen.CursorLineText();
    const size_t end = line.find_last_not_of(" \r\n\t");
    line = end == std::string::npos ? std::string{} : line.substr(0, end + 1);
    // A banner or password prompt is not a shell. Never time out into a false
    // Connected state. Nonstandard prompts remain accessible via Show terminal.
    const auto prompt = Lower(line);
    bool question = prompt.find("yes/no") != std::string::npos && EndsWith(prompt, "?");
    if (EndsWith(prompt, ":")) {
        for (const char* hint : {"password", "passphrase", "verification code", "one-time", "otp:",
                                 "token:", "response:", "pin:", "parola", "login as:"})
            if (prompt.find(hint) != std::string::npos) question = true;
    }
    if (question) {
        if (m_sshStage != SshStage::AwaitingInput)
            AddSshLog(L"…", TrText(L"Waiting for login input", L"Giriş yanıtı bekleniyor"));
        m_sshStage = SshStage::AwaitingInput;
        return;
    }
    const bool shellPrompt = !line.empty() &&
        (line.back() == '$' || line.back() == '#' || line.back() == '>' || line.back() == '%' ||
         EndsWith(line, "❯") || EndsWith(line, "➜"));
    if (shellPrompt || !m_screen.CommandMarks().empty() || m_screen.IsAltBuffer()) {
        m_sshStage = SshStage::Connected;
        AddSshLog(L"✓", TrText(L"SSH terminal is ready for interaction", L"SSH terminali etkileşime hazır"));
    }
}

bool TerminalTab::RestartSsh(std::wstring* err) {
    Close();
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_queue.clear();
        m_posted = false;
    }
    m_exited = false;
    m_dead = false;
    m_endSeenAt = 0;
    m_totalBytes = 0;
    m_windowBytes = 0;
    m_screen.Clear();
    m_parser.Reset();
    SetSshSession(m_sshHost);
    if (m_sshHost.kind == AuthKind::Password && !m_sshHost.password.empty())
        SetAutoPassword(m_sshHost.password, m_sshHost.username, m_sshHost.address);
    std::wstring failure;
    if (Start(m_profile, m_cols, m_rows, m_notifyHwnd, m_notifyMsg, &failure)) return true;
    WipeAutoPassword();
    m_sshStage = SshStage::Failed;
    if (failure.empty()) failure = TrText(L"Could not restart SSH.", L"SSH yeniden başlatılamadı.");
    AddSshLog(L"!", failure, 0xF05454);
    if (err) *err = failure;
    return false;
}

bool TerminalTab::Pump() {
    if (m_database) return m_database->Poll();
    if (m_isSftp) return false;

    // Bitis kuyruktan ONCE okunur: okuyucu bittiyse kuyruga yazacagi her sey
    // yazilmistir ve asagidaki bosaltma son ciktiyi da kapsar.
    const bool ended     = m_exited || m_dead;
    const bool readerEnd = !ended && !m_pty.Running();
    const bool procEnd   = !ended && m_pty.ProcessExited();

    std::string chunk;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        chunk.swap(m_queue);
        m_posted.store(false, std::memory_order_release);
    }

    bool changed = false;
    if (!chunk.empty()) {
        changed = true;
        if (m_recorder && m_recorder->IsRecording()) {
            m_recorder->RecordOutput(chunk.data(), chunk.size());
        }
        // Parse exactly the bytes received, once, for every transport/stage.
        // Never recolor MOTD, inject a local banner or discard debug-looking user output.
        m_parser.Feed(chunk.data(), chunk.size());
        ProcessSshOutput(chunk);

        m_totalBytes += chunk.size();
        m_windowBytes += chunk.size();

        const int64_t now = NowTicks();
        const int64_t elapsed = now - m_windowStart;
        if (elapsed >= TickFreq() / 2) {
            const double sec = (double)elapsed / (double)TickFreq();
            m_mbps = (double)m_windowBytes / sec / (1024.0 * 1024.0);
            m_windowBytes = 0;
            m_windowStart = now;
        }
    }

    AutoPasswordStep(!chunk.empty());

    if (changed || m_screen.Revision() != m_lastAgentCheckRev) {
        UpdateAgentStatus();
    }

    if (readerEnd || procEnd) {
        const int64_t now = NowTicks();
        if (m_endSeenAt == 0) m_endSeenAt = now;
        const int64_t waited = now - m_endSeenAt;
        bool settle = false;
        if (procEnd) settle = readerEnd || (chunk.empty() && waited >= TickFreq() / 10);
        else         settle = waited >= TickFreq();
        if (settle) {
            if (SshPending()) {
                m_sshStage = SshStage::Failed;
                if (m_sshLogs.empty()) {
                    AddSshLog(L"❗", L"Connection closed by remote host (exit " + std::to_wstring(m_pty.ExitCode()) + L")", 0xF05454);
                } else if (m_sshLogs.back().icon != L"😨" && m_sshLogs.back().icon != L"❗") {
                    AddSshLog(L"😨", L"No more authentication methods to try", 0xF05454);
                }
            }
            FinishSession(procEnd);
            changed = true;
        }
    }
    return changed;
}

void TerminalTab::FinishSession(bool haveExitCode) {
    WipeAutoPassword();
    const DWORD code = haveExitCode ? m_pty.ExitCode() : 0;
    if (code == 0) {
        m_exited = true;
        return;
    }
    m_dead = true;
    // NTSTATUS gibi buyuk kodlar (0xC000013A) onaltilik daha okunur.
    char num[16];
    if (code <= 0xFFFF) snprintf(num, sizeof(num), "%lu", (unsigned long)code);
    else                snprintf(num, sizeof(num), "0x%08lX", (unsigned long)code);
    // Onde SGR 0: olen uygulama ters video/arka plan birakmis olabilir.
    const std::string line = std::string("\x1b[0m\r\n\x1b[90m[islem ") + num +
                             " koduyla sonlandi - kapatmak icin Enter]\x1b[0m";
    m_parser.Feed(line.data(), line.size());
}

void TerminalTab::SetAutoPassword(const std::wstring& pw, const std::wstring& user,
                                  const std::wstring& host) {
    WipeAutoPassword();
    if (pw.empty() || host.empty()) return;

    auto trim = [](std::string s) {
        const size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string();
        return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
    };
    // ssh komutu adresi kirpilmis haliyle aliyor (BuildSshCommand), biz de oyle.
    std::string u = trim(Lower(WideToUtf8(user)));
    std::string h = trim(Lower(WideToUtf8(host)));
    // "user@host" bicimi verildiyse ayir.
    const size_t at = h.rfind('@');
    if (at != std::string::npos) {
        if (u.empty()) u = h.substr(0, at);
        h.erase(0, at + 1);
    }
    // IPv6 koseli parantezleri istemde yer almaz.
    if (h.size() > 2 && h.front() == '[' && h.back() == ']') h = h.substr(1, h.size() - 2);
    if (h.empty()) return;

    m_autoPassword = pw;
    m_autoUser = u;
    m_autoHost = h;
    m_autoDeadline = NowTicks() + TickFreq() * 60;
}

void TerminalTab::WipeAutoPassword() {
    if (!m_autoPassword.empty()) {
        SecureZeroMemory(m_autoPassword.data(), m_autoPassword.size() * sizeof(wchar_t));
    }
    m_autoPassword.clear();
    m_autoUser.clear();
    m_autoHost.clear();
    m_autoDeadline = 0;
}

void TerminalTab::AutoPasswordStep(bool gotOutput) {
    if (m_autoPassword.empty()) return;
    if (NowTicks() > m_autoDeadline) { WipeAutoPassword(); return; }
    if (!gotOutput) return;

    // Ham bayt yerine ekrana bakilir: VT dizileri ve parca parca gelen cikti
    // eslesmeyi bozmaz. "Istem ciktinin sonunda mi" sorusunu imlecin mantiksal
    // satiri cevaplar; imlecten sonrasi bos olmali.
    std::string line = Lower(m_screen.CursorLineText());
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
    if (line.empty()) return;

    // Baska host (jump host) ya da baska kullanici adini tasiyan istem eslesmez:
    // yanitlanmaz, parola da silinmez; hedefin kendi istemi ondan sonra gelir.
    bool match = TargetPrompt(line, m_autoUser, m_autoHost);
    // Guncel OpenSSH "%s@%s" basar, 7.x ve oncesi "%.30s@%.128s" ile kirpiyordu.
    if (!match && (m_autoUser.size() > 30 || m_autoHost.size() > 128)) {
        match = TargetPrompt(line, m_autoUser.substr(0, 30), m_autoHost.substr(0, 128));
    }
    if (match) {
        std::string pw = WideToUtf8(m_autoPassword);
        WipeAutoPassword();
        Write(pw);
        Write("\r", 1);
        SecureZeroMemory(pw.data(), pw.size());
        return;
    }

    // Kabuk istemi goruldu: kimlik dogrulama bitti (orn. anahtarla girildi).
    // Sonraki su/sudo/mysql -p/ic ssh istemlerine parola gitmesin.
    const char last = line.back();
    if (last == '$' || last == '#' || last == '>' || last == '%') WipeAutoPassword();
}

void TerminalTab::Write(const char* data, size_t len) {
    if (m_exited || m_dead) return; // surec yok; conhost'a yazmanin anlami yok
    if (m_sshStage == SshStage::AwaitingInput &&
        (memchr(data, '\r', len) || memchr(data, '\n', len))) {
        m_sshStage = SshStage::Connecting;
        AddSshLog(L"…", TrText(L"Waiting for the remote shell...", L"Uzak kabuk hazırlanıyor..."));
    }
    if (m_recorder && m_recorder->IsRecording()) {
        m_recorder->RecordInput(data, len);
    }
    m_pty.Write(data, len);
}

void TerminalTab::Resize(int cols, int rows) {
    if (m_database) return;
    cols = std::clamp(cols, 1, 1000);
    rows = std::clamp(rows, 1, 1000);
    if (cols == m_screen.Cols() && rows == m_screen.Rows()) return;
    // Consume already queued output in its original geometry before resizing.
    if (m_pty.Running()) Pump();
    m_cols = cols; m_rows = rows;
    if (m_recorder && m_recorder->IsRecording()) {
        m_recorder->RecordResize(cols, rows);
    }
    m_screen.Resize(cols, rows);
    // Surec bittiyse conhost'u yeniden cizdirmeyelim; "[islem ... sonlandi]"
    // satirini bilmedigi icin ustune yazar.
    if (!m_exited && !m_dead) {
        m_pty.Resize((short)cols, (short)rows);
    }
}

void TerminalTab::ClearBeforeThemeApply() {
    // Theme changes are render-only: preserve parser, pen, text and cursor state.
}

void TerminalTab::ApplyTheme() {
    if (m_isSftp) return;
    m_screen.Touch();
}

std::wstring TerminalTab::Title() const {
    if (!m_customTitle.empty()) return m_customTitle;
    return m_autoTitle.empty() ? BaseTitle() : m_autoTitle;
}

std::wstring TerminalTab::BaseTitle() const {
    if (!m_profile.name.empty()) return m_profile.name;
    if (!m_sshHost.address.empty()) return m_sshHost.Display();
    return L"Terminal";
}

bool TerminalTab::BellPending() {
    return m_bell.exchange(false, std::memory_order_relaxed);
}

void TerminalTab::UpdateAgentStatus() {
    m_lastAgentCheckRev = m_screen.Revision();
    auto tail = m_screen.GetTailLines(12);
    m_agentStatus = AgentDetector::Instance().Detect(tail, m_screen.title, m_screen.IsAltBuffer());

    const bool isBlocked = (m_agentStatus.state == AgentState::Blocked);
    if (isBlocked && !m_wasBlocked) {
        MessageBeep(MB_ICONWARNING);
        if (m_notifyHwnd) {
            FlashWindow(m_notifyHwnd, TRUE);
        }
    }
    m_wasBlocked = isBlocked;
}

bool TerminalTab::StartRecording(const std::wstring& filePath) {
    if (m_recorder && m_recorder->IsRecording()) return false;
    if (!m_recorder) {
        m_recorder = std::make_unique<SessionRecorder>();
    }
    std::string titleUtf8 = WideToUtf8(Title());
    std::string cmdUtf8 = WideToUtf8(m_profile.CommandLine());
    return m_recorder->Start(filePath, m_screen.Cols(), m_screen.Rows(), titleUtf8, cmdUtf8);
}

bool TerminalTab::StopRecording(std::wstring* savedPath) {
    if (!m_recorder || !m_recorder->IsRecording()) return false;
    if (savedPath) {
        *savedPath = m_recorder->FilePath();
    }
    return m_recorder->Stop();
}

bool TerminalTab::IsRecording() const {
    return m_recorder && m_recorder->IsRecording();
}

void TerminalTab::StartDatabase(const db::Profile& profile, bool connect) {
    m_profile.id = L"db:" + profile.id;
    m_profile.name = profile.name.empty() ? db::EngineName(profile.engine) : profile.name;
    m_profile.kind = ProfileKind::Custom;
    m_profile.badge = L"DB";
    m_profile.accent = 0x71B7E6;
    m_database = std::make_unique<db::Session>(profile);
    if (connect) m_database->Connect();
}

void TerminalTab::StartSftp(const Host* host, const Inventory& inv) {
    m_isSftp = true;
    m_profile.kind = ProfileKind::Custom;
    m_profile.badge = L"SFTP";
    m_profile.accent = 0x00B0FF;
    if (host) {
        m_profile.name = host->Display();
    } else {
        m_profile.name = L"SFTP";
    }
    m_sftp = std::make_unique<SftpController>(inv);
    if (host) {
        m_sftp->ConnectRemote(*host);
    }
}

} // namespace ft
