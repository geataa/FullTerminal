#include "model/TunnelModel.h"
#include "core/Utf8.h"

#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace ft {

namespace {

std::wstring Trim(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}

std::wstring GenerateId() {
    uint64_t tick = GetTickCount64();
    static uint32_t seq = 0;
    wchar_t buf[64];
    swprintf_s(buf, L"tun_%llx_%x", tick, ++seq);
    return buf;
}

} // namespace

TunnelModel::TunnelModel(Inventory& inv) : m_inv(inv) {}

TunnelModel::~TunnelModel() {
    StopAll();
}

void TunnelModel::Load(const std::wstring& dataDir) {
    m_dataDir = dataDir;
    m_rules.clear();

    std::wstring path = dataDir + L"\\tunnels.ini";
    std::ifstream ifs(path);
    if (!ifs.is_open()) return;

    std::string line;
    TunnelRule cur;
    bool inRule = false;

    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::wstring wline = Utf8ToWide(line);
        wline = Trim(wline);
        if (wline.empty() || wline[0] == L'#') continue;

        if (wline == L"[tunnel]") {
            if (inRule && !cur.name.empty()) {
                m_rules.push_back(cur);
            }
            cur = TunnelRule();
            inRule = true;
            continue;
        }

        size_t eq = wline.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = Trim(wline.substr(0, eq));
        std::wstring v = Trim(wline.substr(eq + 1));

        if (k == L"id") cur.id = v;
        else if (k == L"name") cur.name = v;
        else if (k == L"host_id") cur.hostId = v;
        else if (k == L"host_display") cur.hostDisplay = v;
        else if (k == L"type") cur.type = (TunnelType)_wtoi(v.c_str());
        else if (k == L"local_port") cur.localPort = _wtoi(v.c_str());
        else if (k == L"remote_host") cur.remoteHost = v;
        else if (k == L"remote_port") cur.remotePort = _wtoi(v.c_str());
        else if (k == L"autostart") cur.autoStart = (_wtoi(v.c_str()) != 0);
    }

    if (inRule && !cur.name.empty()) {
        m_rules.push_back(cur);
    }
}

void TunnelModel::Save(const std::wstring& dataDir) const {
    std::wstring path = dataDir + L"\\tunnels.ini";
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.is_open()) return;

    ofs << "# FullTerminal Port Yonlendirme Tünelleri\n";
    for (const auto& r : m_rules) {
        ofs << "\n[tunnel]\n";
        ofs << "id=" << WideToUtf8(r.id) << "\n";
        ofs << "name=" << WideToUtf8(r.name) << "\n";
        ofs << "host_id=" << WideToUtf8(r.hostId) << "\n";
        ofs << "host_display=" << WideToUtf8(r.hostDisplay) << "\n";
        ofs << "type=" << (int)r.type << "\n";
        ofs << "local_port=" << r.localPort << "\n";
        ofs << "remote_host=" << WideToUtf8(r.remoteHost) << "\n";
        ofs << "remote_port=" << r.remotePort << "\n";
        ofs << "autostart=" << (r.autoStart ? 1 : 0) << "\n";
    }
}

bool TunnelModel::AddRule(const std::wstring& name, const std::wstring& hostId,
                         TunnelType type, int localPort, const std::wstring& remoteHost, int remotePort) {
    if (name.empty() || hostId.empty()) return false;
    TunnelRule r;
    r.id = GenerateId();
    r.name = name;
    r.hostId = hostId;
    if (Host* h = m_inv.FindHost(hostId)) {
        r.hostDisplay = h->Display();
    } else {
        r.hostDisplay = hostId;
    }
    r.type = type;
    r.localPort = localPort > 0 ? localPort : 8080;
    r.remoteHost = remoteHost.empty() ? L"localhost" : remoteHost;
    r.remotePort = remotePort > 0 ? remotePort : 80;
    m_rules.push_back(r);

    if (!m_dataDir.empty()) Save(m_dataDir);
    return true;
}

bool TunnelModel::DeleteRule(const std::wstring& id) {
    StopTunnel(id);
    auto it = std::find_if(m_rules.begin(), m_rules.end(), [&](const TunnelRule& r) {
        return r.id == id;
    });
    if (it != m_rules.end()) {
        m_rules.erase(it);
        if (!m_dataDir.empty()) Save(m_dataDir);
        return true;
    }
    return false;
}

std::wstring TunnelModel::BuildTunnelCommand(const TunnelRule& rule, const Host& host, std::wstring* err) const {
    const std::wstring ssh = FindSshExe();
    if (ssh.empty()) {
        if (err) *err = L"ssh.exe bulunamadi.";
        return {};
    }

    std::wstring cmd = L"\"" + ssh + L"\" -N"; // -N: Uzak kabuk calistirma, yalniz port ilet

    if (rule.type == TunnelType::Local) {
        // -L [local_ip:]local_port:remote_host:remote_port
        cmd += L" -L " + std::to_wstring(rule.localPort) + L":" + rule.remoteHost + L":" + std::to_wstring(rule.remotePort);
    } else if (rule.type == TunnelType::Remote) {
        // -R [remote_ip:]remote_port:local_host:local_port
        cmd += L" -R " + std::to_wstring(rule.remotePort) + L":" + rule.remoteHost + L":" + std::to_wstring(rule.localPort);
    } else if (rule.type == TunnelType::Dynamic) {
        // -D [local_ip:]local_port (SOCKS5 Proxy)
        cmd += L" -D " + std::to_wstring(rule.localPort);
    }

    if (host.port > 0 && host.port != 22) {
        cmd += L" -p " + std::to_wstring(host.port);
    }

    std::wstring user = Trim(host.username);
    std::wstring key = host.keyPath;
    std::wstring cert = host.certPath;

    if (!host.identityId.empty()) {
        for (const auto& id : m_inv.identities()) {
            if (id.id == host.identityId) {
                if (user.empty()) user = Trim(id.username);
                if (key.empty()) key = id.keyPath;
                if (cert.empty()) cert = id.certPath;
                break;
            }
        }
    }

    if (!key.empty()) {
        key = m_inv.ResolveKeyPath(key, host.id.empty() ? host.Display() : host.id);
        cmd += L" -i \"" + key + L"\"";
    }
    if (!cert.empty()) {
        cert = m_inv.ResolveCertPath(cert, host.id.empty() ? host.Display() : host.id);
        cmd += L" -o \"CertificateFile=" + cert + L"\"";
    }

    cmd += L" -o ServerAliveInterval=15";
    cmd += L" -o ExitOnForwardFailure=yes";
    cmd += L" -o StrictHostKeyChecking=accept-new";

    std::wstring target = host.address;
    if (!user.empty()) target = user + L"@" + target;
    cmd += L" \"" + target + L"\"";

    return cmd;
}

bool TunnelModel::StartTunnel(const std::wstring& id, std::wstring* err) {
    auto it = std::find_if(m_rules.begin(), m_rules.end(), [&](const TunnelRule& r) {
        return r.id == id;
    });
    if (it == m_rules.end()) {
        if (err) *err = L"Kural bulunamadi.";
        return false;
    }

    if (it->running && it->hProcess) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(it->hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
            return true; // zaten calisiyor
        }
    }

    Host* host = m_inv.FindHost(it->hostId);
    if (!host) {
        if (err) *err = L"Tunel icin bagli host envanterde bulunamadi: " + it->hostDisplay;
        return false;
    }

    std::wstring cmd = BuildTunnelCommand(*it, *host, err);
    if (cmd.empty()) return false;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr, cmdBuf.data(),
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr,
        &si, &pi);

    if (!ok) {
        DWORD gle = GetLastError();
        if (err) *err = L"Tunel sureci baslatilamadi (kod " + std::to_wstring(gle) + L")";
        return false;
    }

    CloseHandle(pi.hThread);
    it->hProcess = pi.hProcess;
    it->pid = pi.dwProcessId;
    it->running = true;
    it->lastError.clear();

    return true;
}

bool TunnelModel::StopTunnel(const std::wstring& id) {
    auto it = std::find_if(m_rules.begin(), m_rules.end(), [&](const TunnelRule& r) {
        return r.id == id;
    });
    if (it == m_rules.end()) return false;

    if (it->hProcess) {
        TerminateProcess(it->hProcess, 0);
        CloseHandle(it->hProcess);
        it->hProcess = nullptr;
    }
    it->running = false;
    it->pid = 0;
    return true;
}

void TunnelModel::StopAll() {
    for (auto& r : m_rules) {
        if (r.hProcess) {
            TerminateProcess(r.hProcess, 0);
            CloseHandle(r.hProcess);
            r.hProcess = nullptr;
        }
        r.running = false;
        r.pid = 0;
    }
}

void TunnelModel::CheckStatus() {
    for (auto& r : m_rules) {
        if (r.running && r.hProcess) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(r.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                CloseHandle(r.hProcess);
                r.hProcess = nullptr;
                r.running = false;
                r.pid = 0;
                r.lastError = L"Tünel kapandı (kod " + std::to_wstring(exitCode) + L")";
            }
        }
    }
}

} // namespace ft
