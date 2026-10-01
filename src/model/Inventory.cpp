#include "model/Inventory.h"
#include "model/K8sManager.h"
#include "core/Platform.h"
#include "core/Utf8.h"
#include "core/ShellProfiles.h"
#include "services/KnownHostsService.h"
#include "transport/agentless/AgentlessExecutor.h"

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#include <shlwapi.h>
#pragma comment(lib, "crypt32.lib")
#else
#include "core/Sha256.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <random>
#endif

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <filesystem>

namespace ft {
namespace {

std::wstring EnvVar(const wchar_t* n) {
#if defined(_WIN32)
    wchar_t buf[1024];
    DWORD k = GetEnvironmentVariableW(n, buf, (DWORD)std::size(buf));
    return (k && k < std::size(buf)) ? std::wstring(buf, k) : std::wstring();
#else
    std::string name = WideToUtf8(n);
    const char* val = getenv(name.c_str());
    return val ? Utf8ToWide(val) : std::wstring();
#endif
}

static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (static_cast<uint32_t>(data[i]) << 16) |
                     ((i + 1 < len ? static_cast<uint32_t>(data[i + 1]) : 0) << 8) |
                     (i + 2 < len ? static_cast<uint32_t>(data[i + 2]) : 0);
        out.push_back(b64_chars[(b >> 18) & 0x3F]);
        out.push_back(b64_chars[(b >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? b64_chars[(b >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? b64_chars[b & 0x3F] : '=');
    }
    return out;
}

static std::vector<uint8_t> Base64Decode(const std::string& in) {
    std::vector<uint8_t> out;
    int T[256];
    std::fill(std::begin(T), std::end(T), -1);
    for (int i = 0; i < 64; i++) T[static_cast<unsigned char>(b64_chars[i])] = i;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        if (T[static_cast<unsigned char>(c)] == -1) continue;
        val = (val << 6) + T[static_cast<unsigned char>(c)];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

#if !defined(_WIN32)
static std::vector<uint8_t> GetLinuxMasterKey() {
    std::string material;
    std::ifstream mid("/etc/machine-id");
    if (mid.is_open()) {
        std::string s;
        mid >> s;
        material += s;
    } else {
        std::ifstream mid2("/var/lib/dbus/machine-id");
        if (mid2.is_open()) {
            std::string s;
            mid2 >> s;
            material += s;
        }
    }
    uid_t uid = getuid();
    material += std::to_string(uid);
    const char* user = getenv("USER");
    if (user) material += user;

    const char* home = getenv("HOME");
    if (home) {
        std::string vdir = std::string(home) + "/.config/fullterminal";
        std::string vpath = vdir + "/.vault_key";
        std::ifstream vf(vpath, std::ios::binary);
        if (vf.is_open()) {
            std::string extra((std::istreambuf_iterator<char>(vf)), std::istreambuf_iterator<char>());
            material += extra;
        } else {
            std::error_code ec;
            std::filesystem::create_directories(vdir, ec);
            std::vector<uint8_t> rnd(32);
            int fd = open("/dev/urandom", O_RDONLY);
            if (fd >= 0) {
                if (read(fd, rnd.data(), rnd.size()) <= 0) {}
                close(fd);
            }
            int wfd = open(vpath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (wfd >= 0) {
                if (write(wfd, rnd.data(), rnd.size()) <= 0) {}
                close(wfd);
            }
            material.append(reinterpret_cast<char*>(rnd.data()), rnd.size());
        }
    }

    return Sha256::Hash(material.data(), material.size());
}
#endif

// ---- DPAPI / Salted Machine Key: parolalar diske hicbir zaman duz yazilmaz --------------------

std::string ProtectToBase64(const std::wstring& plain) {
    if (plain.empty()) return {};
#if defined(_WIN32)
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
#else
    std::vector<uint8_t> key = GetLinuxMasterKey();
    std::string utf8 = WideToUtf8(plain);

    std::vector<uint8_t> iv(16);
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, iv.data(), iv.size()) <= 0) {}
        close(fd);
    } else {
        std::random_device rd;
        for (int i = 0; i < 16; i++) iv[i] = rd() & 0xFF;
    }

    std::vector<uint8_t> ct(utf8.size());
    uint32_t counter = 0;
    size_t offset = 0;
    while (offset < utf8.size()) {
        std::vector<uint8_t> blockInput;
        blockInput.insert(blockInput.end(), key.begin(), key.end());
        blockInput.insert(blockInput.end(), iv.begin(), iv.end());
        blockInput.push_back((counter >> 24) & 0xFF);
        blockInput.push_back((counter >> 16) & 0xFF);
        blockInput.push_back((counter >> 8) & 0xFF);
        blockInput.push_back(counter & 0xFF);
        counter++;
        std::vector<uint8_t> ks = Sha256::Hash(blockInput.data(), blockInput.size());
        for (size_t i = 0; i < ks.size() && offset < utf8.size(); i++, offset++) {
            ct[offset] = static_cast<uint8_t>(utf8[offset]) ^ ks[i];
        }
    }

    std::vector<uint8_t> macInput;
    macInput.insert(macInput.end(), key.begin(), key.end());
    macInput.insert(macInput.end(), iv.begin(), iv.end());
    macInput.insert(macInput.end(), ct.begin(), ct.end());
    std::vector<uint8_t> mac = Sha256::Hash(macInput.data(), macInput.size());

    std::vector<uint8_t> payload;
    payload.insert(payload.end(), iv.begin(), iv.end());
    payload.insert(payload.end(), mac.begin(), mac.end());
    payload.insert(payload.end(), ct.begin(), ct.end());

    return Base64Encode(payload.data(), payload.size());
#endif
}

// Basarisizlik (baska kullanici/PC'nin anahtari) bos parolayla karismasin diye bool doner.
bool UnprotectFromBase64(const std::string& b64, std::wstring& plain) {
    plain.clear();
    if (b64.empty()) return true;
#if defined(_WIN32)
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
#else
    std::vector<uint8_t> payload = Base64Decode(b64);
    if (payload.size() < 16 + 32) return false;

    std::vector<uint8_t> key = GetLinuxMasterKey();
    std::vector<uint8_t> iv(payload.begin(), payload.begin() + 16);
    std::vector<uint8_t> expectedMac(payload.begin() + 16, payload.begin() + 48);
    std::vector<uint8_t> ct(payload.begin() + 48, payload.end());

    std::vector<uint8_t> macInput;
    macInput.insert(macInput.end(), key.begin(), key.end());
    macInput.insert(macInput.end(), iv.begin(), iv.end());
    macInput.insert(macInput.end(), ct.begin(), ct.end());
    std::vector<uint8_t> calcMac = Sha256::Hash(macInput.data(), macInput.size());

    if (calcMac != expectedMac) return false;

    std::string utf8(ct.size(), '\0');
    uint32_t counter = 0;
    size_t offset = 0;
    while (offset < ct.size()) {
        std::vector<uint8_t> blockInput;
        blockInput.insert(blockInput.end(), key.begin(), key.end());
        blockInput.insert(blockInput.end(), iv.begin(), iv.end());
        blockInput.push_back((counter >> 24) & 0xFF);
        blockInput.push_back((counter >> 16) & 0xFF);
        blockInput.push_back((counter >> 8) & 0xFF);
        blockInput.push_back(counter & 0xFF);
        counter++;
        std::vector<uint8_t> ks = Sha256::Hash(blockInput.data(), blockInput.size());
        for (size_t i = 0; i < ks.size() && offset < ct.size(); i++, offset++) {
            utf8[offset] = static_cast<char>(ct[offset] ^ ks[i]);
        }
    }

    plain = Utf8ToWide(utf8);
    return true;
#endif
}

// Diske yazilacak "secret=" degeri. Bos donerse satir hic yazilmaz.
std::string SecretForSave(const std::wstring& password, const std::string& blob, bool locked) {
    if (!password.empty()) {
        std::string b = ProtectToBase64(password);
        return b.empty() ? blob : b;
    }
    return locked ? blob : std::string();
}

bool HasCtlChar(const std::wstring& s) {
    for (wchar_t c : s) if (c < 0x20 || c == 0x7F) return true;
    return false;
}

// ssh hedefi/atlama sunucusu: secenek gibi okunamasin ve tek arguman olarak kalsin.
bool SafeSshHost(const std::wstring& s) {
    if (s.empty() || s[0] == L'-') return false;
    for (wchar_t c : s) {
        if (c <= 0x20 || c == 0x7F || iswspace(c) || wcschr(L"'`\"$\\;&<>|(){}", c)) return false;
    }
    return true;
}

// Kullanici adi -l ile tirnakli gider; Windows hesaplarindaki bosluk ve user@domain serbest.
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
    std::filesystem::path p(path);
    std::filesystem::path tmp = p;
    tmp += ".tmp";
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    out.write(utf8.data(), utf8.size());
    out.close();

    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
    }
}

std::wstring ReadFileUtf8(const std::wstring& path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in.is_open()) return {};
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Utf8ToWide(data);
}

} // namespace

std::wstring Inventory::NewId() {
#if defined(_WIN32)
    GUID g{};
    CoCreateGuid(&g);
    wchar_t buf[40];
    swprintf_s(buf, L"%08x%04x%04x%02x%02x%02x%02x",
               g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3]);
    return buf;
#else
    uint8_t b[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, b, sizeof(b)) <= 0) {}
        close(fd);
    } else {
        std::random_device rd;
        for (int i = 0; i < 16; i++) b[i] = rd() & 0xFF;
    }
    wchar_t buf[64];
    swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return buf;
#endif
}

std::wstring FindSshExe() {
#if defined(_WIN32)
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
#else
    const char* paths[] = { "/usr/bin/ssh", "/bin/ssh", "/usr/local/bin/ssh" };
    for (const char* p : paths) {
        if (access(p, X_OK) == 0) return ft::Utf8ToWide(p);
    }
    return L"ssh";
#endif
}

std::wstring FindSftpExe() {
#if defined(_WIN32)
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
#else
    const char* paths[] = { "/usr/bin/sftp", "/bin/sftp", "/usr/local/bin/sftp" };
    for (const char* p : paths) {
        if (access(p, X_OK) == 0) return ft::Utf8ToWide(p);
    }
    return L"sftp";
#endif
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
        std::filesystem::path dir = m_dataDir.empty() ? std::filesystem::path("keys") : (std::filesystem::path(m_dataDir) / "keys");
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::wstring safeId = idOrName.empty() ? L"temp" : idOrName;
        for (wchar_t& c : safeId) {
            if (c == L' ' || c == L':' || c == L'/' || c == L'\\') c = L'_';
        }
        std::filesystem::path keyFile = dir / (L"key_" + safeId + L".pem");
        std::string utf8Content = WideToUtf8(keyOrContent);
        if (!utf8Content.empty() && utf8Content.back() != '\n') utf8Content += "\n";
        WriteFileUtf8(keyFile.wstring(), Utf8ToWide(utf8Content));
#if !defined(_WIN32)
        chmod(keyFile.string().c_str(), 0600);
#endif
        return keyFile.wstring();
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
        std::filesystem::path dir = m_dataDir.empty() ? std::filesystem::path("keys") : (std::filesystem::path(m_dataDir) / "keys");
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::wstring safeId = idOrName.empty() ? L"temp" : idOrName;
        for (wchar_t& c : safeId) {
            if (c == L' ' || c == L':' || c == L'/' || c == L'\\') c = L'_';
        }
        std::filesystem::path certFile = dir / (L"cert_" + safeId + L".pub");
        std::string utf8Content = WideToUtf8(certOrContent);
        if (!utf8Content.empty() && utf8Content.back() != '\n') utf8Content += "\n";
        WriteFileUtf8(certFile.wstring(), Utf8ToWide(utf8Content));
        return certFile.wstring();
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
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dir), ec);

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
        WriteFileUtf8((std::filesystem::path(dir) / "hosts.ini").wstring(), out);
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
        WriteFileUtf8((std::filesystem::path(dir) / "identities.ini").wstring(), out);
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

    parse(ReadFileUtf8((std::filesystem::path(dir) / "identities.ini").wstring()), L"identity",
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

    parse(ReadFileUtf8((std::filesystem::path(dir) / "hosts.ini").wstring()), L"host",
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
    const std::wstring docker = AgentlessExecutor::FindExecutableOnPath(
#if defined(_WIN32)
        L"docker.exe"
#else
        L"docker"
#endif
    );
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
    std::filesystem::path sshDir = std::filesystem::path(home) / ".ssh";
    std::error_code ec;
    if (!std::filesystem::exists(sshDir, ec)) return 0;

    size_t imported = 0;
    for (const auto& entry : std::filesystem::directory_iterator(sshDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::wstring name = entry.path().filename().wstring();
        if (name == L"known_hosts" || name == L"known_hosts.old" || name == L"config" ||
            name == L"authorized_keys" || (name.size() > 4 && name.substr(name.size() - 4) == L".pub") ||
            (name.size() > 4 && name.substr(name.size() - 4) == L".old")) {
            continue;
        }

        std::wstring keyPath = entry.path().wstring();
        bool exists = false;
        for (const auto& id : m_identities) {
            if (_wcsicmp(id.keyPath.c_str(), keyPath.c_str()) == 0 || id.name == (name + L" (OpenSSH)")) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        bool isValidKey = false;
        std::ifstream kf(entry.path(), std::ios::binary);
        if (kf.is_open()) {
            char header[256]{};
            kf.read(header, sizeof(header) - 1);
            std::streamsize bytesRead = kf.gcount();
            if (bytesRead > 0) {
                std::string hStr(header, bytesRead);
                if (hStr.find("-----BEGIN") != std::string::npos ||
                    hStr.find("ssh-") != std::string::npos ||
                    hStr.find("PuTTY") != std::string::npos ||
                    name.find(L"id_") == 0 || name == L"huggingface") {
                    isValidKey = true;
                }
            }
        } else {
            std::filesystem::path pubTest = entry.path();
            pubTest += ".pub";
            if (std::filesystem::exists(pubTest, ec)) {
                isValidKey = true;
            }
        }

        if (!isValidKey) continue;

        std::filesystem::path pubPath = entry.path();
        pubPath += ".pub";
        std::wstring pubStr = std::filesystem::exists(pubPath, ec) ? pubPath.wstring() : L"";

        std::filesystem::path certPath = entry.path();
        certPath += "-cert.pub";
        std::wstring certStr = std::filesystem::exists(certPath, ec) ? certPath.wstring() : L"";

        Identity id;
        id.id = NewId();
        id.name = name + L" (OpenSSH)";
        id.keyPath = keyPath;
        id.publicKeyPath = pubStr;
        id.certPath = certStr;
        id.kind = AuthKind::Key;
        m_identities.push_back(std::move(id));
        imported++;
    }
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
        std::filesystem::path configPath = std::filesystem::path(home) / ".ssh" / "config";
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
