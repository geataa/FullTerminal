#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace ft {

enum class ProfileKind {
    PowerShell,   // Windows PowerShell 5.1
    Pwsh,         // PowerShell 7+
    Cmd,
    GitBash,
    Msys2,
    Nushell,
    Wsl,
    Custom,
};

struct ShellProfile {
    std::wstring id;          // kalici kimlik, ayar dosyasinda kullanilir
    std::wstring name;        // sekmede gorunen ad
    std::wstring exe;         // tam yol
    std::wstring args;        // komut satiri argumanlari
    std::wstring startDir;    // bos ise kullanici profil dizini
    std::wstring badge;       // sekme rozetindeki kisa etiket, orn. "PS", "WSL"
    uint32_t accent = 0;      // 0xRRGGBB, sol serit ve rozet rengi
    ProfileKind kind = ProfileKind::Custom;

    // WSL'e ozel
    std::wstring wslDistro;
    int wslVersion = 0;       // 1 veya 2
    bool wslDefault = false;

    // Terminal acilirken enjekte edilecek ortam degiskenleri.
    // FR-K8S-010: eklenen kubeconfig'ler K8sManager::TerminalEnv ile buraya eklenir.
    std::vector<std::pair<std::wstring, std::wstring>> env;

    // Host baglantilarinda hazir komut satiri dogrudan verilir.
    std::wstring rawCommand;

    std::wstring CommandLine() const {
        if (!rawCommand.empty()) return rawCommand;
        std::wstring cl = L"\"" + exe + L"\"";
        if (!args.empty()) cl += L" " + args;
        return cl;
    }
};

// Sistemde kurulu kabuklari bulur. Sira: varsayilan once.
std::vector<ShellProfile> DiscoverShellProfiles();

// Kullanici profil dizini (%USERPROFILE%).
std::wstring UserHomeDir();

// Tek bir argumani CommandLineToArgvW/MSVCRT kurallarina gore tirnaklar.
// Ic tirnak ve tirnaktan onceki ters bolu dizileri kacislanir; bosluk argumani bolemez.
std::wstring QuoteArg(const std::wstring& arg);

} // namespace ft
