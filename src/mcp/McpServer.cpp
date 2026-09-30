#include "mcp/McpServer.h"
#include "mcp/Json.h"
#include "model/Inventory.h"
#include "model/K8sManager.h"
#include "transport/agentless/AgentlessExecutor.h"
#include "core/Settings.h"
#include "core/ShellProfiles.h"
#include "core/Utf8.h"
#include "mcp/McpBridge.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

namespace ft {

namespace {

std::wstring g_dataDir;

// Exe'nin yanindaki portable_data. Istemci (Claude Code) sunucuyu kendi proje klasorunde
// baslatir; goreli yol GUI'den farkli bir envanteri okur ve kullanicinin reposuna yazardi.
const std::wstring& DataDir() {
    if (g_dataDir.empty()) g_dataDir = PortableDataDir();
    return g_dataDir;
}

// GUI'deki degisiklikler calisan sunucuya da yansisin diye her istekte yeniden okunur.
Settings LoadSettings() {
    Settings cfg;
    cfg.Load(DataDir());
    return cfg;
}

bool ServerEnabled(const Settings& cfg) { return cfg.mcpEnabled && cfg.mcpStdio; }
// Keyfi komut salt okunur olamaz: ft_exec her zaman yazma sayilir.
bool ExecAllowed(const Settings& cfg) { return ServerEnabled(cfg) && cfg.mcpAllowRun && !cfg.mcpReadOnly; }
bool FilesAllowed(const Settings& cfg) { return ServerEnabled(cfg) && cfg.mcpAllowFiles; }

const char* const kDisabledMsg = "MCP sunucusu ayarlarda kapali (Ayarlar > MCP).";

json::Value BuildToolsList(const Settings& cfg) {
    json::Value tools = json::Value::Array();
    if (!ServerEnabled(cfg)) return tools;

    // 1. ft_systems
    {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_systems";
        tool["description"] = "List available systems, shells, WSL distros, Docker containers, Kubernetes resources and SSH hosts in compact table format. The ID column is what 'system' accepts in the other tools.";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        {
            json::Value f = json::Value::Object();
            f["type"] = "string";
            f["description"] = "Optional substring filter on ID, name or path";
            props["filter"] = f;

            json::Value t = json::Value::Object();
            t["type"] = "string";
            t["description"] = "Filter by type: 'all', 'local', 'wsl', 'docker', 'ssh', 'k8s'";
            props["type"] = t;
        }
        schema["properties"] = props;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    // 2. ft_exec
    if (ExecAllowed(cfg)) {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_exec";
        tool["description"] = "Execute a command on a target system with timeout and automatic SkyMemory-style Context Budget Guard.";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        {
            json::Value sys = json::Value::Object();
            sys["type"] = "string";
            sys["description"] = "Target system ID from ft_systems (e.g. 'local' (PowerShell), 'cmd', 'pwsh', 'gitbash', 'wsl:Ubuntu-24.04', 'docker:media-creator', 'k8s:default/api' (pod) or 'k8s:default/deploy/api', an SSH host ID or label, or 'ssh:user@host'). Default: 'local'";
            props["system"] = sys;

            json::Value cmd = json::Value::Object();
            cmd["type"] = "string";
            cmd["description"] = "Shell command or script to execute (PowerShell for local, sh for WSL/Docker/K8s, the login shell for SSH)";
            props["command"] = cmd;

            json::Value to = json::Value::Object();
            to["type"] = "integer";
            to["description"] = "Timeout in milliseconds (default: 15000, max: 600000). The whole process tree is killed on timeout.";
            props["timeout_ms"] = to;

            json::Value ml = json::Value::Object();
            ml["type"] = "integer";
            ml["description"] = "Max lines before context guard triggers (default: 50)";
            props["max_lines"] = ml;
        }
        schema["properties"] = props;
        json::Value req = json::Value::Array();
        req.push_back("command");
        schema["required"] = req;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    // 3. ft_fs
    if (FilesAllowed(cfg)) {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_fs";
        tool["description"] = cfg.mcpReadOnly
            ? "Unified virtual filesystem operations across local, WSL, Docker, K8s and SSH systems without remote agents. Read-only mode: 'write' is disabled."
            : "Unified virtual filesystem operations across local, WSL, Docker, K8s and SSH systems without remote agents.";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        {
            json::Value sys = json::Value::Object();
            sys["type"] = "string";
            sys["description"] = "Target system ID from ft_systems (default: 'local')";
            props["system"] = sys;

            json::Value act = json::Value::Object();
            act["type"] = "string";
            act["description"] = cfg.mcpReadOnly
                ? "Action: 'list' (directory listing), 'read' (read text file), 'info' (file metadata)"
                : "Action: 'list' (directory listing), 'read' (read text file), 'write' (write file), 'info' (file metadata)";
            props["action"] = act;

            json::Value p = json::Value::Object();
            p["type"] = "string";
            p["description"] = "Target file or directory path";
            props["path"] = p;

            if (!cfg.mcpReadOnly) {
                json::Value c = json::Value::Object();
                c["type"] = "string";
                c["description"] = "Content to write (for 'write' action)";
                props["content"] = c;
            }

            json::Value off = json::Value::Object();
            off["type"] = "integer";
            off["description"] = "Starting line offset for 'read' action (default: 0)";
            props["offset_lines"] = off;

            json::Value lim = json::Value::Object();
            lim["type"] = "integer";
            lim["description"] = "Max lines to return for 'read' or 'list' (default: 50)";
            props["limit_lines"] = lim;
        }
        schema["properties"] = props;
        json::Value req = json::Value::Array();
        req.push_back("action");
        req.push_back("path");
        schema["required"] = req;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    // ft_terminal: GUI ile IPC yok; sahte basari donmesin diye listeden cikarildi.

    // 4. ft_vault
    {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_vault";
        tool["description"] = cfg.mcpReadOnly
            ? "List FullTerminal vault SSH hosts and identities (no secrets). Read-only mode: add/remove are disabled."
            : "Manage FullTerminal vault connections, SSH hosts, and credentials (secrets are never returned).";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        {
            json::Value act = json::Value::Object();
            act["type"] = "string";
            act["description"] = cfg.mcpReadOnly ? "Action: 'list'" : "Action: 'list', 'add_host', 'remove_host'";
            props["action"] = act;

            if (!cfg.mcpReadOnly) {
                json::Value lbl = json::Value::Object();
                lbl["type"] = "string";
                lbl["description"] = "Host display label";
                props["label"] = lbl;

                json::Value addr = json::Value::Object();
                addr["type"] = "string";
                addr["description"] = "Host IP or hostname";
                props["address"] = addr;

                json::Value port = json::Value::Object();
                port["type"] = "integer";
                port["description"] = "SSH port (default: 22)";
                props["port"] = port;

                json::Value user = json::Value::Object();
                user["type"] = "string";
                user["description"] = "SSH username";
                props["username"] = user;

                json::Value hid = json::Value::Object();
                hid["type"] = "string";
                hid["description"] = "Host ID (for remove_host)";
                props["id"] = hid;
            }
        }
        schema["properties"] = props;
        json::Value req = json::Value::Array();
        req.push_back("action");
        schema["required"] = req;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    // 5. ft_pane_list
    {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_pane_list";
        tool["description"] = "List all visual tabs and split panes in the running FullTerminal GUI window, including active focus, titles, and live AI agent detection states (blocked/working/idle).";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        schema["properties"] = props;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    // 6. ft_pane_split
    if (ExecAllowed(cfg)) {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_pane_split";
        tool["description"] = "Split the active or specified terminal tab in FullTerminal GUI into a new pane (vertical or horizontal), optionally launching a specific system/shell and executing an initial command.";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        {
            json::Value dir = json::Value::Object();
            dir["type"] = "string";
            dir["description"] = "Split direction: 'vertical' (side-by-side) or 'horizontal' (top-and-bottom). Default: 'vertical'";
            props["direction"] = dir;

            json::Value sys = json::Value::Object();
            sys["type"] = "string";
            sys["description"] = "Target system ID from ft_systems (e.g. 'local', 'cmd', 'pwsh', 'wsl:Ubuntu-24.04', 'docker:container', or SSH host ID). Default: active pane profile";
            props["system"] = sys;

            json::Value cmd = json::Value::Object();
            cmd["type"] = "string";
            cmd["description"] = "Optional initial shell command to execute immediately inside the new pane";
            props["command"] = cmd;

            json::Value tid = json::Value::Object();
            tid["type"] = "integer";
            tid["description"] = "Target tab index (default: active tab)";
            props["tab_id"] = tid;
        }
        schema["properties"] = props;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    // 7. ft_pane_prompt
    if (ExecAllowed(cfg)) {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_pane_prompt";
        tool["description"] = "Send input text, commands, or keystrokes to a specific terminal pane's PTY in FullTerminal GUI (like typing directly into the shell).";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        {
            json::Value in = json::Value::Object();
            in["type"] = "string";
            in["description"] = "Command or input text to send to the terminal pane";
            props["input"] = in;

            json::Value pid = json::Value::Object();
            pid["type"] = "integer";
            pid["description"] = "Target pane ID (default: focused pane)";
            props["pane_id"] = pid;

            json::Value tid = json::Value::Object();
            tid["type"] = "integer";
            tid["description"] = "Target tab index (default: active tab)";
            props["tab_id"] = tid;

            json::Value anl = json::Value::Object();
            anl["type"] = "boolean";
            anl["description"] = "Whether to append a newline ('\\n') to input if not already ending in newline (default: true)";
            props["append_newline"] = anl;
        }
        schema["properties"] = props;
        json::Value reqArr = json::Value::Array();
        reqArr.push_back("input");
        schema["required"] = reqArr;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    // 8. ft_pane_read
    {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_pane_read";
        tool["description"] = "Read the visible screen text and live AI agent detection status (idle, working, blocked on approval/question) from a terminal pane in FullTerminal GUI.";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        {
            json::Value pid = json::Value::Object();
            pid["type"] = "integer";
            pid["description"] = "Target pane ID (default: focused pane)";
            props["pane_id"] = pid;

            json::Value tid = json::Value::Object();
            tid["type"] = "integer";
            tid["description"] = "Target tab index (default: active tab)";
            props["tab_id"] = tid;

            json::Value tl = json::Value::Object();
            tl["type"] = "integer";
            tl["description"] = "Number of tail lines to read from the bottom of the screen (default: 25, max: 500)";
            props["tail_lines"] = tl;
        }
        schema["properties"] = props;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    // 9. ft_pane_wait
    {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_pane_wait";
        tool["description"] = "Wait for a terminal pane in FullTerminal GUI to enter a specific state (blocked on prompt/approval, idle/finished, working, or exited) with a timeout.";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        {
            json::Value pid = json::Value::Object();
            pid["type"] = "integer";
            pid["description"] = "Target pane ID (default: focused pane)";
            props["pane_id"] = pid;

            json::Value tid = json::Value::Object();
            tid["type"] = "integer";
            tid["description"] = "Target tab index (default: active tab)";
            props["tab_id"] = tid;

            json::Value ts = json::Value::Object();
            ts["type"] = "string";
            ts["description"] = "Target state to wait for: 'idle' (done/waiting at prompt), 'blocked' (waiting for user permission/question), 'working', 'exited', 'any_agent'. Default: 'idle'";
            props["target_state"] = ts;

            json::Value to = json::Value::Object();
            to["type"] = "integer";
            to["description"] = "Timeout in milliseconds (default: 30000, max: 300000)";
            props["timeout_ms"] = to;

            json::Value pi = json::Value::Object();
            pi["type"] = "integer";
            pi["description"] = "Polling interval in milliseconds (default: 250, min: 50)";
            props["poll_interval_ms"] = pi;
        }
        schema["properties"] = props;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    // 10. ft_swarm_spawn
    {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_swarm_spawn";
        tool["description"] = "Spawn a multi-agent swarm workspace layout in FullTerminal GUI (e.g. AI pair programming with agent + test runner + diff viewer, dual-agent code review, or devops 2x2 quad grid).";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        {
            json::Value ps = json::Value::Object();
            ps["type"] = "string";
            ps["description"] = "Preset layout: 'pair_programming' (3 panes: agent + test + git), 'dual_agent' (2 panes: left/right), 'quad_grid' (4 panes: 2x2 grid), or 'triage' (3 panes: top + 2 bottom). Default: 'pair_programming'";
            props["preset"] = ps;

            json::Value cwd = json::Value::Object();
            cwd["type"] = "string";
            cwd["description"] = "Working directory path for spawned panels";
            props["cwd"] = cwd;

            json::Value cmds = json::Value::Object();
            cmds["type"] = "array";
            cmds["description"] = "Initial shell commands to launch in each pane sequentially (e.g. ['claude', 'npm test', 'git status'])";
            props["commands"] = cmds;
        }
        schema["properties"] = props;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    // 11. ft_session_record
    {
        json::Value tool = json::Value::Object();
        tool["name"] = "ft_session_record";
        tool["description"] = "Control enterprise session recording and compliance audit logs (.ftrec binary and .cast Asciinema formats).";
        json::Value schema = json::Value::Object();
        schema["type"] = "object";
        json::Value props = json::Value::Object();
        {
            json::Value act = json::Value::Object();
            act["type"] = "string";
            act["description"] = "Recording action: 'start' (begin recording), 'stop' (end recording and save files), 'status' (query current recording state), or 'toggle'";
            props["action"] = act;

            json::Value fp = json::Value::Object();
            fp["type"] = "string";
            fp["description"] = "Optional custom destination file path (e.g. 'audit_session.ftrec')";
            props["file_path"] = fp;
        }
        schema["properties"] = props;
        tool["inputSchema"] = schema;
        tools.push_back(tool);
    }

    return tools;
}

// ---------------------------------------------------------------- yardimcilar

struct ToolResult {
    std::string text;
    bool isError = false;
};

// Denetim izi satiri. detail'e dosya icerigi ya da sir asla konmaz.
struct AuditRec {
    std::string action;
    std::string system;
    std::string detail;
    bool denied = false;
    bool hasExit = false;
    int exitCode = 0;
};

ToolResult Fail(std::string msg) { return { std::move(msg), true }; }

ToolResult Deny(AuditRec& a, std::string msg) {
    a.denied = true;
    return { std::move(msg), true };
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// UTF-8 dizisini bolmeden kisaltir.
std::string Clip(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "...";
}

// Arac argumanina donusen adlar: '-' ile baslayan deger ssh/docker/kubectl/wsl'e secenek
// olarak sizar (orn. -oProxyCommand=...), bosluk/tirnak argumani boler.
bool SafeToken(const std::string& s) {
    if (s.empty() || s[0] == '-') return false;
    for (unsigned char c : s) {
        if (c <= 0x20 || c == 0x7F || c == '"' || c == '\'') return false;
    }
    return true;
}

bool Invalid(std::string& err, const std::string& what, const std::string& v) {
    err = "Gecersiz " + what + " '" + v + "': '-' ile baslayamaz, bosluk/tirnak iceremez.";
    return false;
}

// Onay kutusunda gosterilecek metin. Bidi/kontrol karakterleri metni gorsel olarak yeniden
// siralayip zararli kismi gizleyebilir; gorunur '?' olur. Satir siniri: kutu ekrandan tasmasin.
std::wstring ApprovalText(const std::string& s, size_t maxBytes, bool& cut) {
    constexpr int kMaxLines = 30;
    std::wstring out;
    int lines = 1;
    cut = s.size() > maxBytes;
    for (wchar_t ch : Utf8ToWide(Clip(s, maxBytes))) {
        if (ch == L'\r') continue;
        if (ch == L'\n' && ++lines > kMaxLines) {
            cut = true;
            out += L"\n...";
            break;
        }
        const bool hidden = (ch < 0x20 && ch != L'\n' && ch != L'\t') || ch == 0x7F ||
                            (ch >= 0x200B && ch <= 0x200F) || (ch >= 0x202A && ch <= 0x202E) ||
                            (ch >= 0x2066 && ch <= 0x2069) || ch == 0xFEFF;
        out += hidden ? L'?' : ch;
    }
    return out;
}

// Kullaniciya sorar. Sunucu tek thread'li oldugundan modal kutu cagriyi yanit gelene kadar bekletir.
bool AskApproval(const std::string& tool, const std::string& system, const std::string& what) {
    bool sysCut = false, cut = false;
    const std::wstring sys = ApprovalText(system.empty() ? "-" : system, 200, sysCut);
    const std::wstring body = ApprovalText(what, 1500, cut);
    // Uzun bir komutun sonu kutuda gorunmez; uyari en ustte ki bos satirlarla asagi itilemesin
    const std::wstring warn = cut
        ? L"UYARI: Icerik uzun (" + std::to_wstring(what.size()) +
          L" bayt); asagida yalniz bas kismi gorunuyor. Tamamini gormeden onaylamayin.\n\n"
        : L"";
    const std::wstring text =
        L"Bir MCP istemcisi (yapay zeka ajani) su islemi yapmak istiyor:\n\n" + warn +
        L"Arac:  " + Utf8ToWide(tool) + L"\n"
        L"Sistem:  " + sys + L"\n\n" + body +
        L"\n\nIzin veriyor musunuz?";
    return MessageBoxW(nullptr, text.c_str(), L"FullTerminal MCP - Onay",
                       MB_YESNO | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND | MB_DEFBUTTON2) == IDYES;
}

std::string OneLine(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        out += (u < 0x20 || u == 0x7F) ? ' ' : c;
    }
    return Clip(out, 500);
}

// Her tools/call icin bir satir: UTC zaman, arac, sistem, izin, cikis kodu, komut/yol.
void WriteAudit(const Settings& cfg, const std::string& tool, const AuditRec& a) {
    if (!cfg.mcpAudit) return;
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char ts[40];
    snprintf(ts, sizeof(ts), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    // Arac adi da istemciden gelir: satir sonu/sekme ile sahte denetim satiri uretilemesin
    std::string line = std::string(ts) + "\t" + OneLine(tool) + (a.action.empty() ? "" : ":" + OneLine(a.action));
    line += "\tsystem=" + (a.system.empty() ? std::string("-") : OneLine(a.system));
    line += a.denied ? "\tdenied" : "\tallowed";
    line += "\texit=" + (a.hasExit ? std::to_string(a.exitCode) : std::string("-"));
    line += "\t" + OneLine(a.detail) + "\r\n";

    const std::wstring path = DataDir() + L"\\mcp-audit.log";
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) &&
        (fad.nFileSizeHigh != 0 || fad.nFileSizeLow > 5u * 1024 * 1024)) {
        MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    ::WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &w, nullptr);
    CloseHandle(h);
}

// ------------------------------------------------------ system kimligi cozumu

// ssh.exe komutuna BatchMode ekler: parola/anahtar parolasi sorulamaz, uzak taraf stdin
// beklemesin; ConnectTimeout: kara delik host'ta arac zaman asimindan once hata versin.
bool SshTargetFromHost(const Inventory& inv, const Host& h, ExecTarget& t, std::string& err) {
    std::wstring werr;
    std::wstring cmd = inv.BuildSshCommand(h, &werr);
    if (cmd.empty()) {
        err = "'" + WideToUtf8(h.Display()) + "' icin ssh komutu olusturulamadi: " +
              (werr.empty() ? std::string("adres, kullanici ya da atlama sunucusu gecersiz.") : WideToUtf8(werr));
        return false;
    }
    // Hedef her zaman "-- <adres>" ile biter; adres bosluk iceremedigi icin son " -- " ayractir.
    const size_t sep = cmd.rfind(L" -- ");
    if (sep == std::wstring::npos) {
        err = "ssh komutu beklenen bicimde degil.";
        return false;
    }
    cmd.insert(sep, L" -o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10");
    t.kind = TargetKind::Ssh;
    t.sshPrefix = cmd;
    return true;
}

bool RawSshTarget(const std::string& dest, ExecTarget& t, std::string& err) {
    const size_t at = dest.rfind('@');
    Host h;
    h.address = Utf8ToWide(at == std::string::npos ? dest : dest.substr(at + 1));
    h.username = at == std::string::npos ? L"" : Utf8ToWide(dest.substr(0, at));
    if (!SafeToken(WideToUtf8(h.address))) return Invalid(err, "ssh hedefi", dest);
    if (!h.username.empty() && !SafeToken(WideToUtf8(h.username))) return Invalid(err, "ssh kullanicisi", dest);
    Inventory inv;
    return SshTargetFromHost(inv, h, t, err);
}

// ft_systems'in listeledigi kimligi (ya da yolu/adi) calistirilabilir hedefe cevirir.
// Bilinmeyen kimlik artik sessizce "ssh <kimlik>" olarak denenmez.
bool ResolveSystem(const std::string& systemId, const Settings& cfg, ExecTarget& t, std::string& err) {
    std::string id = Trim(systemId);
    if (id.empty()) id = "local";
    t = ExecTarget{};
    t.id = id;

    // ft_systems PATH sutunu da kabul edilir
    std::string low = Lower(id);
    if (StartsWith(low, "/local/wsl/")) id = "wsl:" + id.substr(11);
    else if (StartsWith(low, "/local/docker/")) id = "docker:" + id.substr(14);
    else if (StartsWith(low, "/k8s/")) id = "k8s:" + id.substr(5);
    else if (StartsWith(low, "/local/")) id = id.substr(7);
    low = Lower(id);

    if (low == "local") {
        t.kind = TargetKind::PowerShell;
        return true;
    }

    if (StartsWith(low, "wsl:") || StartsWith(low, "wsl/") || StartsWith(low, "wsl_")) {
        const std::string distro = id.substr(4);
        if (!SafeToken(distro)) return Invalid(err, "WSL dagitimi", distro);
        t.kind = TargetKind::Wsl;
        t.name = Utf8ToWide(distro);
        t.env.emplace_back(L"WSL_UTF8", L"1"); // wsl.exe kendi hatalarini UTF-16 yerine UTF-8 yazsin
        return true;
    }

    if (StartsWith(low, "docker:") || StartsWith(low, "docker/")) {
        const std::string name = id.substr(7);
        if (!SafeToken(name)) return Invalid(err, "container adi", name);
        t.kind = TargetKind::Docker;
        t.name = Utf8ToWide(name);
        return true;
    }

    if (StartsWith(low, "k8s:") || StartsWith(low, "k8s/")) {
        // ft_systems bicimi: Pod icin k8s:<ns>/<ad>, is yukleri icin k8s:<ns>/<tur>/<ad>
        // (PATH: /k8s/<ns>/<tur>/<ad>). "ns/ad"i kubectl TUR/AD sanardi.
        const std::string rest = id.substr(4);
        std::vector<std::string> parts;
        for (size_t b = 0;;) {
            const size_t e = rest.find('/', b);
            parts.push_back(rest.substr(b, e == std::string::npos ? std::string::npos : e - b));
            if (e == std::string::npos) break;
            b = e + 1;
        }
        if (parts.size() > 3) return Invalid(err, "Kubernetes kaynagi", rest);
        std::string ns = parts.size() >= 2 ? parts[0] : "";
        const std::string kindTok = parts.size() == 3 ? Lower(parts[1]) : "";
        const std::string name = parts.back();
        if ((!ns.empty() && !SafeToken(ns)) || !SafeToken(name)) {
            return Invalid(err, "Kubernetes kaynagi", rest);
        }
        static const struct { const char* tok; const char* kind; } kKinds[] = {
            { "pod", "Pod" }, { "po", "Pod" }, { "pods", "Pod" },
            { "deploy", "Deployment" }, { "deployment", "Deployment" }, { "deployments", "Deployment" },
            { "sts", "StatefulSet" }, { "statefulset", "StatefulSet" }, { "statefulsets", "StatefulSet" },
            { "ds", "DaemonSet" }, { "daemonset", "DaemonSet" }, { "daemonsets", "DaemonSet" },
            { "rs", "ReplicaSet" }, { "replicaset", "ReplicaSet" }, { "replicasets", "ReplicaSet" },
            { "job", "Job" }, { "jobs", "Job" },
        };
        std::string kind;
        if (!kindTok.empty()) {
            for (const auto& k : kKinds) {
                if (kindTok == k.tok) { kind = k.kind; break; }
            }
            if (kind.empty()) {
                err = "Bilinmeyen Kubernetes turu '" + parts[1] + "' (pod, deploy, sts, ds, rs, job).";
                return false;
            }
        }
        K8sManager::Instance().Rescan(DataDir());
        // Tur verilmediyse YAML'dan bulunur: ayni adli kaynaklarda Pod, sonra exec'e uygun is yuku
        // (ft_systems'teki k8s:<ns>/<ad> Pod'dur). YAML'da yoksa kumede canli bir Pod varsayilir.
        const K8sResource* match = nullptr;
        int best = -1;
        for (const auto& r : K8sManager::Instance().Resources()) {
            if (r.name != name || (!ns.empty() && r.ns != ns) || (!kind.empty() && r.kind != kind)) continue;
            const int score = r.kind == "Pod" ? 2 : (K8sManager::IsExecKind(r.kind) ? 1 : 0);
            if (score > best) { best = score; match = &r; }
        }
        if (match) {
            if (kind.empty()) kind = match->kind;
            if (ns.empty()) ns = match->ns;
        }
        if (kind.empty()) kind = "Pod";
        std::string ref;
        if (kind == "Pod") ref = name;
        else if (kind == "Deployment") ref = "deploy/" + name;
        else if (kind == "StatefulSet") ref = "sts/" + name;
        else if (kind == "DaemonSet") ref = "ds/" + name;
        else if (kind == "ReplicaSet") ref = "rs/" + name;
        else if (kind == "Job") ref = "job/" + name;
        else {
            err = "'" + id + "' bir " + kind + " kaynagi; kubectl exec yalniz Pod, Deployment, StatefulSet, "
                  "DaemonSet, ReplicaSet ve Job icin calisir.";
            return false;
        }
        t.kind = TargetKind::K8s;
        t.ns = Utf8ToWide(ns);
        t.name = Utf8ToWide(ref);
        // Uygulamaya eklenen kubeconfig'ler kubectl'e de verilir (yeni terminallerle ayni kural)
        if (cfg.k8sAutoEnv) t.env = K8sManager::Instance().TerminalEnv(false);
        return true;
    }

    if (StartsWith(low, "ssh:")) return RawSshTarget(id.substr(4), t, err);

    // Yerel kabuk profilleri (ft_systems: powershell, pwsh, cmd, gitbash, msys2, nu)
    for (const auto& p : DiscoverShellProfiles()) {
        if (Lower(WideToUtf8(p.id)) != low) continue;
        switch (p.kind) {
            case ProfileKind::PowerShell:
            case ProfileKind::Pwsh:
                t.kind = TargetKind::PowerShell;
                t.exe = p.exe;
                break;
            case ProfileKind::Cmd:
                t.kind = TargetKind::Cmd;
                t.exe = p.exe;
                break;
            case ProfileKind::GitBash:
            case ProfileKind::Msys2:
                t.kind = TargetKind::LocalShell;
                t.exe = p.exe;
                t.shellFlag = L"-lc";
                t.env = p.env;
                // MSYS2 giris profili aksi halde $HOME'a gecer; komut istemcinin dizininde kosmali
                t.env.emplace_back(L"CHERE_INVOKING", L"1");
                break;
            case ProfileKind::Nushell:
                t.kind = TargetKind::LocalShell;
                t.exe = p.exe;
                t.shellFlag = L"-c";
                break;
            case ProfileKind::Wsl:
                t.kind = TargetKind::Wsl;
                t.name = p.wslDistro;
                t.env.emplace_back(L"WSL_UTF8", L"1");
                return true;
            default:
                err = "'" + id + "' ozel bir profil; ft_exec/ft_fs bunu desteklemiyor.";
                return false;
        }
        if (t.exe.empty()) {
            err = "'" + id + "' kabugunun exe'si bulunamadi.";
            return false;
        }
        return true;
    }
    if (low == "powershell") { t.kind = TargetKind::PowerShell; return true; }
    if (low == "cmd") { t.kind = TargetKind::Cmd; return true; }
    if (low == "pwsh") {
        err = "pwsh (PowerShell 7) kurulu degil; 'local' ya da 'powershell' kullanin.";
        return false;
    }

    // Envanterdeki SSH host'lari: kimlik, etiket, adres, user@adres ya da /ssh/... yolu
    Inventory inv;
    inv.Load(DataDir());
    const std::wstring wid = Utf8ToWide(id);
    const Host* host = inv.FindHost(wid);
    if (!host) {
        for (const auto& h : inv.hosts()) {
            const std::wstring grp = h.group.empty() ? L"" : h.group + L"/";
            const std::wstring path = L"/ssh/" + grp + h.Display();
            if (_wcsicmp(h.Display().c_str(), wid.c_str()) == 0 ||
                _wcsicmp(h.address.c_str(), wid.c_str()) == 0 ||
                _wcsicmp(h.Target().c_str(), wid.c_str()) == 0 ||
                _wcsicmp(path.c_str(), wid.c_str()) == 0) {
                host = &h;
                break;
            }
        }
    }
    if (host) return SshTargetFromHost(inv, *host, t, err);

    // Envanterde olmayan user@host dogrudan ssh ile denenir
    if (id.find('@') != std::string::npos) return RawSshTarget(id, t, err);

    err = "Bilinmeyen sistem '" + id + "'. Gecerli kimlikler icin ft_systems kullanin "
          "(ya da envanter disi bir host icin 'ssh:kullanici@host').";
    return false;
}

// ------------------------------------------------------------------ araclar

ToolResult HandleFtSystems(const json::Value& args, AuditRec& a) {
    std::string filter = args["filter"].as_string();
    std::string typeFilter = args["type"].as_string();
    a.detail = "filter=" + filter + " type=" + typeFilter;

    K8sManager::Instance().Rescan(DataDir());
    Inventory inv;
    inv.Load(DataDir());
    auto nodes = inv.BuildNodeTree();

    filter = Lower(filter);
    typeFilter = Lower(typeFilter);

    struct Row { std::string id, type, status, path, target; };
    std::vector<Row> rows;
    for (const auto& n : nodes) {
        std::string typeStr;
        switch (n.type) {
            case NodeType::Local: typeStr = "local"; break;
            case NodeType::Wsl: typeStr = "wsl"; break;
            case NodeType::SshHost: typeStr = "ssh"; break;
            case NodeType::DockerContainer: typeStr = "docker"; break;
            case NodeType::K8sPod: typeStr = "k8s"; break;
            default: typeStr = "custom"; break;
        }

        if (!typeFilter.empty() && typeFilter != "all" && typeStr != typeFilter) {
            continue;
        }

        if (!filter.empty()) {
            if (Lower(n.id).find(filter) == std::string::npos &&
                Lower(n.name).find(filter) == std::string::npos &&
                Lower(n.path).find(filter) == std::string::npos) {
                continue;
            }
        }
        rows.push_back({ n.id, typeStr, n.status, n.path, n.target });
    }

    // Sutunlar icerige gore genisler: 24 karakterlik host kimlikleri TYPE sutununa yapismasin
    size_t idW = 22, statusW = 12, pathW = 32;
    for (const auto& r : rows) {
        idW = std::max(idW, r.id.size() + 2);
        statusW = std::max(statusW, r.status.size() + 2);
        pathW = std::max(pathW, r.path.size() + 2);
    }

    std::ostringstream oss;
    oss << std::left << std::setw(static_cast<int>(idW)) << "ID"
        << std::setw(10) << "TYPE"
        << std::setw(static_cast<int>(statusW)) << "STATUS"
        << std::setw(static_cast<int>(pathW)) << "PATH"
        << "TARGET\n";
    oss << std::string(idW + 10 + statusW + pathW + 6, '-') << "\n";
    for (const auto& r : rows) {
        oss << std::left << std::setw(static_cast<int>(idW)) << r.id
            << std::setw(10) << r.type
            << std::setw(static_cast<int>(statusW)) << r.status
            << std::setw(static_cast<int>(pathW)) << r.path
            << r.target << "\n";
    }
    if (rows.empty()) {
        oss << "(No matching systems found)\n";
    }
    return { oss.str(), false };
}

ToolResult HandleFtExec(const json::Value& args, const Settings& cfg, AuditRec& a) {
    const std::string system = args["system"].as_string("local");
    const std::string command = args["command"].as_string();
    int64_t timeout = args["timeout_ms"].as_int(15000);
    if (timeout <= 0) timeout = 15000;
    timeout = std::min<int64_t>(timeout, 600000);
    const int64_t maxLines = std::clamp<int64_t>(args["max_lines"].as_int(50), 1, 5000);
    a.system = system;
    a.detail = "cmd=" + command;

    if (command.empty()) {
        return Fail("Error: 'command' parameter is required.");
    }
    if (!ExecAllowed(cfg)) {
        return Deny(a, cfg.mcpReadOnly
            ? "ft_exec salt okunur modda kapali (Ayarlar > MCP > Salt okunur mod)."
            : "ft_exec izni kapali (Ayarlar > MCP > Arac izinleri > ft_exec).");
    }

    ExecTarget target;
    std::string err;
    if (!ResolveSystem(system, cfg, target, err)) return Fail("Error: " + err);
    if (target.kind == TargetKind::K8s && !cfg.mcpAllowK8s) {
        return Deny(a, "Kubernetes hedefleri kapali (Ayarlar > MCP > Arac izinleri > Kubernetes hedefleri).");
    }
    if (cfg.mcpApproval && !AskApproval("ft_exec", system, "Komut:\n" + command)) {
        return Deny(a, "Kullanici bu komutu onaylamadi.");
    }

    ExecResult res = AgentlessExecutor::Exec(target, command, static_cast<int>(timeout), static_cast<int>(maxLines));
    a.hasExit = true;
    a.exitCode = res.exitCode;
    if (res.exitCode != 0) {
        std::string prefix = "[Process exited with code " + std::to_string(res.exitCode) + "]\n";
        return { prefix + (res.output.empty() ? "(no output)" : res.output), true };
    }
    return { res.output.empty() ? "(Command completed with no output)" : res.output, false };
}

ToolResult HandleFtFs(const json::Value& args, const Settings& cfg, AuditRec& a) {
    const std::string system = args["system"].as_string("local");
    const std::string action = args["action"].as_string();
    const std::string path = args["path"].as_string();
    const std::string content = args["content"].as_string();
    const int offset = static_cast<int>(std::clamp<int64_t>(args["offset_lines"].as_int(0), 0, INT32_MAX));
    const int limit = static_cast<int>(std::clamp<int64_t>(args["limit_lines"].as_int(50), 1, 5000));
    a.action = action;
    a.system = system;
    a.detail = "path=" + path + (action == "write" ? " bytes=" + std::to_string(content.size()) : "");

    if (action.empty() || path.empty()) {
        return Fail("Error: 'action' and 'path' are required parameters.");
    }
    if (action != "list" && action != "read" && action != "write" && action != "info") {
        return Fail("Error: Unknown action '" + action + "'. Valid: list, read, write, info.");
    }
    if (!FilesAllowed(cfg)) {
        return Deny(a, "ft_fs izni kapali (Ayarlar > MCP > Arac izinleri > ft_fs).");
    }
    if (action == "write" && cfg.mcpReadOnly) {
        return Deny(a, "Dosya yazma salt okunur modda kapali (Ayarlar > MCP > Salt okunur mod).");
    }

    ExecTarget target;
    std::string err;
    if (!ResolveSystem(system, cfg, target, err)) return Fail("Error: " + err);
    if (target.kind == TargetKind::K8s && !cfg.mcpAllowK8s) {
        return Deny(a, "Kubernetes hedefleri kapali (Ayarlar > MCP > Arac izinleri > Kubernetes hedefleri).");
    }
    if (action == "write" && cfg.mcpApproval &&
        !AskApproval("ft_fs write", system,
                     "Dosya: " + path + "\nBoyut: " + std::to_string(content.size()) + " bayt\n\n"
                     "Icerik (bas kismi):\n" + Clip(content, 300))) {
        return Deny(a, "Kullanici bu dosya yazmasini onaylamadi.");
    }

    ExecResult res;
    if (action == "list") {
        res = AgentlessExecutor::ListFiles(target, path, limit);
    } else if (action == "read") {
        res = AgentlessExecutor::ReadFile(target, path, offset, limit);
    } else if (action == "write") {
        res = AgentlessExecutor::WriteFile(target, path, content);
    } else {
        res = AgentlessExecutor::GetFileInfo(target, path);
    }
    a.hasExit = true;
    a.exitCode = res.exitCode;

    // Basarisiz cagri bos ya da basarili gorunen metin olarak donmesin
    if (res.exitCode != 0) {
        if (res.output.empty()) {
            return Fail("Error: ft_fs " + action + " failed (exit code " + std::to_string(res.exitCode) + ", no output).");
        }
        if (StartsWith(res.output, "Error:")) return Fail(res.output);
        return Fail("[Process exited with code " + std::to_string(res.exitCode) + "]\n" + res.output);
    }
    if (StartsWith(res.output, "Error:")) return Fail(res.output);
    if (res.output.empty()) {
        return { action == "list" ? "(empty directory)" : "(no output)", false };
    }
    return { res.output, false };
}

ToolResult HandleFtVault(const json::Value& args, const Settings& cfg, AuditRec& a) {
    const std::string action = args["action"].as_string();
    a.action = action;

    Inventory inv;
    inv.Load(DataDir());

    if (action == "list") {
        std::ostringstream oss;
        oss << "=== FullTerminal Vault Hosts ===\n";
        for (const auto& h : inv.hosts()) {
            oss << "- ID: " << WideToUtf8(h.id)
                << " | " << WideToUtf8(h.Display())
                << " (" << WideToUtf8(h.Target()) << ":" << h.port << ")\n";
        }
        oss << "\n=== Keychain Identities ===\n";
        for (const auto& i : inv.identities()) {
            oss << "- ID: " << WideToUtf8(i.id)
                << " | " << WideToUtf8(i.name)
                << " (user: " << WideToUtf8(i.username) << ")\n";
        }
        return { oss.str(), false };
    }
    if (action != "add_host" && action != "remove_host") {
        return Fail("Error: Unknown vault action '" + action + "'. Valid: list, add_host, remove_host.");
    }
    if (cfg.mcpReadOnly) {
        return Deny(a, "Kasa degisikligi salt okunur modda kapali (Ayarlar > MCP > Salt okunur mod).");
    }

    if (action == "add_host") {
        const std::string label = Trim(args["label"].as_string());
        const std::string address = Trim(args["address"].as_string());
        const std::string username = Trim(args["username"].as_string());
        const int64_t port = args["port"].as_int(22);
        a.detail = "label=" + label + " address=" + address + " user=" + username + " port=" + std::to_string(port);

        if (address.empty()) return Fail("Error: 'address' is required for add_host.");
        // Kaydedilen adres sonradan ssh.exe argumani olur
        if (!SafeToken(address)) return Fail("Error: invalid address '" + address + "' (must not start with '-' or contain spaces/quotes).");
        if (!username.empty() && !SafeToken(username)) return Fail("Error: invalid username '" + username + "'.");
        if (port < 1 || port > 65535) return Fail("Error: port must be between 1 and 65535.");
        {
            // GUI ile ayni kural (kabuk karakterleri vb.): kasaya ssh'in reddedecegi host yazilmasin
            Host probe;
            probe.address = Utf8ToWide(address);
            probe.username = Utf8ToWide(username);
            std::wstring verr;
            if (!Inventory::ValidateSshFields(probe, &verr)) return Fail("Error: " + WideToUtf8(verr));
        }

        if (cfg.mcpApproval &&
            !AskApproval("ft_vault add_host", "-",
                         "Kasaya host eklenecek:\nEtiket: " + label + "\nAdres: " + address +
                         "\nKullanici: " + username + "\nPort: " + std::to_string(port))) {
            return Deny(a, "Kullanici host eklemeyi onaylamadi.");
        }
        Host& h = inv.AddHost();
        if (!label.empty()) h.label = Utf8ToWide(label);
        h.address = Utf8ToWide(address);
        h.port = static_cast<int>(port);
        h.username = Utf8ToWide(username);
        const std::string display = WideToUtf8(h.Display());
        const std::string newId = WideToUtf8(h.id);
        inv.Save(DataDir());
        return { "Successfully added host '" + display + "' with ID: " + newId, false };
    }

    const std::string id = Trim(args["id"].as_string());
    a.detail = "id=" + id;
    if (id.empty()) return Fail("Error: 'id' is required for remove_host.");
    const Host* h = inv.FindHost(Utf8ToWide(id));
    if (!h) return Fail("Error: no host with ID '" + id + "' in vault (see ft_vault list).");
    if (cfg.mcpApproval &&
        !AskApproval("ft_vault remove_host", "-",
                     "Kasadan host silinecek:\n" + WideToUtf8(h->Display()) + " (" + WideToUtf8(h->Target()) +
                     ")\nID: " + id)) {
        return Deny(a, "Kullanici host silmeyi onaylamadi.");
    }
    inv.RemoveHost(Utf8ToWide(id));
    inv.Save(DataDir());
    return { "Host with ID '" + id + "' removed from vault.", false };
}

// ------------------------------------------------------------- ft_pane_*

ToolResult HandleFtPaneList(const json::Value& args, AuditRec& a) {
    (void)args;
    a.action = "list";
    json::Value req = json::Value::Object();
    req["action"] = "list";
    json::Value resp;
    std::string err;
    if (!McpBridge::CallGui(req, resp, err)) {
        return Fail("Error: " + err);
    }
    if (!resp["success"].as_bool()) {
        return Fail("Error from GUI: " + resp["error"].as_string());
    }

    std::ostringstream oss;
    oss << "=== FullTerminal GUI Active Panes ===\n";
    const int64_t actTab = resp["active_tab"].as_int(0);
    const int64_t actPane = resp["active_pane"].as_int(0);
    oss << "Active Tab: " << actTab << " | Active Pane: " << actPane << "\n\n";

    const json::Value& tabs = resp["tabs"];
    if (tabs.is_array()) {
        for (const auto& t : tabs.arrVal) {
            const int64_t tid = t["tab_id"].as_int();
            const bool isAct = t["is_active"].as_bool();
            const std::string title = t["title"].as_string();
            oss << "[Tab " << tid << "] " << title << (isAct ? " (ACTIVE)" : "") << "\n";

            const json::Value& panes = t["panes"];
            if (panes.is_array()) {
                for (const auto& p : panes.arrVal) {
                    const int64_t pid = p["pane_id"].as_int();
                    const bool isFoc = p["is_focused"].as_bool();
                    const std::string pTitle = p["title"].as_string();
                    const std::string prof = p["profile"].as_string();
                    const bool alive = p["alive"].as_bool();
                    const auto& as = p["agent_status"];
                    const std::string state = as["state"].as_string();
                    const std::string kind = as["kind"].as_string();

                    oss << "  ├─ Pane #" << pid << (isFoc ? " [FOCUSED]" : "")
                        << " | Title: '" << pTitle << "' | Shell: " << prof
                        << " | Alive: " << (alive ? "yes" : "no") << "\n";
                    oss << "  │  Agent State: " << state;
                    if (kind != "unknown") oss << " (kind: " << kind << ")";
                    if (!as["rule_id"].as_string().empty()) oss << " [rule: " << as["rule_id"].as_string() << "]";
                    oss << "\n";
                }
            }
            oss << "\n";
        }
    }
    return { oss.str(), false };
}

ToolResult HandleFtPaneSplit(const json::Value& args, const Settings& cfg, AuditRec& a) {
    const std::string dir = args["direction"].as_string("vertical");
    const std::string system = args["system"].as_string();
    const std::string cmd = args["command"].as_string();
    const int64_t tabId = args["tab_id"].as_int(-1);

    a.action = "split";
    a.system = system;
    a.detail = "dir=" + dir + " cmd=" + cmd;

    if (!ExecAllowed(cfg)) {
        return Deny(a, cfg.mcpReadOnly
            ? "ft_pane_split salt okunur modda kapali (Ayarlar > MCP > Salt okunur mod)."
            : "ft_pane_split izni kapali (Ayarlar > MCP > Arac izinleri > ft_exec).");
    }

    if (cfg.mcpApproval && !AskApproval("ft_pane_split", system.empty() ? "local" : system,
                                        "Panel bolunecek (" + dir + "):\nKomut: " + cmd)) {
        return Deny(a, "Kullanici panel bolmeyi onaylamadi.");
    }

    json::Value req = json::Value::Object();
    req["action"] = "split";
    req["direction"] = dir;
    req["system"] = system;
    req["command"] = cmd;
    req["tab_id"] = tabId;

    json::Value resp;
    std::string err;
    if (!McpBridge::CallGui(req, resp, err)) {
        return Fail("Error: " + err);
    }
    if (!resp["success"].as_bool()) {
        return Fail("Error from GUI: " + resp["error"].as_string());
    }

    std::ostringstream oss;
    oss << "Successfully split terminal pane in FullTerminal GUI.\n";
    oss << "Tab ID: " << resp["tab_id"].as_int() << "\n";
    oss << "New Pane ID: " << resp["pane_id"].as_int() << "\n";
    oss << "Direction: " << resp["direction"].as_string() << "\n";
    oss << "System: " << resp["system"].as_string() << "\n";
    oss << "Title: " << resp["title"].as_string() << "\n";
    if (!cmd.empty()) {
        oss << "Initial command started: " << cmd << "\n";
    }
    return { oss.str(), false };
}

ToolResult HandleFtPanePrompt(const json::Value& args, const Settings& cfg, AuditRec& a) {
    const std::string input = args["input"].as_string();
    const int64_t paneId = args["pane_id"].as_int(0);
    const int64_t tabId = args["tab_id"].as_int(-1);
    const bool appendNewline = args.has("append_newline") ? args["append_newline"].as_bool(true) : true;

    a.action = "prompt";
    a.detail = "pane=" + std::to_string(paneId) + " input=" + input;

    if (input.empty()) {
        return Fail("Error: 'input' parameter is required.");
    }
    if (!ExecAllowed(cfg)) {
        return Deny(a, cfg.mcpReadOnly
            ? "ft_pane_prompt salt okunur modda kapali (Ayarlar > MCP > Salt okunur mod)."
            : "ft_pane_prompt izni kapali (Ayarlar > MCP > Arac izinleri > ft_exec).");
    }

    if (cfg.mcpApproval && !AskApproval("ft_pane_prompt", "Pane #" + std::to_string(paneId),
                                        "Pane #" + std::to_string(paneId) + " giris gonderilecek:\n" + input)) {
        return Deny(a, "Kullanici komut gondermeyi onaylamadi.");
    }

    json::Value req = json::Value::Object();
    req["action"] = "prompt";
    req["input"] = input;
    req["pane_id"] = paneId;
    req["tab_id"] = tabId;
    req["append_newline"] = appendNewline;

    json::Value resp;
    std::string err;
    if (!McpBridge::CallGui(req, resp, err)) {
        return Fail("Error: " + err);
    }
    if (!resp["success"].as_bool()) {
        return Fail("Error from GUI: " + resp["error"].as_string());
    }

    std::ostringstream oss;
    oss << "Successfully sent " << resp["bytes_written"].as_int() << " bytes to Tab "
        << resp["tab_id"].as_int() << " Pane #" << resp["pane_id"].as_int() << ".";
    return { oss.str(), false };
}

ToolResult HandleFtPaneRead(const json::Value& args, AuditRec& a) {
    const int64_t paneId = args["pane_id"].as_int(0);
    const int64_t tabId = args["tab_id"].as_int(-1);
    const int64_t tailLines = std::clamp<int64_t>(args["tail_lines"].as_int(25), 1, 500);

    a.action = "read";
    a.detail = "pane=" + std::to_string(paneId) + " lines=" + std::to_string(tailLines);

    json::Value req = json::Value::Object();
    req["action"] = "read";
    req["pane_id"] = paneId;
    req["tab_id"] = tabId;
    req["tail_lines"] = tailLines;

    json::Value resp;
    std::string err;
    if (!McpBridge::CallGui(req, resp, err)) {
        return Fail("Error: " + err);
    }
    if (!resp["success"].as_bool()) {
        return Fail("Error from GUI: " + resp["error"].as_string());
    }

    const auto& as = resp["agent_status"];
    const std::string state = as["state"].as_string();
    const std::string kind = as["kind"].as_string();
    const std::string ruleId = as["rule_id"].as_string();
    const std::string pat = as["matched_pattern"].as_string();

    std::ostringstream oss;
    oss << "=== Pane #" << resp["pane_id"].as_int() << " ('" << resp["title"].as_string() << "') ===\n";
    oss << "[Agent State: " << state;
    if (kind != "unknown") oss << " | Kind: " << kind;
    if (!ruleId.empty()) oss << " | Rule: " << ruleId;
    if (!pat.empty()) oss << " | Pattern: '" << pat << "'";
    oss << " | Alive: " << (resp["alive"].as_bool() ? "yes" : "no");
    if (resp["exited"].as_bool()) oss << " (exited with " << resp["exit_code"].as_int() << ")";
    oss << "]\n";
    oss << std::string(60, '-') << "\n";
    oss << resp["text"].as_string() << "\n";
    return { oss.str(), false };
}

ToolResult HandleFtPaneWait(const json::Value& args, AuditRec& a) {
    const int64_t paneId = args["pane_id"].as_int(0);
    const int64_t tabId = args["tab_id"].as_int(-1);
    std::string targetState = Lower(args["target_state"].as_string("idle"));
    int64_t timeoutMs = std::clamp<int64_t>(args["timeout_ms"].as_int(30000), 500, 300000);
    const int64_t pollMs = std::clamp<int64_t>(args["poll_interval_ms"].as_int(250), 50, 5000);

    a.action = "wait";
    a.detail = "pane=" + std::to_string(paneId) + " target=" + targetState + " timeout=" + std::to_string(timeoutMs);

    json::Value readReq = json::Value::Object();
    readReq["action"] = "read";
    readReq["pane_id"] = paneId;
    readReq["tab_id"] = tabId;
    readReq["tail_lines"] = 25;

    const auto startTime = std::chrono::steady_clock::now();
    json::Value lastResp;
    std::string lastState = "unknown";

    while (true) {
        json::Value resp;
        std::string err;
        if (!McpBridge::CallGui(readReq, resp, err)) {
            return Fail("Error during pane wait: " + err);
        }
        if (!resp["success"].as_bool()) {
            return Fail("Error from GUI during pane wait: " + resp["error"].as_string());
        }

        lastResp = resp;
        const auto& as = resp["agent_status"];
        lastState = Lower(as["state"].as_string());
        const bool alive = resp["alive"].as_bool();
        const bool exited = resp["exited"].as_bool();

        bool conditionMet = false;
        if (targetState == "blocked" && lastState == "blocked") {
            conditionMet = true;
        } else if (targetState == "idle" && (lastState == "idle" || exited || !alive)) {
            conditionMet = true;
        } else if (targetState == "working" && lastState == "working") {
            conditionMet = true;
        } else if (targetState == "exited" && (exited || !alive)) {
            conditionMet = true;
        } else if (targetState == "any_agent" && lastState != "unknown") {
            conditionMet = true;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();

        if (conditionMet) {
            std::ostringstream oss;
            oss << "Condition satisfied: Pane #" << resp["pane_id"].as_int()
                << " reached state '" << lastState << "' in " << elapsed << " ms.\n";
            oss << "Agent Kind: " << as["kind"].as_string();
            if (!as["rule_id"].as_string().empty()) oss << " | Rule: " << as["rule_id"].as_string();
            if (!as["matched_pattern"].as_string().empty()) oss << " | Pattern: '" << as["matched_pattern"].as_string() << "'";
            oss << "\n\n=== Latest Screen Output ===\n" << resp["text"].as_string() << "\n";
            return { oss.str(), false };
        }

        if (elapsed >= timeoutMs) {
            std::ostringstream oss;
            oss << "Timeout (" << timeoutMs << " ms) reached while waiting for Pane #"
                << (paneId <= 0 ? 0 : paneId) << " to enter state '" << targetState << "'.\n";
            oss << "Current state: " << lastState << " (alive: " << (alive ? "yes" : "no") << ")\n\n";
            oss << "=== Latest Screen Output ===\n" << lastResp["text"].as_string() << "\n";
            return { oss.str(), true };
        }

        Sleep(static_cast<DWORD>(pollMs));
    }
}

ToolResult HandleFtSwarmSpawn(const json::Value& args, const Settings& cfg, AuditRec& a) {
    std::string preset = "pair_programming";
    if (args.has("preset") && args["preset"].is_string()) {
        preset = args["preset"].as_string();
    }

    a.action = "swarm";
    a.system = "local";
    a.detail = "preset=" + preset;

    if (!ExecAllowed(cfg)) {
        return Deny(a, cfg.mcpReadOnly
            ? "ft_swarm_spawn salt okunur modda kapali (Ayarlar > MCP > Salt okunur mod)."
            : "ft_swarm_spawn izni kapali (Ayarlar > MCP > Arac izinleri > ft_exec).");
    }

    if (cfg.mcpApproval && !AskApproval("ft_swarm_spawn", preset, "Spawn Multi-Agent Swarm Workspace Layout")) {
        return Deny(a, "Kullanici swarm baslatmayi onaylamadi.");
    }

    json::Value ipcReq = json::Value::Object();
    ipcReq["action"] = "swarm";
    ipcReq["preset"] = preset;
    if (args.has("cwd") && args["cwd"].is_string()) {
        ipcReq["cwd"] = args["cwd"].as_string();
    }
    if (args.has("commands") && args["commands"].is_array()) {
        ipcReq["commands"] = args["commands"];
    }

    json::Value ipcResp;
    std::string ipcErr;
    if (!McpBridge::CallGui(ipcReq, ipcResp, ipcErr)) {
        return Fail("Failed to communicate with FullTerminal GUI window: " + ipcErr);
    }

    if (!ipcResp["ok"].as_bool() && ipcResp.has("error") && ipcResp["error"].is_string()) {
        return Fail(ipcResp["error"].as_string());
    }

    std::ostringstream oss;
    oss << "Successfully spawned Swarm Workspace '" << preset << "'.\n";
    if (ipcResp.has("paneCount")) {
        oss << "Panes created: " << ipcResp["paneCount"].as_int() << "\n";
    }
    if (ipcResp.has("tabIndex")) {
        oss << "Tab index: " << ipcResp["tabIndex"].as_int() << "\n";
    }
    return { oss.str(), false };
}

ToolResult HandleFtSessionRecord(const json::Value& args, AuditRec& a) {
    a.action = "ft_session_record";
    a.system = "local";
    std::string act = args.has("action") ? args["action"].as_string() : "status";
    a.detail = "action=" + act;

    json::Value ipcReq = json::Value::Object();
    ipcReq["action"] = "record";
    ipcReq["sub_action"] = act;
    if (args.has("file_path")) ipcReq["file_path"] = args["file_path"].as_string();

    json::Value ipcResp;
    std::string ipcErr;
    if (!McpBridge::CallGui(ipcReq, ipcResp, ipcErr)) {
        return Fail("Failed to communicate with FullTerminal GUI window: " + ipcErr);
    }
    if (!ipcResp["success"].as_bool(false)) {
        return Fail(ipcResp.has("error") ? ipcResp["error"].as_string() : "Session recording failed.");
    }
    return { ipcResp.dump(), false };
}

// ------------------------------------------------------------- JSON-RPC

json::Value ErrorResponse(const json::Value& id, int code, const std::string& message) {
    json::Value resp = json::Value::Object();
    resp["jsonrpc"] = "2.0";
    resp["id"] = id; // bilinmiyorsa null: JSON-RPC hata yanitinda id zorunlu
    json::Value err = json::Value::Object();
    err["code"] = code;
    err["message"] = message;
    resp["error"] = err;
    return resp;
}

json::Value ToolsCall(const json::Value& params) {
    const Settings cfg = LoadSettings();
    const std::string toolName = params["name"].as_string();
    const json::Value& rawArgs = params["arguments"];
    const json::Value toolArgs = rawArgs.is_object() ? rawArgs : json::Value::Object();

    ToolResult r;
    AuditRec a;
    if (!ServerEnabled(cfg)) {
        a.denied = true;
        r = Fail(kDisabledMsg);
    } else if (toolName == "ft_systems") {
        r = HandleFtSystems(toolArgs, a);
    } else if (toolName == "ft_exec") {
        r = HandleFtExec(toolArgs, cfg, a);
    } else if (toolName == "ft_fs") {
        r = HandleFtFs(toolArgs, cfg, a);
    } else if (toolName == "ft_vault") {
        r = HandleFtVault(toolArgs, cfg, a);
    } else if (toolName == "ft_pane_list") {
        r = HandleFtPaneList(toolArgs, a);
    } else if (toolName == "ft_pane_split") {
        r = HandleFtPaneSplit(toolArgs, cfg, a);
    } else if (toolName == "ft_pane_prompt") {
        r = HandleFtPanePrompt(toolArgs, cfg, a);
    } else if (toolName == "ft_pane_read") {
        r = HandleFtPaneRead(toolArgs, a);
    } else if (toolName == "ft_pane_wait") {
        r = HandleFtPaneWait(toolArgs, a);
    } else if (toolName == "ft_swarm_spawn") {
        r = HandleFtSwarmSpawn(toolArgs, cfg, a);
    } else if (toolName == "ft_session_record") {
        r = HandleFtSessionRecord(toolArgs, a);
    } else if (toolName == "ft_terminal") {
        if (toolArgs.has("input") || toolArgs.has("command")) {
            r = HandleFtPanePrompt(toolArgs, cfg, a);
        } else {
            r = HandleFtPaneList(toolArgs, a);
        }
    } else {
        r = Fail("Error: Unknown tool '" + toolName + "'.");
    }
    WriteAudit(cfg, toolName.empty() ? "-" : toolName, a);

    json::Value result = json::Value::Object();
    json::Value contentArr = json::Value::Array();
    json::Value textItem = json::Value::Object();
    textItem["type"] = "text";
    textItem["text"] = r.text;
    contentArr.push_back(textItem);
    result["content"] = contentArr;
    result["isError"] = r.isError;
    return result;
}

// Tek bir istegi isler. Yanit gerekmiyorsa (bildirim, istemci yaniti) false doner.
bool HandleRequest(const json::Value& req, json::Value& resp) {
    if (!req.is_object()) {
        resp = ErrorResponse(json::Value(), -32600, "Invalid Request");
        return true;
    }
    const bool hasId = req.has("id");
    const json::Value& id = req["id"];
    if (!req.has("method")) {
        // Istemcinin bize yolladigi yanit (sunucu istek gondermiyor): yok say
        if (req.has("result") || req.has("error")) return false;
        resp = ErrorResponse(hasId ? id : json::Value(), -32600, "Invalid Request");
        return true;
    }
    if (hasId && !(id.is_string() || id.is_number() || id.is_null())) {
        resp = ErrorResponse(json::Value(), -32600, "Invalid Request: bad id");
        return true;
    }
    if (!req["method"].is_string()) {
        if (!hasId) return false;
        resp = ErrorResponse(id, -32600, "Invalid Request: method must be a string");
        return true;
    }
    // id'siz her mesaj bildirimdir (notifications/initialized, notifications/cancelled ...):
    // JSON-RPC 2.0 bildirimlere yanit verilmesini yasaklar.
    if (!hasId) return false;

    const std::string method = req["method"].as_string();
    try {
        json::Value result;
        if (method == "initialize") {
            result = json::Value::Object();
            result["protocolVersion"] = "2024-11-05";
            json::Value caps = json::Value::Object();
            caps["tools"] = json::Value::Object();
            result["capabilities"] = caps;

            json::Value sInfo = json::Value::Object();
            sInfo["name"] = "FullTerminal";
            sInfo["version"] = "1.0.0";
            result["serverInfo"] = sInfo;
        } else if (method == "ping") {
            result = json::Value::Object();
        } else if (method == "tools/list") {
            result = json::Value::Object();
            result["tools"] = BuildToolsList(LoadSettings());
        } else if (method == "tools/call") {
            const json::Value& params = req["params"];
            if (!params.is_object() || !params["name"].is_string() ||
                !(params["arguments"].is_null() || params["arguments"].is_object())) {
                resp = ErrorResponse(id, -32602, "Invalid params: expected {name: string, arguments?: object}");
                return true;
            }
            result = ToolsCall(params);
        } else {
            resp = ErrorResponse(id, -32601, "Method not found: " + method);
            return true;
        }
        resp = json::Value::Object();
        resp["jsonrpc"] = "2.0";
        resp["id"] = id;
        resp["result"] = result;
    } catch (const std::exception& e) {
        resp = ErrorResponse(id, -32603, std::string("Internal error: ") + e.what());
    } catch (...) {
        resp = ErrorResponse(id, -32603, "Internal error");
    }
    return true;
}

} // namespace

std::string McpServer::ProcessMessage(const std::string& line) {
    if (line.empty()) return "";

    bool ok = false;
    const json::Value msg = json::Value::parse(line, &ok);
    if (!ok) {
        // Bozuk/yarim satir asla kismen calistirilmaz
        return ErrorResponse(json::Value(), -32700, "Parse error").dump();
    }

    if (msg.is_array()) {
        if (msg.size() == 0) return ErrorResponse(json::Value(), -32600, "Invalid Request: empty batch").dump();
        json::Value out = json::Value::Array();
        for (const auto& item : msg.arrVal) {
            json::Value r;
            if (HandleRequest(item, r)) out.push_back(r);
        }
        return out.size() == 0 ? "" : out.dump();
    }

    json::Value resp;
    return HandleRequest(msg, resp) ? resp.dump() : "";
}

int McpServer::RunStdio() {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    g_dataDir = PortableDataDir();
    AgentlessExecutor::SetDataDir(g_dataDir);
    // Init degil Rescan: basliksiz (ve belki kapali) sunucu ornek YAML yazmasin
    K8sManager::Instance().Rescan(g_dataDir);

    std::string line;
    while (std::getline(std::cin, line)) {
        // Strip trailing \r if present
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        // Bazi istemciler (.NET StreamWriter, PowerShell) akisin basina UTF-8 BOM
        // koyar; ilk istek (initialize) parse hatasina dusmesin.
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
            line.erase(0, 3);
        }
        if (line.empty()) continue;

        std::string response;
        try {
            response = ProcessMessage(line);
        } catch (...) {
            // Son savunma hatti: tek bir mesaj (orn. bad_alloc) stdio oturumunu bitirmesin
            response = "{\"error\":{\"code\":-32603,\"message\":\"Internal error\"},\"id\":null,\"jsonrpc\":\"2.0\"}";
        }
        if (!response.empty()) {
            std::cout << response << "\n";
            std::cout.flush();
        }
    }

    return 0;
}

} // namespace ft
