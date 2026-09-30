#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

namespace ft {

struct ExecResult {
    int exitCode = 0;
    std::string output;
    bool truncated = false;
    std::string dumpFile;
    int totalChars = 0;
    int totalLines = 0;
};

using EnvList = std::vector<std::pair<std::wstring, std::wstring>>;

// Komutun hangi kabukta/aracta calisacagi. McpServer "system" kimligini
// (ft_systems ciktisi) bunu cozerek uretir; boylece bilinmeyen kimlik ssh'a dusmez.
enum class TargetKind {
    PowerShell,   // powershell.exe / pwsh.exe -Command
    Cmd,          // cmd.exe /d /s /c
    LocalShell,   // Git Bash / MSYS2 (bash -lc), Nushell (nu -c)
    Wsl,          // wsl.exe -d <dagitim> --exec sh -c
    Docker,       // docker.exe exec -i <container> sh -c
    K8s,          // kubectl.exe exec -i -n <ns> <tur/ad> -- sh -c
    Ssh,          // ssh.exe ... -- <hedef> <komut>
};

struct ExecTarget {
    TargetKind   kind = TargetKind::PowerShell;
    std::string  id;          // istemcinin verdigi kimlik, mesajlar icin
    std::wstring exe;         // yerel kabuk exe'si; bos ise System32'deki varsayilan
    std::wstring shellFlag;   // LocalShell: L"-lc" (bash) veya L"-c" (nu)
    std::wstring name;        // WSL dagitimi, container ya da k8s kaynagi ("deploy/web")
    std::wstring ns;          // k8s namespace
    std::wstring sshPrefix;   // hazir ssh komut satiri, "-- <hedef>" ile biter
    EnvList      env;         // cocuk surece eklenecek ortam degiskenleri

    bool IsLocal() const {
        return kind == TargetKind::PowerShell || kind == TargetKind::Cmd || kind == TargetKind::LocalShell;
    }
};

class AgentlessExecutor {
public:
    // Execute command or script on target system node with timeout and context-guard
    static ExecResult Exec(const ExecTarget& target,
                           const std::string& command,
                           int timeoutMs = 15000,
                           int maxLines = 50,
                           int maxChars = 2500);

    // Unified Virtual Filesystem operations
    static ExecResult ListFiles(const ExecTarget& target,
                                const std::string& path,
                                int maxItems = 50);

    static ExecResult ReadFile(const ExecTarget& target,
                               const std::string& path,
                               int offsetLines = 0,
                               int limitLines = 50);

    static ExecResult WriteFile(const ExecTarget& target,
                                const std::string& path,
                                const std::string& content,
                                bool append = false);

    static ExecResult GetFileInfo(const ExecTarget& target,
                                  const std::string& path);

    // Apply SkyMemory-style Context Budget Guard to prevent LLM context exhaustion
    static ExecResult ApplyContextGuard(const std::string& rawOutput,
                                        int exitCode,
                                        int maxLines = 50,
                                        int maxChars = 2500);

    // Run a local Windows process hidden and capture stdout/stderr (tek boru) with timeout.
    // timeoutMs <= 0: sure siniri yok. Zaman asiminda tum surec agaci (Job) oldurulur, 124 doner.
    // stdinData verilirse cocugun stdin'ine yazilir (buyuk icerik komut satirina gomulmez),
    // yoksa stdin NUL'dur. env verilirse mevcut ortama eklenir/uzerine yazilir.
    // Surec baslatilamazsa -1 doner ve outStd nedeni icerir.
    static int RunLocalProcess(const std::wstring& cmdLine,
                               const std::wstring& startDir,
                               std::string& outStd,
                               int timeoutMs = 15000,
                               const std::string* stdinData = nullptr,
                               const EnvList* env = nullptr);

    // Yalniz PATH'te arar, calisma dizinine bakmaz (bos: bulunamadi). Harici araclar
    // (docker/kubectl) her zaman bununla cozulmeli: MCP'de calisma dizini istemcinin reposu.
    static std::wstring FindExecutableOnPath(const wchar_t* exeName);

    // Windows CommandLineToArgvW kurallarina gore tek arguman olarak tirnaklar.
    static std::wstring QuoteArg(const std::wstring& arg);
    // POSIX sh icin tek tirnakli kacis ('\'' kalibi).
    static std::string ShQuote(const std::string& s);

    // Set storage directory for dump files (portable_data/scratch)
    static void SetDataDir(const std::wstring& dir);
    static std::wstring GetDataDir();

private:
    static std::wstring s_dataDir;
};

} // namespace ft
