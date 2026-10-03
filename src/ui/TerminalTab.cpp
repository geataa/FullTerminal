#include "ui/TerminalTab.h"
#include "core/Utf8.h"
#include "core/I18n.h"

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

    return m_pty.Start(profile.CommandLine(), profile.startDir, env,
                       (short)cols, (short)rows, err);
}

void TerminalTab::Close() {
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
    m_sshLogs.clear();
    m_sshLineBuf.clear();
    m_screen.Clear();
    m_parser.Reset();
    m_seenAuthMethods = false;
    m_sshBannerInjected = false;
    m_sshMotdDone = false;
    m_cachedSshBanner.clear();
    m_cachedSshMotd.clear();
    m_cachedSshPrompt.clear();
    m_userHasRunCommand = false;
    m_sshInResizeRepaint = false;
    m_serverCipher.clear();
    m_serverMac.clear();
    m_serverComp.clear();

    const std::wstring addr = h.address.empty() ? L"127.0.0.1" : h.address;
    const std::wstring port = std::to_wstring(h.port ? h.port : 22);

    AddSshLog(L"👤", L"Starting a new connection to: \"" + addr + L"\" port \"" + port + L"\"");
    AddSshLog(L"⚙️", L"Starting address resolution of \"" + addr + L"\"");
    AddSshLog(L"⚙️", L"Address resolution finished");
    AddSshLog(L"⚙️", L"Connecting to \"" + addr + L"\" port \"" + port + L"\"");
}

static std::string StripAnsiEscapes(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\x1b') {
            if (i + 1 < str.size() && (str[i + 1] == '[' || str[i + 1] == ']')) {
                i += 2;
                while (i < str.size() && str[i] >= 0x20 && str[i] <= 0x3F) ++i;
                while (i < str.size() && str[i] >= 0x40 && str[i] <= 0x7E) {
                    if (str[i] == 'm' || str[i] == 'h' || str[i] == 'l' || str[i] == 'r' ||
                        str[i] == 'H' || str[i] == 'J' || str[i] == 'K' || str[i] == '\a') {
                        break;
                    }
                    ++i;
                }
            }
        } else {
            out.push_back(str[i]);
        }
    }
    return out;
}

static std::string FilterSshDebug(const std::string& chunk) {
    if (chunk.find("debug") == std::string::npos && chunk.find("OpenSSH") == std::string::npos) {
        return chunk;
    }
    std::string out;
    out.reserve(chunk.size());
    size_t start = 0;
    while (start < chunk.size()) {
        size_t end = chunk.find('\n', start);
        std::string line;
        if (end == std::string::npos) {
            line = chunk.substr(start);
            start = chunk.size();
        } else {
            line = chunk.substr(start, end - start + 1);
            start = end + 1;
        }
        std::string cl = StripAnsiEscapes(line);
        while (!cl.empty() && (cl.back() == '\r' || cl.back() == '\n' || cl.back() == ' ')) cl.pop_back();
        if (cl.rfind("debug1:", 0) == 0 || cl.rfind("debug2:", 0) == 0 || cl.rfind("debug3:", 0) == 0 ||
            cl.rfind("OpenSSH", 0) == 0) {
            continue;
        }
        out += line;
    }
    return out;
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
    if (m_sshLogs.empty()) return TrText(L"Connecting to server...", L"Sunucuya bağlanılıyor...");
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

static bool IsShellPrompt(const std::string& str) {
    std::string cl = StripAnsiEscapes(str);
    while (!cl.empty() && (cl.back() == ' ' || cl.back() == '\t' || cl.back() == '\r' || cl.back() == '\n')) {
        cl.pop_back();
    }
    if (cl.empty()) return false;
    const char last = cl.back();
    if (last == '$' || last == '#' || last == '>') return true;
    if (last == '%') {
        // Eger % isaretinden once rakam varsa bu istatistik yuzdesidir (orn. 35%, 24.5%), istem degildir!
        if (cl.size() >= 2 && (isdigit((unsigned char)cl[cl.size() - 2]) || cl[cl.size() - 2] == '.')) {
            return false;
        }
        return true;
    }
    return false;
}

static std::string ColorizeMotdLine(const std::string& raw) {
    std::string cl = StripAnsiEscapes(raw);
    while (!cl.empty() && (cl.back() == '\r' || cl.back() == '\n')) cl.pop_back();
    if (cl.empty()) return "\r\n";

    std::string trimmed = TrimWs(cl);
    if (trimmed.empty()) return "\r\n";

    // 1. Welcome to ...
    if (trimmed.rfind("Welcome to ", 0) == 0) {
        return "\x1b[1;36m  ⚡ " + trimmed.substr(0, 11) + 
               "\x1b[1;32m" + trimmed.substr(11) + "\x1b[0m\r\n";
    }

    // 2. Last login: ...
    if (trimmed.rfind("Last login:", 0) == 0) {
        size_t fromPos = trimmed.find(" from ");
        if (fromPos != std::string::npos) {
            std::string before = trimmed.substr(0, fromPos);
            std::string ip = trimmed.substr(fromPos + 6);
            return "\x1b[1;33m  🕒 " + before + 
                   " \x1b[37mfrom\x1b[0m \x1b[1;32m" + ip + "\x1b[0m\r\n";
        }
        return "\x1b[1;33m  🕒 " + trimmed + "\x1b[0m\r\n";
    }

    // 3. System information as of ...
    if (trimmed.rfind("System information as of", 0) == 0) {
        return "\x1b[1;35m  📊 " + trimmed + "\x1b[0m\r\n";
    }

    // 4. Bullet points: * Documentation: https://...
    if (trimmed.rfind("* ", 0) == 0) {
        std::string rest = trimmed.substr(2);
        size_t httpPos = rest.find("http://");
        if (httpPos == std::string::npos) httpPos = rest.find("https://");
        if (httpPos != std::string::npos) {
            std::string label = rest.substr(0, httpPos);
            std::string url = rest.substr(httpPos);
            return "\x1b[1;33m  • \x1b[37m" + label + 
                   "\x1b[4;36m" + url + "\x1b[0m\r\n";
        }
        return "\x1b[1;33m  • \x1b[37m" + rest + "\x1b[0m\r\n";
    }

    // 5. System metrics (System load:, Memory usage:, Usage of /:, Processes:, etc.)
    if (trimmed.find("System load:") != std::string::npos ||
        trimmed.find("Memory usage:") != std::string::npos ||
        trimmed.find("Usage of ") != std::string::npos ||
        trimmed.find("Swap usage:") != std::string::npos ||
        trimmed.find("Processes:") != std::string::npos ||
        trimmed.find("Users logged in:") != std::string::npos ||
        trimmed.find("IPv4 address") != std::string::npos) {
        
        std::string colored = "  ";
        std::istringstream iss(trimmed);
        std::string token;
        while (iss >> token) {
            if (token.back() == ':') {
                colored += "\x1b[36m" + token + "\x1b[0m ";
            } else if (!token.empty() && (isdigit((unsigned char)token[0]) || token[0] == '.')) {
                colored += "\x1b[1;32m" + token + "\x1b[0m ";
            } else {
                colored += "\x1b[37m" + token + "\x1b[0m ";
            }
        }
        return colored + "\r\n";
    }

    // 6. Package updates
    if (trimmed.find("can be updated") != std::string::npos ||
        trimmed.find("can be applied") != std::string::npos) {
        if (trimmed.find("0 updates") != std::string::npos) {
            return "\x1b[1;32m  ✓ " + trimmed + "\x1b[0m\r\n";
        }
        return "\x1b[1;33m  ⚠️ " + trimmed + "\x1b[0m\r\n";
    }

    // Eger raw zaten zengin renk kacislari tasiyorsa onu koru
    if (raw.find("\x1b[38;") != std::string::npos || raw.find("\x1b[48;") != std::string::npos ||
        raw.find("\x1b[3") != std::string::npos || raw.find("\x1b[9") != std::string::npos) {
        return raw;
    }

    // 7. Diger metin satirlari: varsayilan metin tonu
    return "  \x1b[37m" + trimmed + "\x1b[0m\r\n";
}

std::string TerminalTab::BuildSshBanner() const {
    const std::string user = WideToUtf8(m_sshHost.username.empty() ? L"root" : m_sshHost.username);
    const std::string addr = WideToUtf8(m_sshHost.address.empty() ? L"127.0.0.1" : m_sshHost.address);
    const std::string port = std::to_string(m_sshHost.port ? m_sshHost.port : 22);
    const std::string label = WideToUtf8(m_sshHost.label);

    std::string authStr = (m_sshHost.kind == AuthKind::Password) ? 
        WideToUtf8(TrText(L"Password (DPAPI Encrypted)", L"Parola (DPAPI Güvenli)")) :
        (m_sshHost.kind == AuthKind::Key) ? 
        WideToUtf8(TrText(L"SSH Asymmetric Key", L"SSH Asimetrik Anahtarı")) : "SSH Agent";

    const std::string srvLbl   = WideToUtf8(TrText(L"SERVER   ", L"SUNUCU   "));
    const std::string protoLbl = WideToUtf8(TrText(L"PROTOCOL ", L"PROTOKOL "));
    const std::string idLbl    = WideToUtf8(TrText(L"IDENTITY: ", L"KİMLİK: "));
    const std::string statLbl  = WideToUtf8(TrText(L"STATUS   ", L"DURUM    "));
    const std::string actLbl   = WideToUtf8(TrText(L"CONNECTION ACTIVE", L"BAĞLANTI AKTİF"));
    const std::string tunLbl   = WideToUtf8(TrText(L"[SECURE ENCRYPTED TUNNEL]", L"[GÜVENLİ ENCRYPTED TÜNEL]"));

    // Direct2D / VT ANSI Semantik Renkler (Her tema ile otomatik uyumlu):
    // 36 (Cyan / Cerceve), 1;36 (Vurgulu Baslik), 37 (Etiket), 1;32 (Sunucu / Durum), 35 (Protokol), 33 (Kimlik)
    std::string banner = 
        "\r\n"
        "\x1b[36m  ╭── \x1b[1;36m[ ⚡ FULLTERMINAL // SECURE REMOTE CLUSTER ]\x1b[0m\x1b[36m ────────────────────────────\x1b[0m\r\n"
        "\x1b[36m  │\x1b[0m  \x1b[37m◈ " + srvLbl + ":\x1b[0m \x1b[1;32m" + user + "@" + addr + ":" + port + "\x1b[0m" +
        (label.empty() ? "" : (" \x1b[1;36m[" + label + "]\x1b[0m")) + "\r\n"
        "\x1b[36m  │\x1b[0m  \x1b[37m◈ " + protoLbl + ":\x1b[0m \x1b[35mSSH-2.0 • CIPHER: AES-GCM\x1b[0m \x1b[90m•\x1b[0m \x1b[33m" + idLbl + authStr + "\x1b[0m\r\n"
        "\x1b[36m  │\x1b[0m  \x1b[37m◈ " + statLbl + ":\x1b[0m \x1b[1;32m● " + actLbl + "\x1b[0m \x1b[36m" + tunLbl + "\x1b[0m\r\n"
        "\x1b[36m  ╰──────────────────────────────────────────────────────────────────────────\x1b[0m\r\n\r\n";

    return banner;
}

void TerminalTab::InjectSshWelcomeBanner() {
    if (m_sshBannerInjected) return;
    m_sshBannerInjected = true;
    m_screen.Clear();
    m_parser.Reset();
    m_screen.SetCursorVisible(true);

    m_cachedSshBanner = BuildSshBanner();
    m_parser.Feed(m_cachedSshBanner.data(), m_cachedSshBanner.size());
    m_screen.SetCursorVisible(true);
}

void TerminalTab::ProcessSshPostConnect(const std::string& chunk) {
    m_sshLineBuf += chunk;
    size_t pos = 0;
    while (true) {
        size_t nl = m_sshLineBuf.find('\n', pos);
        if (nl == std::string::npos) break;
        std::string raw = m_sshLineBuf.substr(pos, nl - pos + 1);
        pos = nl + 1;

        std::string cl = StripAnsiEscapes(raw);
        while (!cl.empty() && (cl.back() == '\r' || cl.back() == '\n' || cl.back() == ' ')) cl.pop_back();

        // debug loglarini asla terminal ekranina basma
        if (cl.rfind("debug1:", 0) == 0 || cl.rfind("debug2:", 0) == 0 || cl.rfind("debug3:", 0) == 0) {
            continue;
        }

        // Bos satir gelirse aradaki dikey boslugu koru
        if (cl.empty()) {
            m_cachedSshMotd += "\r\n";
            m_parser.Feed("\r\n", 2);
            continue;
        }

        // Uzak sunucunun gercek ciktisi basladi
        if (!m_sshBannerInjected) {
            InjectSshWelcomeBanner();
        }

        // Kabuk istemi (prompt) goruldu mu?
        if (IsShellPrompt(cl)) {
            m_sshMotdDone = true;
            m_cachedSshPrompt = raw;
            m_parser.Feed(raw.data(), raw.size());
            m_screen.SetCursorVisible(true);
            continue;
        }

        std::string styled = ColorizeMotdLine(raw);
        m_cachedSshMotd += styled;
        m_parser.Feed(styled.data(), styled.size());
    }
    m_sshLineBuf.erase(0, pos);

    // Kalan parca kabuk istemi ise (satir sonu \n olmayan prompt)
    if (!m_sshLineBuf.empty()) {
        std::string cl = StripAnsiEscapes(m_sshLineBuf);
        while (!cl.empty() && (cl.back() == '\r' || cl.back() == '\n' || cl.back() == ' ')) cl.pop_back();
        if (!cl.empty() && cl.rfind("debug", 0) != 0) {
            if (IsShellPrompt(cl)) {
                if (!m_sshBannerInjected) {
                    InjectSshWelcomeBanner();
                }
                m_sshMotdDone = true;
                m_cachedSshPrompt = m_sshLineBuf;
                m_parser.Feed(m_sshLineBuf.data(), m_sshLineBuf.size());
                m_sshLineBuf.clear();
                m_screen.SetCursorVisible(true);
            }
        }
    }
}

void TerminalTab::ProcessSshOutput(const std::string& chunk) {
    m_sshLineBuf += chunk;
    size_t pos = 0;
    while (true) {
        size_t nl = m_sshLineBuf.find('\n', pos);
        if (nl == std::string::npos) break;
        std::string raw = m_sshLineBuf.substr(pos, nl - pos);
        pos = nl + 1;

        while (!raw.empty() && (raw.back() == '\r' || raw.back() == ' ')) raw.pop_back();
        if (raw.empty()) continue;

        std::string clean = StripAnsiEscapes(raw);
        while (!clean.empty() && (clean.back() == '\r' || clean.back() == ' ')) clean.pop_back();
        if (clean.empty()) continue;

        bool isDebug = false;
        std::string line = clean;
        if (line.rfind("debug1: ", 0) == 0)      { isDebug = true; line = line.substr(8); }
        else if (line.rfind("debug2: ", 0) == 0) { isDebug = true; line = line.substr(8); }
        else if (line.rfind("debug3: ", 0) == 0) { isDebug = true; line = line.substr(8); }

        std::string lower = Lower(line);
        const std::wstring addr = m_sshHost.address.empty() ? L"127.0.0.1" : m_sshHost.address;
        const std::wstring port = std::to_wstring(m_sshHost.port ? m_sshHost.port : 22);
        const std::wstring user = m_sshHost.username.empty() ? L"root" : m_sshHost.username;

        // Eger debug ciktisi DEGILSE:
        if (!isDebug) {
            const bool isAuthPrompt = (lower.find("password:") != std::string::npos ||
                                       lower.find("passphrase") != std::string::npos ||
                                       lower.find("parola") != std::string::npos ||
                                       lower.find("continue connecting (yes/no") != std::string::npos);
            if (isAuthPrompt) {
                if (lower.find("continue connecting") != std::string::npos) {
                    m_sshStage = SshStage::Connected;
                    m_screen.Clear();
                    m_parser.Reset();
                    m_parser.Feed(raw.data(), raw.size());
                    m_parser.Feed("\r\n", 2);
                    return;
                }
                if (!m_autoPassword.empty()) {
                    std::string pw = WideToUtf8(m_autoPassword);
                    WipeAutoPassword();
                    Write(pw.data(), pw.size());
                    Write("\r", 1);
                    SecureZeroMemory(pw.data(), pw.size());
                    AddSshLog(L"🔑", L"Parola otomatik olarak iletildi");
                    continue;
                } else {
                    m_sshStage = SshStage::Connected;
                    m_screen.Clear();
                    m_parser.Reset();
                    m_parser.Feed(raw.data(), raw.size());
                    m_parser.Feed("\r\n", 2);
                    return;
                }
            } else {
                if (lower.rfind("warning:", 0) == 0) {
                    AddSshLog(L"⚠️", Utf8ToWide(line));
                    continue;
                }
                // Diger satirlar: eger parola gonderilmisse veya auth bittiyse MOTD baslamis olabilir
                if (m_autoPassword.empty() && (lower.find("last login:") != std::string::npos ||
                                              lower.find("welcome to") != std::string::npos ||
                                              clean.back() == '$' || clean.back() == '#' ||
                                              clean.back() == '>' || clean.back() == '%')) {
                    AddSshLog(L"✔️", L"Authentication successful");
                    m_sshStage = SshStage::Connected;
                    InjectSshWelcomeBanner();
                    if (clean.back() == '$' || clean.back() == '#' || clean.back() == '>' || clean.back() == '%') {
                        m_sshMotdDone = true;
                        m_cachedSshPrompt = raw + "\r\n";
                        m_parser.Feed(raw.data(), raw.size());
                        m_parser.Feed("\r\n", 2);
                    } else {
                        std::string styled = ColorizeMotdLine(raw);
                        m_cachedSshMotd += styled;
                        m_parser.Feed(styled.data(), styled.size());
                    }
                    if (pos < m_sshLineBuf.size()) {
                        std::string rem = m_sshLineBuf.substr(pos);
                        m_sshLineBuf.clear();
                        ProcessSshPostConnect(rem);
                    } else {
                        m_sshLineBuf.clear();
                    }
                    return;
                }
            }
        }

        if (lower.find("authentication succeeded") != std::string::npos) {
            AddSshLog(L"✔️", L"Authentication successful");
        }

        if (lower.find("entering interactive session") != std::string::npos) {
            AddSshLog(L"✔️", L"Interactive session started");
            m_sshStage = SshStage::Connected;
            InjectSshWelcomeBanner();
            if (pos < m_sshLineBuf.size()) {
                std::string remainder = m_sshLineBuf.substr(pos);
                m_sshLineBuf.clear();
                ProcessSshPostConnect(remainder);
            } else {
                m_sshLineBuf.clear();
            }
            return;
        }

        if (lower.find("connection established") != std::string::npos) {
            AddSshLog(L"👤", L"Connection to \"" + addr + L"\" established");
            AddSshLog(L"⚙️", L"Starting SSH session");
        } else if (lower.find("remote software version") != std::string::npos ||
                   lower.find("remote protocol version") != std::string::npos) {
            size_t p = line.find("remote software version ");
            std::string srv;
            if (p != std::string::npos) {
                srv = line.substr(p + 24);
            } else {
                p = line.find("version ");
                srv = (p != std::string::npos) ? line.substr(p + 8) : line;
            }
            srv = TrimWs(srv);
            std::wstring wsrv = Utf8ToWide(srv);
            if (wsrv.rfind(L"SSH-", 0) != 0) wsrv = L"SSH-2.0-" + wsrv;
            AddSshLog(L"⚙️", L"Remote server: " + wsrv);
        } else if (lower.find("kex: algorithm:") != std::string::npos) {
            size_t p = line.find("kex: algorithm:");
            std::string algo = TrimWs(line.substr(p + 15));
            AddSshLog(L"⚙️", L"Agreed KEX algorithm: " + Utf8ToWide(algo));
        } else if (lower.find("kex: host key algorithm:") != std::string::npos) {
            size_t p = line.find("kex: host key algorithm:");
            std::string algo = TrimWs(line.substr(p + 24));
            AddSshLog(L"⚙️", L"Agreed Host Key algorithm: " + Utf8ToWide(algo));
        } else if (lower.find("kex: server->client cipher:") != std::string::npos) {
            size_t cPos = line.find("cipher:");
            size_t mPos = line.find("MAC:", cPos);
            size_t compPos = line.find("compression:", mPos);
            std::string cipher, mac, comp;
            if (cPos != std::string::npos && mPos != std::string::npos) {
                cipher = TrimWs(line.substr(cPos + 7, mPos - (cPos + 7)));
                mac = (compPos != std::string::npos) ? TrimWs(line.substr(mPos + 4, compPos - (mPos + 4))) : TrimWs(line.substr(mPos + 4));
                comp = (compPos != std::string::npos) ? TrimWs(line.substr(compPos + 12)) : "none";
            }
            if (mac == "<implicit>" || mac.empty()) {
                if (cipher.find("gcm") != std::string::npos) mac = "INTEGRATED-AES-GCM";
                else if (cipher.find("chacha20") != std::string::npos) mac = "INTEGRATED-POLY1305";
                else mac = "none";
            }
            m_serverCipher = Utf8ToWide(cipher);
            m_serverMac = Utf8ToWide(mac);
            m_serverComp = Utf8ToWide(comp.empty() ? "none" : comp);
        } else if (lower.find("kex: client->server cipher:") != std::string::npos) {
            size_t cPos = line.find("cipher:");
            size_t mPos = line.find("MAC:", cPos);
            size_t compPos = line.find("compression:", mPos);
            std::string cipher, mac, comp;
            if (cPos != std::string::npos && mPos != std::string::npos) {
                cipher = TrimWs(line.substr(cPos + 7, mPos - (cPos + 7)));
                mac = (compPos != std::string::npos) ? TrimWs(line.substr(mPos + 4, compPos - (mPos + 4))) : TrimWs(line.substr(mPos + 4));
                comp = (compPos != std::string::npos) ? TrimWs(line.substr(compPos + 12)) : "none";
            }
            if (mac == "<implicit>" || mac.empty()) {
                if (cipher.find("gcm") != std::string::npos) mac = "INTEGRATED-AES-GCM";
                else if (cipher.find("chacha20") != std::string::npos) mac = "INTEGRATED-POLY1305";
                else mac = "none";
            }
            std::wstring clientCipher = Utf8ToWide(cipher);
            std::wstring clientMac = Utf8ToWide(mac);
            std::wstring clientComp = Utf8ToWide(comp.empty() ? "none" : comp);

            if (!m_serverCipher.empty()) {
                AddSshLog(L"⚙️", L"Agreed server-to-client cipher: " + m_serverCipher + L" MAC: " + m_serverMac);
            }
            AddSshLog(L"⚙️", L"Agreed client-to-server cipher: " + clientCipher + L" MAC: " + clientMac);
            AddSshLog(L"⚙️", L"Agreed client-to-server compression: " + clientComp);
            if (!m_serverComp.empty()) {
                AddSshLog(L"⚙️", L"Agreed server-to-client compression: " + m_serverComp);
            }
        } else if (lower.find("ssh2_msg_newkeys received") != std::string::npos) {
            AddSshLog(L"⚙️", L"Handshake finished");
        } else if (lower.find("server host key:") != std::string::npos) {
            size_t p = line.find("SHA256:");
            std::string fp;
            if (p != std::string::npos) {
                fp = line.substr(p);
            } else {
                p = line.find("Server host key:");
                fp = (p != std::string::npos) ? line.substr(p + 16) : line;
            }
            fp = TrimWs(fp);
            AddSshLog(L"👤", L"Checking host key: " + Utf8ToWide(fp));
        } else if (lower.find("is known and matches") != std::string::npos) {
            AddSshLog(L"👤", L"Host \"" + addr + L"\":\"" + port + L"\" is known and matches");
        } else if (lower.find("authenticating to") != std::string::npos) {
            std::wstring u = user;
            size_t asPos = line.find(" as ");
            if (asPos != std::string::npos) {
                std::string rawUser = TrimWs(line.substr(asPos + 4));
                if (!rawUser.empty() && rawUser.front() == '\'' && rawUser.back() == '\'') {
                    rawUser = rawUser.substr(1, rawUser.size() - 2);
                }
                if (!rawUser.empty()) u = Utf8ToWide(rawUser);
            }
            AddSshLog(L"👤", L"Authenticating to \"" + addr + L"\":\"" + port + L"\" as \"" + u + L"\"");
        } else if (lower.find("authentications that can continue:") != std::string::npos ||
                   lower.find("authentication that can continue:") != std::string::npos) {
            size_t colon = line.find(':');
            std::string methods = (colon != std::string::npos) ? TrimWs(line.substr(colon + 1)) : "";
            if (!m_seenAuthMethods) {
                m_seenAuthMethods = true;
                AddSshLog(L"⚙️", L"Available client authentication methods: " + Utf8ToWide(methods));
                std::string chosen;
                if (m_sshHost.kind == AuthKind::Password) {
                    if (methods.find("password") != std::string::npos) chosen = "password";
                    else if (methods.find("keyboard-interactive") != std::string::npos) chosen = "keyboard-interactive";
                } else if (m_sshHost.kind == AuthKind::Key || m_sshHost.kind == AuthKind::Agent) {
                    if (methods.find("publickey") != std::string::npos) chosen = "publickey";
                }
                if (chosen.empty()) {
                    chosen = methods;
                    size_t comma = chosen.find(',');
                    if (comma != std::string::npos) chosen = chosen.substr(0, comma);
                }
                AddSshLog(L"⚙️", L"Authentication that can continue: " + Utf8ToWide(chosen));
            } else {
                std::string meth = methods;
                size_t comma = meth.find(',');
                if (comma != std::string::npos) meth = meth.substr(0, comma);
                AddSshLog(L"⚙️", L"Authentication that can continue: " + Utf8ToWide(meth.empty() ? methods : meth));
            }
        } else if (lower.find("next authentication method:") != std::string::npos ||
                   lower.find("trying private key:") != std::string::npos ||
                   lower.find("offering public key:") != std::string::npos ||
                   lower.find("authenticating using") != std::string::npos) {
            std::string meth;
            if (lower.find("password") != std::string::npos) meth = "password";
            else if (lower.find("keyboard-interactive") != std::string::npos) meth = "keyboard-interactive";
            else if (lower.find("publickey") != std::string::npos || lower.find("key") != std::string::npos) meth = "publickey";
            else if (lower.find("next authentication method:") != std::string::npos) {
                size_t colon = line.find(':');
                if (colon != std::string::npos) meth = TrimWs(line.substr(colon + 1));
            }
            if (meth.empty()) {
                meth = (m_sshHost.kind == AuthKind::Password) ? "password" : "publickey";
            }
            AddSshLog(L"👤", L"Authenticating using " + Utf8ToWide(meth) + L" method");
        } else if (lower.find("permission denied") != std::string::npos ||
                   lower.find("authentication failed") != std::string::npos) {
            std::string meth = (m_sshHost.kind == AuthKind::Password) ? "password" : "publickey";
            size_t p1 = line.find('(');
            size_t p2 = line.find(')', p1);
            if (p1 != std::string::npos && p2 != std::string::npos) {
                std::string extracted = line.substr(p1 + 1, p2 - p1 - 1);
                if (m_sshHost.kind == AuthKind::Password && extracted.find("password") != std::string::npos) {
                    meth = "password";
                } else if (m_sshHost.kind == AuthKind::Key && extracted.find("publickey") != std::string::npos) {
                    meth = "publickey";
                } else {
                    size_t comma = extracted.find(',');
                    meth = (comma != std::string::npos) ? extracted.substr(0, comma) : extracted;
                }
            }
            AddSshLog(L"❗", L"Authentication failed (" + Utf8ToWide(meth) + L")", 0xF05454);
            AddSshLog(L"⚙️", L"Partial success: no");
        } else if (lower.find("no more authentication methods to try") != std::string::npos) {
            AddSshLog(L"😨", L"No more authentication methods to try", 0xF05454);
            m_sshStage = SshStage::Failed;
        } else if (lower.find("host key verification failed") != std::string::npos) {
            AddSshLog(L"❗", L"Host key verification failed", 0xF05454);
            m_sshStage = SshStage::Failed;
        } else if (lower.find("connection refused") != std::string::npos) {
            AddSshLog(L"❗", L"Connection refused", 0xF05454);
            m_sshStage = SshStage::Failed;
        } else if (lower.find("connection timed out") != std::string::npos) {
            AddSshLog(L"❗", L"Connection timed out", 0xF05454);
            m_sshStage = SshStage::Failed;
        } else if (lower.find("could not resolve hostname") != std::string::npos) {
            AddSshLog(L"❗", L"Could not resolve hostname", 0xF05454);
            m_sshStage = SshStage::Failed;
        }
    }
    m_sshLineBuf.erase(0, pos);

    // Satir sonu (\n) olmayan son parcanin incelenmesi (orn. Parola veya Host Key onayi istemi)
    if (!m_sshLineBuf.empty()) {
        std::string trailing = Lower(TrimWs(StripAnsiEscapes(m_sshLineBuf)));
        if (!trailing.empty()) {
            const bool isPwPrompt = (TargetPrompt(trailing, m_autoUser, m_autoHost) ||
                                     trailing.find("password:") != std::string::npos ||
                                     trailing.find("passphrase") != std::string::npos ||
                                     trailing.find("parola:") != std::string::npos ||
                                     trailing.find("parolası:") != std::string::npos);
            const bool isHostKeyPrompt = (trailing.find("continue connecting (yes/no") != std::string::npos);

            if (isPwPrompt && !m_autoPassword.empty()) {
                std::string pw = WideToUtf8(m_autoPassword);
                WipeAutoPassword();
                Write(pw.data(), pw.size());
                Write("\r", 1);
                SecureZeroMemory(pw.data(), pw.size());
                m_sshLineBuf.clear();
                AddSshLog(L"🔑", L"Parola otomatik olarak iletildi");
            } else if (isHostKeyPrompt || (isPwPrompt && m_autoPassword.empty())) {
                // Kullanicidan interaktif girdi bekleniyor (parola veya host key onay sorusu):
                m_sshStage = SshStage::Connected;
                m_screen.Clear();
                m_parser.Reset();
                m_screen.SetCursorVisible(true);
                m_parser.Feed(m_sshLineBuf.data(), m_sshLineBuf.size());
                m_sshLineBuf.clear();
                m_screen.SetCursorVisible(true);
            } else if (trailing.rfind("debug", 0) != 0 && IsShellPrompt(trailing)) {
                // Kabuk istemi geldi
                m_sshStage = SshStage::Connected;
                if (!m_sshBannerInjected) {
                    InjectSshWelcomeBanner();
                }
                m_sshMotdDone = true;
                m_cachedSshPrompt = m_sshLineBuf;
                m_parser.Feed(m_sshLineBuf.data(), m_sshLineBuf.size());
                m_sshLineBuf.clear();
                m_screen.SetCursorVisible(true);
            }
        }
    }
}

bool TerminalTab::RestartSsh(std::wstring* err) {
    Close();
    m_exited = false;
    m_dead = false;
    m_endSeenAt = 0;
    m_totalBytes = 0;
    m_windowBytes = 0;
    m_screen.Clear();
    m_parser.Reset();
    SetSshSession(m_sshHost);
    return Start(m_profile, m_cols, m_rows, m_notifyHwnd, m_notifyMsg, err);
}

bool TerminalTab::Pump() {
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
        if (m_sshStage == SshStage::Connecting) {
            ProcessSshOutput(chunk);
        } else if (m_sshStage == SshStage::Connected && !m_sshMotdDone) {
            ProcessSshPostConnect(chunk);
        } else {
            if (m_sshStage != SshStage::None) {
                if (m_sshInResizeRepaint) {
                    m_sshResizeChunks++;
                    std::string cl = StripAnsiEscapes(chunk);
                    const bool hasPrompt = IsShellPrompt(cl) || (cl.find("$") != std::string::npos) ||
                                           (cl.find("#") != std::string::npos) || (cl.find(">") != std::string::npos);
                    const bool hasCursorShow = (chunk.find("\x1b[?25h") != std::string::npos);
                    if (hasPrompt || hasCursorShow || m_sshResizeChunks >= 8) {
                        m_sshInResizeRepaint = false;
                        ReapplySshWelcome();
                        m_screen.SetCursorVisible(true);
                    }
                } else {
                    std::string filtered = FilterSshDebug(chunk);
                    if (!filtered.empty()) {
                        m_parser.Feed(filtered.data(), filtered.size());
                    }
                }
            } else {
                m_parser.Feed(chunk.data(), chunk.size());
            }
        }

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
            if (m_sshStage == SshStage::Connecting) {
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
    if (m_recorder && m_recorder->IsRecording()) {
        m_recorder->RecordInput(data, len);
    }
    if (m_sshStage == SshStage::Connected && !m_userHasRunCommand && data && len > 0) {
        for (size_t i = 0; i < len; ++i) {
            if (data[i] == '\r' || data[i] == '\n') {
                m_userHasRunCommand = true;
                break;
            }
        }
    }
    m_screen.SetCursorVisible(true);
    m_pty.Write(data, len);
}

void TerminalTab::Resize(int cols, int rows) {
    if (cols == m_screen.Cols() && rows == m_screen.Rows()) return;
    if (m_recorder && m_recorder->IsRecording()) {
        m_recorder->RecordResize(cols, rows);
    }
    m_screen.Resize(cols, rows);
    // Surec bittiyse conhost'u yeniden cizdirmeyelim; "[islem ... sonlandi]"
    // satirini bilmedigi icin ustune yazar.
    if (!m_exited && !m_dead) {
        if (m_sshStage == SshStage::Connected && !m_userHasRunCommand && !m_cachedSshBanner.empty()) {
            m_sshInResizeRepaint = true;
            m_sshResizeChunks = 0;
        }
        m_pty.Resize((short)cols, (short)rows);
        if (m_sshInResizeRepaint) {
            ReapplySshWelcome();
            m_screen.SetCursorVisible(true);
        }
    }
}

void TerminalTab::ClearBeforeThemeApply() {
    if (m_isSftp) return;
    if (m_sshStage == SshStage::Connected && !m_userHasRunCommand) {
        // Kullanicinin talebi: Tema uygulanmadan once tuvali temizle ve sifirla
        m_screen.Clear();
        m_parser.Reset();
        m_screen.SetCursorVisible(true);
    } else {
        m_screen.pen().Reset();
        m_screen.SetCursorVisible(true);
    }
}

void TerminalTab::ApplyTheme() {
    if (m_isSftp) return;
    if (m_sshStage == SshStage::Connected && !m_userHasRunCommand) {
        ReapplySshWelcome();
    } else {
        m_screen.SetCursorVisible(true);
        m_screen.Touch();
    }
}

void TerminalTab::ReapplySshWelcome() {
    if (m_sshStage != SshStage::Connected || m_userHasRunCommand) {
        return;
    }
    m_screen.Clear();
    m_parser.Reset();
    m_screen.SetCursorVisible(true);

    m_cachedSshBanner = BuildSshBanner();
    if (!m_cachedSshBanner.empty()) {
        m_parser.Feed(m_cachedSshBanner.data(), m_cachedSshBanner.size());
    }
    if (!m_cachedSshMotd.empty()) {
        m_parser.Feed(m_cachedSshMotd.data(), m_cachedSshMotd.size());
    }
    if (!m_cachedSshPrompt.empty()) {
        m_parser.Feed(m_cachedSshPrompt.data(), m_cachedSshPrompt.size());
    }
    m_screen.SetCursorVisible(true);
}

std::wstring TerminalTab::Title() const {
    if (!m_customTitle.empty()) return m_customTitle;
    if (!m_screen.title.empty()) return m_screen.title;
    return m_profile.name;
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

void TerminalTab::StartSftp(const Host* host, const Inventory& inv) {
    m_isSftp = true;
    m_profile.kind = ProfileKind::Custom;
    m_profile.badge = L"SFTP";
    m_profile.accent = 0x00B0FF;
    if (host) {
        m_customTitle = L"SFTP: " + host->Display();
        m_profile.name = m_customTitle;
    } else {
        m_customTitle = L"SFTP Gezgini";
        m_profile.name = m_customTitle;
    }
    m_sftp = std::make_unique<SftpController>(inv);
    if (host) {
        m_sftp->ConnectRemote(*host);
    }
}

} // namespace ft
