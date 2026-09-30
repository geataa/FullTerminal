#pragma once
//
// FullTerminal Cok Dilli Uluslararasilasma (i18n / Localization) Motoru
//
// Desteklenen 16 Dil:
//  0: English (en) - Varsayilan
//  1: Türkçe (tr)
//  2: Русский (ru)
//  3: Українська (uk)
//  4: Deutsch (de)
//  5: Français (fr)
//  6: Español (es)
//  7: Italiano (it)
//  8: Português (pt)
//  9: Nederlands (nl)
// 10: Polski (pl)
// 11: 简体中文 (zh)
// 12: 日本語 (ja)
// 13: 한국어 (ko)
// 14: العربية (ar)
// 15: हिन्दी (hi)
//
#include <string>
#include <vector>

namespace ft {

enum class LangId : int {
    En = 0, // English (Default)
    Tr = 1, // Türkçe
    Ru = 2, // Русский
    Uk = 3, // Українська
    De = 4, // Deutsch
    Fr = 5, // Français
    Es = 6, // Español
    It = 7, // Italiano
    Pt = 8, // Português
    Nl = 9, // Nederlands
    Pl = 10,// Polski
    Zh = 11,// 简体中文
    Ja = 12,// 日本語
    Ko = 13,// 한국어
    Ar = 14,// العربية
    Hi = 15 // हिन्दी
};

struct LangInfo {
    LangId         id;
    const wchar_t* code;   // "en", "tr", "ru", "uk", ...
    const wchar_t* name;   // "English", "Türkçe", "Русский", "Українська", ...
    const wchar_t* badge;  // "EN", "TR", "RU", "UK", ...
};

enum class Msg : int {
    // Navigation / Kenar Cubugu
    NavTerminal,
    NavSystemsHub,
    NavSftpFiles,
    NavIdentities,
    NavPortForward,
    NavSnippets,
    NavKnownHosts,
    NavLogs,
    NavSettings,

    // Actions & General Buttons
    ActionNewTab,
    ActionCloseTab,
    ActionClose,
    ActionMinimize,
    ActionMaximize,
    ActionRestore,
    ActionSave,
    ActionCancel,
    ActionApply,
    ActionConnect,
    ActionDisconnect,
    ActionRefresh,
    ActionEdit,
    ActionDelete,
    ActionAdd,
    ActionSearch,
    ActionImport,
    ActionExport,
    ActionSplitH,
    ActionSplitV,
    ActionCopy,
    ActionPaste,
    ActionClear,
    ActionExit,
    ActionOpenFolder,

    // Settings Tabs
    SettingsTitle,
    TabAppearance,
    TabTerminal,
    TabShell,
    TabVisor,
    TabApplication,
    TabLanguage,
    TabMcp,
    TabKubernetes,
    TabAbout,

    // Settings Fields & Labels
    LangSelectTitle,
    LangSelectDesc,
    AccentColor,
    FontFamily,
    FontSize,
    Opacity,
    CursorStyle,
    CursorBlink,
    ColorPalette,
    ScrollbackLines,
    CopyOnSelect,
    BellSound,
    PasteGuard,
    RunInBackground,
    MinimizeToTray,
    ConfirmClose,
    RestoreSessions,
    OpenDataDir,
    QuitApp,
    CustomColors,
    VisorSettings,
    ApplyFont,
    FullOpaque,

    // Status Bar & HUD
    StatusInfrastructure,
    StatusSessions,
    StatusTransparent,
    StatusOpaque,
    StatusMenuDrawer,
    StatusCloseDrawer,
    StatusApproved,
    StatusWorking,
    StatusIdle,

    // SFTP & Remote
    SftpLocalFiles,
    SftpRemoteFiles,
    SftpUpload,
    SftpDownload,
    SftpParentDir,
    SftpNewFolder,
    SftpConnected,

    // Hosts & Key Management
    AddHostTitle,
    ImportSshConfig,
    HostLabel,
    HostAddress,
    HostPort,
    HostUsername,
    HostAuthMethod,
    AuthPassword,
    AuthKey,
    AuthAgent,

    // Snippets & K8s
    AddSnippetTitle,
    AddK8sYamlTitle,
    SnippetName,
    SnippetCode,
    SnippetRunCmd,
    SnippetUpdatedToast,
    YamlExportedToast,

    // Toasts & Notifications
    ToastLangChanged,
    ToastSaved,
    ToastCopied,

        // Additional Settings & Options
    ProfileFirstFound,
    ToolbarBroadcast,
    DefaultProfile,
    DiscoveredProfiles,
    DefaultTag,
    NoteOpacity,
    NoteScrollback,
    NoteRunInBackground,
    NoteRestoreSessions,
    CursorStyleBlock,
    CursorStyleUnderline,
    CursorStyleBar,
    AnsiColors,

    // Tabs & Context Menu
    TabRenameTitle,
    TabRenamePlaceholder,
    ActionReset,
    ToastTabReset,
    ToastTabSaved,
    MenuRenameTab,
    MenuCopyTabTitle,
    MenuResetTabTitle,
    MenuCloseOtherTabs,
    MenuCloseRightTabs,
    BroadcastOnToast,
    BroadcastOffToast,

    // Context Menus & Actions
    MenuZoomIn,
    MenuZoomOut,
    MenuCloseActivePane,
    MenuCloseActiveTab,
    MenuCopy,
    MenuPaste,
    MenuClearTerminal,
    MenuApplyYaml,
    MenuExportYaml,
    MenuSnippetsK8s,
    SftpMenuOpen,
    SftpMenuDownload,
    SftpMenuUpload,
    SftpMenuRename,
    SftpMenuDelete,
    SftpMenuCopyPath,
    SftpMenuNewFolder,
    SftpMenuRefresh,
    TrayShow,
    TrayHide,
    TrayNewSession,
    TrayVisorToggleOff,
    TrayVisorToggleOn,
    TrayQuit,
    ToastNotConnected,

    _Count
};

class I18n {
public:
    static void Init(const std::wstring& langCode = L"en");
    static void SetLanguage(LangId id);
    static void SetLanguage(const std::wstring& code);
    static LangId CurrentLang();
    static const wchar_t* CurrentCode();
    static const wchar_t* CurrentName();
    static const wchar_t* CurrentBadge();
    static const wchar_t* Get(Msg key);
    static const std::vector<LangInfo>& Languages();
    static void CycleLanguage();
};

inline const wchar_t* Tr(Msg key) {
    return I18n::Get(key);
}

} // namespace ft
