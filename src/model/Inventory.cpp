#include "model/Inventory.h"
#include "model/K8sManager.h"
#include "core/Utf8.h"
#include "core/ShellProfiles.h"
#include "services/KnownHostsService.h"
#include "transport/agentless/AgentlessExecutor.h"

#include <windows.h>
#include <wincrypt.h>
#include <shlwapi.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <sstream>

#pragma comment(lib, "crypt32.lib")

namespace ft {
namespace {

std::wstring EnvVar(const wchar_t* n) {
    wchar_t buf[1024];
    DWORD k = GetEnvironmentVariableW(n, buf, (DWORD)std::size(buf));
    return (k && k < std::size(buf)) ? std::wstring(buf, k) : std::wstring();
}

// ---- DPAPI: parolalar diske hicbir zaman duz yazilmaz --------------------

std::string ProtectToBase64(const std::wstring& plain) {
    if (plain.empty()) return {};
    DATA_BLOB in{ (DWORD)((plain.size() + 1) * sizeof(wchar_t)),
                  (BYTE*)plain.c_str() };
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"FullTerminal", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    DWORD n = 0;
    CryptBinaryToStringA(out.pbData, out.cbData,
                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
    std::string b64(n, '\0');
    CryptBinaryToStringA(out.pbData, out.cbData,
                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &n);
    b64.resize(n);
    LocalFree(out.pbData);
    while (!b64.empty() && (b64.back() == '\0' || b64.back() == '\n' || b64.back() == '\r')) b64.pop_back();
    return b64;
}

// Basarisizlik (baska kullanici/PC'nin DPAPI anahtari) bos parolayla karismasin diye bool doner.
bool UnprotectFromBase64(const std::string& b64, std::wstring& plain) {
    plain.clear();
    if (b64.empty()) return true;
    DWORD bin = 0;
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64,
                              nullptr, &bin, nullptr, nullptr)) return false;
    std::vector<BYTE> buf(bin);
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64,
                              buf.data(), &bin, nullptr, nullptr)) return false;
    DATA_BLOB in{ bin, buf.data() };
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    plain.assign((const wchar_t*)out.pbData, wcsnlen((const wchar_t*)out.pbData, out.cbData / sizeof(wchar_t)));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
}

// Diske yazilacak "secret=" degeri. Bos donerse satir hic yazilmaz.
std::string SecretForSave(const std::wstring& password, const std::string& blob, bool locked) {
    if (!password.empty()) {
        std::string b = ProtectToBase64(password);
        // CryptProtectData basarisizsa bos "secret=" yazip eskisini de kaybetme
        return b.empty() ? blob : b;
    }
    // Cozulemeyen parola: kullanici yenisini girmedikce sifreli blob aynen korunur
    return locked ? blob : std::string();
}

bool HasCtlChar(const std::wstring& s) {
    for (wchar_t c : s) if (c < 0x20 || c == 0x7F) return true;
    return false;
}

// ssh hedefi/atlama sunucusu: secenek gibi okunamasin ve tek arguman olarak kalsin.
// Kabuk karakterleri OpenSSH 9.6'nin valid_hostname kuraliyla ayni: eski ssh.exe
// surumleri %h'yi ssh_config'teki ProxyCommand'a kacislamadan koyar (CVE-2023-51385).
bool SafeSshHost(const std::wstring& s) {
    if (s.empty() || s[0] == L'-') return false;
    for (wchar_t c : s) {
        if (c <= 0x20 || c == 0x7F || iswspace(c) || wcschr(L"'`\"$\\;&<>|(){}", c)) return false;
    }
    return true;
}

// Kullanici adi -l ile tirnakli gider; Windows hesaplarindaki bosluk ve user@domain serbest.
// Geri kalani OpenSSH valid_ruser (%r genislemesi icin).
bool SafeSshUser(const std::wstring& s) {
    if (s.empty() || s[0] == L'-' || HasCtlChar(s) || s.back() == L'\\') return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (wcschr(L"'`\";&<>|(){}", s[i])) return false;
        if (iswspace(s[i]) && i + 1 < s.size() && s[i + 1] == L'-') return false;
    }
    return true;
}

std::wstring TrimWs(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return {};
    const size_t b = s.find_last_not_of(L" \t");
    return s.substr(a, b - a + 1);
}

// ---- kucuk INI yardimcilari ----------------------------------------------

std::wstring Escape(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size());
    for (wchar_t c : s) {
        if (c == L'\n') o += L"\\n";
        else if (c == L'\r') continue;
        else if (c == L'\\') o += L"\\\\";
        else o.push_back(c);
    }
    return o;
}

std::wstring Unescape(const std::wstring& s) {
    std::wstring o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            ++i;
            o.push_back(s[i] == L'n' ? L'\n' : s[i]);
        } else {
            o.push_back(s[i]);
        }
    }
    return o;
}

void WriteFileUtf8(const std::wstring& path, const std::wstring& content) {
    const std::string utf8 = WideToUtf8(content);
    // Once gecici dosyaya yaz, sonra yerine tasi: yarida kesilen yazma
    // hosts.ini'yi (ve icindeki sifreli parolalari) kesik birakmasin.
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    const BOOL ok = WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != (DWORD)utf8.size() ||
        !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
    }
}

std::wstring ReadFileUtf8(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    std::string data((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    ReadFile(h, data.data(), (DWORD)data.size(), &got, nullptr);
    CloseHandle(h);
    data.resize(got);
    return Utf8ToWide(data);
}

} // namespace

std::wstring Inventory::NewId() {
    GUID g{};
    CoCreateGuid(&g);
    wchar_t buf[40];
    swprintf_s(buf, L"%08x%04x%04x%02x%02x%02x%02x",
               g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3]);
    return buf;
}

std::wstring FindSshExe() {
    const std::wstring sys = EnvVar(L"SystemRoot");
    const std::wstring cands[] = {
        sys + L"\\System32\\OpenSSH\\ssh.exe",
        sys + L"\\Sysnative\\OpenSSH\\ssh.exe",
        EnvVar(L"ProgramFiles") + L"\\Git\\usr\\bin\\ssh.exe",
    };
    for (const auto& c : cands) {
        DWORD a = GetFileAttributesW(c.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) return c;
    }
    wchar_t found[MAX_PATH] = L"ssh.exe";
    if (PathFindOnPathW(found, nullptr)) return found;
    return {};
}

std::wstring FindSftpExe() {
    const std::wstring sys = EnvVar(L"SystemRoot");
    const std::wstring cands[] = {
        sys + L"\\System32\\OpenSSH\\sftp.exe",
        sys + L"\\Sysnative\\OpenSSH\\sftp.exe",
        EnvVar(L"ProgramFiles") + L"\\Git\\usr\\bin\\sftp.exe",
    };
    for (const auto& c : cands) {
        DWORD a = GetFileAttributesW(c.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) return c;
    }
    wchar_t found[MAX_PATH] = L"sftp.exe";
    if (PathFindOnPathW(found, nullptr)) return found;
    return {};
}

Host* Inventory::FindHost(const std::wstring& id) {
    for (auto& h : m_hosts) if (h.id == id) return &h;
    return nullptr;
}

Identity* Inventory::FindIdentity(const std::wstring& id) {
    for (auto& i : m_identities) if (i.id == id) return &i;
    return nullptr;
}

Host& Inventory::AddHost() {
    Host h;
    h.id = NewId();
    h.label = L"Yeni host";
    h.port = 22;
    m_hosts.push_back(std::move(h));
    return m_hosts.back();
}

Identity& Inventory::AddIdentity() {
    Identity i;
    i.id = NewId();
    i.name = L"Yeni kimlik";
    m_identities.push_back(std::move(i));
    return m_identities.back();
}

void Inventory::RemoveHost(const std::wstring& id) {
    m_hosts.erase(std::remove_if(m_hosts.begin(), m_hosts.end(),
                                 [&](const Host& h) { return h.id == id; }),
                  m_hosts.end());
}

void Inventory::RemoveIdentity(const std::wstring& id) {
    m_identities.erase(std::remove_if(m_identities.begin(), m_identities.end(),
                                      [&](const Identity& i) { return i.id == id; }),
                       m_identities.end());
}

std::vector<std::wstring> Inventory::Groups() const {
    std::vector<std::wstring> g;
    for (const auto& h : m_hosts) {
        if (h.group.empty()) continue;
        if (std::find(g.begin(), g.end(), h.group) == g.end()) g.push_back(h.group);
    }
    std::sort(g.begin(), g.end());
    return g;
}

bool Inventory::ValidateSshFields(const Host& h, std::wstring* err) {
    const std::wstring address = TrimWs(h.address);
    const std::wstring user = TrimWs(h.username);
    const std::wstring jump = TrimWs(h.jumpHost);
    if (address.empty()) {
        if (err) *err = L"Host adresi bos.";
        return false;
    }
    if (!SafeSshHost(address)) {
        if (err) *err = L"Gecersiz host adresi: '-' ile baslayamaz, bosluk, tirnak, kontrol ya da kabuk karakteri ($ ; & | < > ( ) { } ` \\) iceremez.";
        return false;
    }
    if (!user.empty() && !SafeSshUser(user)) {
        if (err) *err = L"Gecersiz kullanici adi: '-' ile baslayamaz, tirnak, kontrol ya da kabuk karakteri (; & | < > ( ) { } `) iceremez.";
        return false;
    }
    if (!jump.empty() && !SafeSshHost(jump)) {
        if (err) *err = L"Gecersiz atlama sunucusu (jump host): '-' ile baslayamaz, bosluk, tirnak ya da kabuk karakteri iceremez.";
        return false;
    }
    return true;
}

std::wstring Inventory::ResolveKeyPath(const std::wstring& keyOrContent, const std::wstring& idOrName) const {
    if (keyOrContent.empty()) return {};
    // Eger raw PEM/OpenSSH private key iceriyorsa diske key dosyasi olarak yaz
    if (keyOrContent.find(L"-----BEGIN") != std::wstring::npos || keyOrContent.find(L'\n') != std::wstring::npos) {
        std::wstring dir = m_dataDir.empty() ? L"keys" : (m_dataDir + L"\\keys");
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring safeId = idOrName.empty() ? L"temp" : idOrName;
        for (wchar_t& c : safeId) {
            if (c == L' ' || c == L':' || c == L'/' || c == L'\\') c = L'_';
        }
        std::wstring keyFile = dir + L"\\key_" + safeId + L".pem";
        std::string utf8Content = WideToUtf8(keyOrContent);
        if (!utf8Content.empty() && utf8Content.back() != '\n') utf8Content += "\n";
        HANDLE hFile = CreateFileW(keyFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hFile, utf8Content.data(), (DWORD)utf8Content.size(), &written, nullptr);
            CloseHandle(hFile);
            return keyFile;
        }
    }
    return keyOrContent;
}

std::wstring Inventory::ResolveCertPath(const std::wstring& certOrContent, const std::wstring& idOrName) const {
    if (certOrContent.empty()) return {};
    // Eger raw sertifika metni iceriyorsa diske cert dosyasi olarak yaz
    if (certOrContent.find(L"-----BEGIN") != std::wstring::npos ||
        certOrContent.find(L'\n') != std::wstring::npos ||
        certOrContent.find(L"ssh-") != std::wstring::npos ||
        certOrContent.find(L"ecdsa-") != std::wstring::npos) {
        std::wstring dir = m_dataDir.empty() ? L"keys" : (m_dataDir + L"\\keys");
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring safeId = idOrName.empty() ? L"temp" : idOrName;
        for (wchar_t& c : safeId) {
            if (c == L' ' || c == L':' || c == L'/' || c == L'\\') c = L'_';
        }
        std::wstring certFile = dir + L"\\cert_" + safeId + L".pub";
        std::string utf8Content = WideToUtf8(certOrContent);
        if (!utf8Content.empty() && utf8Content.back() != '\n') utf8Content += "\n";
        HANDLE hFile = CreateFileW(certFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hFile, utf8Content.data(), (DWORD)utf8Content.size(), &written, nullptr);
            CloseHandle(hFile);
            return certFile;
        }
    }
    return certOrContent;
}

std::wstring Inventory::BuildSshCommand(const Host& h, std::wstring* err) const {
    const std::wstring ssh = FindSshExe();
    if (ssh.empty()) {
        if (err) *err = L"Windows OpenSSH istemcisi (ssh.exe) bulunamadi.";
        return {};
    }
    // Adres/kullanici/jump ssh.exe'ye secenek olarak sizamasin (orn. -oProxyCommand=...)
    if (!ValidateSshFields(h, err)) return {};

    std::wstring user = TrimWs(h.username);
    std::wstring key = h.keyPath;
    std::wstring cert = h.certPath;
    AuthKind kind = h.kind;

    if (!h.identityId.empty()) {
        for (const auto& id : m_identities) {
            if (id.id != h.identityId) continue;
            if (user.empty()) user = TrimWs(id.username);
            if (key.empty()) key = id.keyPath;
            if (cert.empty()) cert = id.certPath;
            kind = id.kind;
            break;
        }
    }
    if (!user.empty() && !SafeSshUser(user)) {
        if (err) *err = L"Gecersiz kullanici adi (kimlik): '-' ile baslayamaz, tirnak, kontrol ya da kabuk karakteri iceremez.";
        return {};
    }

    std::wstring cmd = QuoteArg(ssh);
    cmd += L" -v";
    if (h.port > 0 && h.port != 22) cmd += L" -p " + std::to_wstring(h.port);

    // KULLANICININ SECTIGI AUTH METHODUNA KESIN BAGLILIK:
    if (kind == AuthKind::Password) {
        // Sunucuda veya hostta parola tanimli! Key denenmesini tamamen kapat:
        cmd += L" -o PubkeyAuthentication=no";
        cmd += L" -o PreferredAuthentications=password,keyboard-interactive";
        cmd += L" -o PasswordAuthentication=yes";
        cmd += L" -o KbdInteractiveAuthentication=yes";
    }
    else if (kind == AuthKind::Key) {
        // Sunucuda veya hostta key tanimli! Parola denenmesini kapat, sadece key kullan:
        cmd += L" -o PasswordAuthentication=no";
        cmd += L" -o KbdInteractiveAuthentication=no";
        cmd += L" -o PreferredAuthentications=publickey";
        if (!key.empty()) {
            key = ResolveKeyPath(key, h.id.empty() ? h.Display() : h.id);
            cmd += L" -i " + QuoteArg(key);
            if (!cert.empty()) {
                cert = ResolveCertPath(cert, h.id.empty() ? h.Display() : h.id);
                cmd += L" -o \"CertificateFile=" + cert + L"\"";
            }
            cmd += L" -o IdentitiesOnly=yes";
        }
    }
    else if (kind == AuthKind::Agent) {
        cmd += L" -o PasswordAuthentication=no";
        cmd += L" -o PreferredAuthentications=publickey";
    }

    const std::wstring jump = TrimWs(h.jumpHost);
    if (!jump.empty()) cmd += L" -J " + QuoteArg(jump);

    cmd += L" -o ServerAliveInterval=30";
    if (!user.empty()) cmd += L" -l " + QuoteArg(user);
    // "--" sonrasi hedef her zaman konum argumanidir, secenek olarak okunmaz
    cmd += L" -- " + QuoteArg(TrimWs(h.address));
    return cmd;
}

// ------------------------------------------------------------- kalicilik ---

void Inventory::Save(const std::wstring& dir) const {
    m_dataDir = dir;
    CreateDirectoryW(dir.c_str(), nullptr);

    {
        std::wstring out = L"# FullTerminal host envanteri\n";
        for (const auto& h : m_hosts) {
            out += L"\n[host]\n";
            out += L"id=" + h.id + L"\n";
            out += L"label=" + Escape(h.label) + L"\n";
            out += L"address=" + Escape(h.address) + L"\n";
            out += L"port=" + std::to_wstring(h.port) + L"\n";
            out += L"username=" + Escape(h.username) + L"\n";
            out += L"identity=" + h.identityId + L"\n";
            out += L"auth=" + std::to_wstring((int)h.kind) + L"\n";
            out += L"key=" + Escape(h.keyPath) + L"\n";
            out += L"public_key=" + Escape(h.publicKeyPath) + L"\n";
            out += L"cert=" + Escape(h.certPath) + L"\n";
            out += L"group=" + Escape(h.group) + L"\n";
            out += L"tags=" + Escape(h.tags) + L"\n";
            out += L"jump=" + Escape(h.jumpHost) + L"\n";
            out += L"notes=" + Escape(h.notes) + L"\n";
            out += L"accent=" + std::to_wstring(h.accent) + L"\n";
            out += L"prod=" + std::wstring(h.production ? L"1" : L"0") + L"\n";
            out += L"last=" + std::to_wstring(h.lastUsed) + L"\n";
            const std::string secret = SecretForSave(h.password, h.secretBlob, h.secretLocked);
            if (!secret.empty()) out += L"secret=" + Utf8ToWide(secret) + L"\n";
        }
        WriteFileUtf8(dir + L"\\hosts.ini", out);
    }
    {
        std::wstring out = L"# FullTerminal kimlikler\n";
        for (const auto& i : m_identities) {
            out += L"\n[identity]\n";
            out += L"id=" + i.id + L"\n";
            out += L"name=" + Escape(i.name) + L"\n";
            out += L"username=" + Escape(i.username) + L"\n";
            out += L"auth=" + std::to_wstring((int)i.kind) + L"\n";
            out += L"key=" + Escape(i.keyPath) + L"\n";
            out += L"public_key=" + Escape(i.publicKeyPath) + L"\n";
            out += L"cert=" + Escape(i.certPath) + L"\n";
            const std::string secret = SecretForSave(i.password, i.secretBlob, i.secretLocked);
            if (!secret.empty()) out += L"secret=" + Utf8ToWide(secret) + L"\n";
        }
        WriteFileUtf8(dir + L"\\identities.ini", out);
    }
}

void Inventory::Load(const std::wstring& dir) {
    m_dataDir = dir;
    m_hosts.clear();
    m_identities.clear();

    auto parse = [](const std::wstring& text, const wchar_t* section,
                    auto&& onSection, auto&& onKey) {
        std::wistringstream in(text);
        std::wstring line;
        bool inSection = false;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
            if (line.empty() || line[0] == L'#') continue;
            if (line[0] == L'[') {
                inSection = (line == std::wstring(L"[") + section + L"]");
                if (inSection) onSection();
                continue;
            }
            if (!inSection) continue;
            const size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            onKey(line.substr(0, eq), line.substr(eq + 1));
        }
    };

    parse(ReadFileUtf8(dir + L"\\identities.ini"), L"identity",
          [&] { Identity i; i.id = NewId(); m_identities.push_back(std::move(i)); },
          [&](const std::wstring& k, const std::wstring& v) {
              if (m_identities.empty()) return;
              Identity& i = m_identities.back();
              if (k == L"id") i.id = v;
              else if (k == L"name") i.name = Unescape(v);
              else if (k == L"username") i.username = Unescape(v);
              else if (k == L"auth") i.kind = (AuthKind)_wtoi(v.c_str());
              else if (k == L"key") i.keyPath = Unescape(v);
              else if (k == L"public_key") i.publicKeyPath = Unescape(v);
              else if (k == L"cert") i.certPath = Unescape(v);
              else if (k == L"secret") {
                  i.secretBlob = WideToUtf8(v);
                  i.secretLocked = !UnprotectFromBase64(i.secretBlob, i.password);
              }
          });

    parse(ReadFileUtf8(dir + L"\\hosts.ini"), L"host",
          [&] { Host h; h.id = NewId(); m_hosts.push_back(std::move(h)); },
          [&](const std::wstring& k, const std::wstring& v) {
              if (m_hosts.empty()) return;
              Host& h = m_hosts.back();
              if (k == L"id") h.id = v;
              else if (k == L"label") h.label = Unescape(v);
              else if (k == L"address") h.address = Unescape(v);
              else if (k == L"port") h.port = _wtoi(v.c_str());
              else if (k == L"username") h.username = Unescape(v);
              else if (k == L"identity") h.identityId = v;
              else if (k == L"auth") h.kind = (AuthKind)_wtoi(v.c_str());
              else if (k == L"key") h.keyPath = Unescape(v);
              else if (k == L"public_key") h.publicKeyPath = Unescape(v);
              else if (k == L"cert") h.certPath = Unescape(v);
              else if (k == L"group") h.group = Unescape(v);
              else if (k == L"tags") h.tags = Unescape(v);
              else if (k == L"jump") h.jumpHost = Unescape(v);
              else if (k == L"notes") h.notes = Unescape(v);
              else if (k == L"accent") h.accent = (uint32_t)_wtoi64(v.c_str());
              else if (k == L"prod") h.production = (v == L"1");
              else if (k == L"last") h.lastUsed = _wtoi64(v.c_str());
              else if (k == L"secret") {
                  h.secretBlob = WideToUtf8(v);
                  h.secretLocked = !UnprotectFromBase64(h.secretBlob, h.password);
              }
          });
}

int Inventory::LockedSecretCount() const {
    int n = 0;
    for (const auto& h : m_hosts) if (h.secretLocked && h.password.empty()) ++n;
    for (const auto& i : m_identities) if (i.secretLocked && i.password.empty()) ++n;
    return n;
}

std::vector<ConnectionNode> Inventory::FastNodes() const {
    std::vector<ConnectionNode> nodes;

    // 1. Yerel kabuklar ve WSL dagitimlari (ShellProfiles uzerinden; yalnizca kayit defteri/dosya)
    auto profiles = DiscoverShellProfiles();
    for (const auto& p : profiles) {
        ConnectionNode node;
        if (p.kind == ProfileKind::Wsl) {
            node.id = "wsl:" + WideToUtf8(p.wslDistro);
            node.parentId = "wsl";
            node.name = WideToUtf8(p.name);
            node.type = NodeType::Wsl;
            node.path = "/local/wsl/" + WideToUtf8(p.wslDistro);
            node.status = "online";
            node.target = "wsl.exe -d " + WideToUtf8(p.wslDistro);
        } else {
            node.id = WideToUtf8(p.id);
            node.parentId = "local";
            node.name = WideToUtf8(p.name);
            node.type = NodeType::Local;
            node.path = "/local/" + WideToUtf8(p.id);
            node.status = "online";
            node.target = WideToUtf8(p.CommandLine());
        }
        nodes.push_back(std::move(node));
    }

    // 2. Envanterdeki SSH sunuculari
    for (const auto& h : m_hosts) {
        ConnectionNode node;
        node.id = WideToUtf8(h.id);
        node.parentId = "ssh";
        node.name = WideToUtf8(h.Display());
        node.type = NodeType::SshHost;
        std::string grp = WideToUtf8(h.group);
        node.path = "/ssh/" + (grp.empty() ? "" : (grp + "/")) + WideToUtf8(h.Display());
        node.status = "configured";
        node.target = WideToUtf8(h.Target());
        node.identityId = WideToUtf8(h.identityId);
        node.notes = WideToUtf8(h.notes);
        nodes.push_back(std::move(node));
    }

    // 3. Kubernetes Podlari ve Manifestleri
    auto k8sNodes = K8sManager::Instance().BuildNodes();
    for (auto& kn : k8sNodes) {
        nodes.push_back(std::move(kn));
    }

    return nodes;
}

std::vector<ConnectionNode> Inventory::ProbeSlowNodes() {
    std::vector<ConnectionNode> nodes;

    // Docker container'lari (Docker kurulu ve calisiyorsa). docker.exe Docker Desktop
    // acilirken saniyelerce bekleyebilir; bu yuzden UI thread'inde cagrilmamali.
    // Tam yol: CreateProcess ciplak adi once calisma dizininde arar (MCP'de istemcinin reposu).
    std::string dockerOut;
    int dCode = -1;
    const std::wstring docker = AgentlessExecutor::FindExecutableOnPath(L"docker.exe");
    if (!docker.empty()) {
        dCode = AgentlessExecutor::RunLocalProcess(
            AgentlessExecutor::QuoteArg(docker) +
            L" ps --format \"{{.ID}}\t{{.Names}}\t{{.Image}}\t{{.Status}}\"",
            L"", dockerOut, 3000);
    }
    if (dCode == 0 && !dockerOut.empty()) {
        std::istringstream dss(dockerOut);
        std::string line;
        while (std::getline(dss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;
            size_t t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            size_t t2 = line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            size_t t3 = line.find('\t', t2 + 1);

            std::string cId = line.substr(0, t1);
            std::string cName = line.substr(t1 + 1, t2 - t1 - 1);
            std::string cImg = (t3 != std::string::npos) ? line.substr(t2 + 1, t3 - t2 - 1) : line.substr(t2 + 1);
            std::string cStatus = (t3 != std::string::npos) ? line.substr(t3 + 1) : "running";

            ConnectionNode node;
            node.id = "docker:" + cName;
            node.parentId = "docker";
            node.name = cName;
            node.type = NodeType::DockerContainer;
            node.path = "/local/docker/" + cName;
            node.status = cStatus;
            node.target = cId;
            node.notes = "image: " + cImg;
            nodes.push_back(std::move(node));
        }
    }

    return nodes;
}

std::vector<ConnectionNode> Inventory::BuildNodeTree() const {
    std::vector<ConnectionNode> nodes = FastNodes();
    std::vector<ConnectionNode> slow = ProbeSlowNodes();
    nodes.insert(nodes.end(), std::make_move_iterator(slow.begin()), std::make_move_iterator(slow.end()));
    return nodes;
}

size_t Inventory::ImportSshKeysFromDisk() {
    const std::wstring home = UserHomeDir();
    if (home.empty()) return 0;
    const std::wstring sshDir = home + L"\\.ssh";

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW((sshDir + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    size_t imported = 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        std::wstring name = fd.cFileName;
        if (name == L"known_hosts" || name == L"known_hosts.old" || name == L"config" ||
            name == L"authorized_keys" || (name.size() > 4 && name.substr(name.size() - 4) == L".pub") ||
            (name.size() > 4 && name.substr(name.size() - 4) == L".old")) {
            continue;
        }

        std::wstring keyPath = sshDir + L"\\" + name;
        bool exists = false;
        for (const auto& id : m_identities) {
            if (_wcsicmp(id.keyPath.c_str(), keyPath.c_str()) == 0 || id.name == (name + L" (OpenSSH)")) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        bool isValidKey = false;
        HANDLE hFile = CreateFileW(keyPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            char header[256]{};
            DWORD bytesRead = 0;
            if (ReadFile(hFile, header, sizeof(header) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                std::string hStr(header, bytesRead);
                if (hStr.find("-----BEGIN") != std::string::npos ||
                    hStr.find("ssh-") != std::string::npos ||
                    hStr.find("PuTTY") != std::string::npos ||
                    name.find(L"id_") == 0 || name == L"huggingface") {
                    isValidKey = true;
                }
            }
            CloseHandle(hFile);
        } else {
            std::wstring pubTest = keyPath + L".pub";
            if (GetFileAttributesW(pubTest.c_str()) != INVALID_FILE_ATTRIBUTES) {
                isValidKey = true;
            }
        }

        if (!isValidKey) continue;

        std::wstring pubPath = keyPath + L".pub";
        if (GetFileAttributesW(pubPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            pubPath.clear();
        }

        std::wstring certPath = keyPath + L"-cert.pub";
        if (GetFileAttributesW(certPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            certPath.clear();
        }

        Identity id;
        id.id = NewId();
        id.name = name + L" (OpenSSH)";
        id.keyPath = keyPath;
        id.publicKeyPath = pubPath;
        id.certPath = certPath;
        id.kind = AuthKind::Key;
        m_identities.push_back(std::move(id));
        imported++;
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return imported;
}

size_t Inventory::ImportKnownHostsAndConfig(const KnownHostsService& kh) {
    size_t imported = 0;

    // 1. known_hosts'tan unhashed sunuculari aktar
    for (const auto& entry : kh.Entries()) {
        if (entry.isHashed || entry.hostPattern.empty() || entry.isRevoked) continue;
        std::wstring rawHost = entry.hostPattern;
        size_t comma = rawHost.find(L',');
        if (comma != std::wstring::npos) rawHost = rawHost.substr(0, comma);

        std::wstring addr = rawHost;
        int port = 22;
        if (!addr.empty() && addr.front() == L'[' && addr.find(L']') != std::wstring::npos) {
            size_t closeBracket = addr.find(L']');
            std::wstring hostPart = addr.substr(1, closeBracket - 1);
            if (closeBracket + 1 < addr.size() && addr[closeBracket + 1] == L':') {
                try {
                    port = std::stoi(addr.substr(closeBracket + 2));
                } catch (...) { port = 22; }
            }
            addr = hostPart;
        }

        if (addr.empty()) continue;

        bool exists = false;
        for (const auto& h : m_hosts) {
            if (_wcsicmp(h.address.c_str(), addr.c_str()) == 0 && h.port == port) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        Host h;
        h.id = NewId();
        h.label = addr + (port != 22 ? (L":" + std::to_wstring(port)) : L"");
        h.address = addr;
        h.port = port;
        h.group = L"Bilinen Sunucular (known_hosts)";
        h.kind = AuthKind::Password;
        m_hosts.push_back(std::move(h));
        imported++;
    }

    // 2. ~/.ssh/config dosyasini parse et
    const std::wstring home = UserHomeDir();
    if (!home.empty()) {
        std::wstring configPath = home + L"\\.ssh\\config";
        std::ifstream cf(configPath);
        if (cf.is_open()) {
            std::string line;
            Host curHost;
            bool inHost = false;

            auto saveCurHost = [&]() {
                if (inHost && !curHost.address.empty() && curHost.address != L"*") {
                    bool exists = false;
                    for (const auto& h : m_hosts) {
                        if (_wcsicmp(h.address.c_str(), curHost.address.c_str()) == 0 && h.port == curHost.port) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        curHost.id = NewId();
                        curHost.group = L"SSH Config";
                        m_hosts.push_back(curHost);
                        imported++;
                    }
                }
                curHost = Host();
                inHost = false;
            };

            while (std::getline(cf, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                size_t s = line.find_first_not_of(" \t");
                if (s == std::string::npos || line[s] == '#') continue;

                std::istringstream iss(line.substr(s));
                std::string key, val;
                iss >> key >> val;
                for (auto& c : key) c = (char)tolower((unsigned char)c);

                if (key == "host") {
                    saveCurHost();
                    inHost = true;
                    curHost.label = Utf8ToWide(val);
                    curHost.address = curHost.label;
                    curHost.port = 22;
                    curHost.kind = AuthKind::Key;
                } else if (inHost) {
                    if (key == "hostname") {
                        curHost.address = Utf8ToWide(val);
                    } else if (key == "user") {
                        curHost.username = Utf8ToWide(val);
                    } else if (key == "port") {
                        try { curHost.port = std::stoi(val); } catch (...) { curHost.port = 22; }
                    } else if (key == "identityfile") {
                        curHost.keyPath = Utf8ToWide(val);
                        if (curHost.keyPath.rfind(L"~", 0) == 0) {
                            curHost.keyPath = home + curHost.keyPath.substr(1);
                        }
                        curHost.kind = AuthKind::Key;
                    }
                }
            }
            saveCurHost();
        }
    }

    return imported;
}

} // namespace ft
