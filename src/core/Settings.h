#pragma once
//
// Tasinabilir ayarlar. portable_data/settings.ini, registry kirletmez.
//
#include <string>

namespace ft {

struct Settings {
    // --- gorunum ---
    std::wstring fontFamily;          // bos ise otomatik secim
    float        fontPt = 11.0f;
    float        opacity = 1.0f;      // terminal arka plani, 0.10 - 1.0
    int          accentChoice = 0;    // 0 jade, 1 mavi, 2 amber, 3 mor, 4 mercan
    int          cursorStyle = 0;     // 0 blok, 3 alt cizgi, 5 dikey cubuk
    bool         cursorBlink = true;
    int          colorScheme = 0;     // 0: Obsidian, 1: Visor Dark, 2: Dracula, 3: Monokai, 4: Solarized, 5: Nord, 6: Cyberpunk, 7: Retro CRT, 8: Ozel
    uint32_t     customBgColor = 0x0A0D11;
    uint32_t     customFgColor = 0xD3DAE4;

    // --- Visor / Slide-Down Modu (Yukarıdan İnen Kayan Terminal) ---
    bool quakeMode = false;           // Visor tarzı yukarıdan açılma aktif mi
    int  quakeHeightPercent = 50;     // calisma alani yuksekligi yuzdesi (%20 - %100)
    int  quakeWidthPercent = 100;     // calisma alani genisligi yuzdesi (%30 - %100)
    int  quakeAlign = 1;              // 0 sol, 1 orta, 2 sag
    int  quakeMonitor = 0;            // 0 farenin oldugu ekran, 1 birincil ekran
    int  quakeHotkeyVk = 0x7B;        // VK_F12 (0x7B)
    int  quakeHotkeyMods = 0;         // MOD_ALT 1 | MOD_CONTROL 2 | MOD_SHIFT 4 | MOD_WIN 8
    bool quakeHideOnLoseFocus = false;// baska bir uygulamaya gecince otomatik gizle
    int  quakeAnimDurationMs = 200;   // inis/cikis animasyonu, 0 = kapali

    // --- Kubernetes YAML ---
    std::wstring k8sYamlDir;          // YAML manifest klasoru
    bool         k8sAutoExport = false;// Otomatik k8s YAML disa aktarma
    bool         k8sAutoEnv = true;    // yeni terminal acilirken eklenen kubeconfig'leri KUBECONFIG olarak ver

    // --- terminal ---
    int  scrollbackLines = 10000;
    bool copyOnSelect = false;
    bool bellSound = true;
    bool pasteGuard = true;

    // --- kabuk ---
    std::wstring defaultProfile;      // ShellProfile::id

    // --- uygulama ---
    std::wstring language = L"en";    // en, tr, ru, uk, de, fr, es, it, pt, nl, pl, zh, ja, ko, ar, hi
    bool runInBackground = false;     // kapatinca tepsiye insin
    bool minimizeToTray = false;
    bool confirmClose = true;
    bool restoreSessions = false;

    // --- MCP: --mcp sunucusu bunlari her arac cagrisinda diskten okur ---
    // (mcpHttp / mcpPort / mcpToken: HTTP tasimasi henuz yok, yalnizca saklaniyor)
    bool         mcpEnabled = false;
    bool         mcpStdio = true;
    bool         mcpHttp = false;
    int          mcpPort = 8787;
    std::wstring mcpToken;
    bool         mcpReadOnly = true;      // varsayilan salt okunur (FR-MCP-020)
    bool         mcpApproval = true;      // her yazma icin insan onayi
    bool         mcpAudit = true;         // denetim izi
    bool         mcpAllowRun = false;     // ft_exec
    bool         mcpAllowFiles = false;   // ft_fs (yazma icin ayrica salt okunur kapali)
    bool         mcpAllowK8s = false;     // k8s:... hedefleri

    void Load(const std::wstring& dir);
    void Save(const std::wstring& dir) const;
};

// Exe'nin yanindaki portable_data dizini.
std::wstring PortableDataDir();

} // namespace ft
