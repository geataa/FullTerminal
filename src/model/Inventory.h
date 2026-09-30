#pragma once
//
// Envanter: Vault > Grup > Host, ve Kimlikler (Keychain).
// M0/M1 icin dosya bicimi duz INI benzeri, Gety'nin Config kalibi.
// Parolalar diske DPAPI ile sifreli yazilir (FR-SEC-009).
//
#include <string>
#include <vector>
#include <cstdint>

namespace ft {

enum class AuthKind {
    Password = 0,
    Key      = 1,
    Agent    = 2,
};

enum class NodeType {
    Local,          // Yerel kabuk (PowerShell, CMD, Git Bash, vb.)
    Wsl,            // WSL2 dagitimi (Ubuntu, Debian, vb.)
    SshHost,        // SSH sunucusu (Bastion / Normal)
    DockerContainer,// Docker container
    K8sPod,         // Kubernetes Pod
    Custom
};

struct ConnectionNode {
    std::string id;
    std::string parentId;
    std::string name;
    NodeType    type = NodeType::Local;
    std::string path;        // ornek: "/local/pwsh", "/ssh/bastion", "/local/docker/media-creator"
    std::string status = "online";
    std::string target;      // adres / komut / container id
    std::string identityId;
    std::string notes;
};

struct Identity {
    std::wstring id;
    std::wstring name;      // gorunen ad
    std::wstring username;
    std::wstring password;  // bellekte duz, diske DPAPI ile
    std::wstring keyPath;          // Private key yolu (Kind == Key iken zorunlu)
    std::wstring publicKeyPath;    // Public key yolu (istege bagli)
    std::wstring certPath;         // Sertifika yolu (istege bagli)
    std::wstring passphrase;       // Anahtar parolasi (istege bagli)
    AuthKind kind = AuthKind::Password;
    // DPAPI baska kullanici/PC'de cozemezse sifreli blob oldugu gibi tutulur ve
    // Save'de geri yazilir; tasinan portable_data'daki parola sessizce silinmez.
    std::string  secretBlob;
    bool         secretLocked = false;
};

struct Host {
    std::wstring id;
    std::wstring label;
    std::wstring address;
    int          port = 22;
    std::wstring username;
    std::wstring identityId;   // bos ise asagidaki alanlar kullanilir
    std::wstring password;
    std::wstring keyPath;
    std::wstring publicKeyPath;
    std::wstring certPath;
    AuthKind     kind = AuthKind::Password;
    std::wstring group;        // duz grup adi, ic ice yapi M1'de
    std::wstring tags;         // virgulle ayrilmis
    std::wstring jumpHost;     // user@host[:port]
    std::wstring notes;
    uint32_t     accent = 0;   // 0 ise gruptan/varsayilandan
    bool         production = false;
    int64_t      lastUsed = 0;
    std::string  secretBlob;   // bkz. Identity::secretBlob
    bool         secretLocked = false;

    std::wstring Display() const { return label.empty() ? address : label; }
    std::wstring Target() const {
        std::wstring u = username;
        return u.empty() ? address : (u + L"@" + address);
    }
};

class Inventory {
public:
    void Load(const std::wstring& dir);
    void Save(const std::wstring& dir) const;

    std::vector<Host>&           hosts()      { return m_hosts; }
    const std::vector<Host>&     hosts() const { return m_hosts; }
    std::vector<Identity>&       identities() { return m_identities; }
    const std::vector<Identity>& identities() const { return m_identities; }

    Host*     FindHost(const std::wstring& id);
    Identity* FindIdentity(const std::wstring& id);

    Host&     AddHost();
    Identity& AddIdentity();
    void      RemoveHost(const std::wstring& id);
    void      RemoveIdentity(const std::wstring& id);

    std::vector<std::wstring> Groups() const;

    // Windows OpenSSH Standartlarindan Otomatik Icerik Aktarimi
    size_t ImportSshKeysFromDisk();
    size_t ImportKnownHostsAndConfig(const class KnownHostsService& kh);

    // Bu makinede DPAPI ile cozulemeyen (secretLocked) host + kimlik parolasi sayisi.
    int LockedSecretCount() const;

    // Surec baslatmayan dugumler: yerel kabuklar, WSL, SSH host'lari, yuklu K8s YAML'lari.
    // UI thread'inde guvenle cagrilir.
    std::vector<ConnectionNode> FastNodes() const;

    // docker ps gibi dis surec calistiran tarama; saniyeler surebilir.
    // Hicbir Inventory/K8sManager durumuna dokunmaz, worker thread'den cagrilabilir.
    static std::vector<ConnectionNode> ProbeSlowNodes();

    // XPipe tarzi tam agac: FastNodes() + ProbeSlowNodes() (MCP gibi senkron kullanicilar icin)
    std::vector<ConnectionNode> BuildNodeTree() const;

    // Host'a gore ssh.exe komut satiri uretir.
    // M1'de libssh2 gelene kadar Windows'un yerlesik OpenSSH istemcisi kullaniliyor.
    // Adres/kullanici/atlama sunucusu gecersizse bos doner ve *err nedeni soyler.
    std::wstring BuildSshCommand(const Host& h, std::wstring* err = nullptr) const;
    std::wstring ResolveKeyPath(const std::wstring& keyOrContent, const std::wstring& idOrName) const;
    std::wstring ResolveCertPath(const std::wstring& certOrContent, const std::wstring& idOrName) const;
    void SetDataDir(const std::wstring& d) { m_dataDir = d; }
    const std::wstring& DataDir() const { return m_dataDir; }

    // Adres ve atlama sunucusu '-' ile baslayamaz, bosluk/tirnak/kontrol/kabuk karakteri iceremez;
    // kullanici adi '-' ile baslayamaz, tirnak/kontrol/kabuk karakteri iceremez (bosluk serbest,
    // -l ile tirnakli gider). Kurallar OpenSSH 9.6 valid_hostname/valid_ruser ile ayni.
    // UI ve MCP add_host kaydetmeden once de kullanabilir.
    static bool ValidateSshFields(const Host& h, std::wstring* err = nullptr);

    static std::wstring NewId();

private:
    std::vector<Host> m_hosts;
    std::vector<Identity> m_identities;
    mutable std::wstring m_dataDir;
};

// Windows OpenSSH istemcisi kurulu mu, tam yolu.
std::wstring FindSshExe();
std::wstring FindSftpExe();

} // namespace ft
