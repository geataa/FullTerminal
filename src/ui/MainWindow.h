#pragma once
//
// Cercevesiz ana pencere: kendi baslik cubugu, sekme seridi, ikonlu kenar
// cubugu, ekranlar ve durum cubugu. Tepsi simgesi ve arka planda calisma.
//
#include "core/ShellProfiles.h"
#include "core/Settings.h"
#include "model/Inventory.h"
#include "model/SftpModel.h"
#include "model/SnippetModel.h"
#include "model/TunnelModel.h"
#include "services/KeyGenService.h"
#include "services/KnownHostsService.h"
#include "render/Renderer.h"
#include "ui/TerminalTab.h"
#include "ui/PaneLayout.h"
#include "ui/Ui.h"
#include "services/SwarmWorkspace.h"

#include <windows.h>
#include <memory>
#include <vector>
#include <string>

namespace ft {

namespace json { class Value; }

class MainWindow {
public:
    // Ikinci bir kopya calistirilinca mevcut pencereye "one gel" der (tek ornek).
    static constexpr UINT WM_SUMMON = WM_APP + 3;
    static const wchar_t* ClassName();

    bool Create(HINSTANCE inst, std::wstring* err);
    void Show(int nCmdShow);
    bool Tick();

    HWND hwnd() const { return m_hwnd; }
    bool Alive() const { return m_hwnd != nullptr; }
    bool SaveScreenshot(const wchar_t* path) { return m_r.SaveScreenshot(path); }
    void ForceRender() { Render(); }
    void ClearToast() { m_toast.clear(); m_toastUntil = 0; }

private:
    enum class View {
        Terminal = 0, Hosts, Sftp, Keychain, PortForward, Snippets, KnownHosts, Logs, Settings
    };
    enum class Icon {
        Terminal, Hosts, Sftp, Key, Forward, Snippet, Shield, Clock, Gear, Plus, Search, Folder, Refresh, Download, Upload
    };

    struct Layout {
        float scale = 1.0f;
        float titleH = 38.0f;
        float statusH = 26.0f;
        float railW = 3.0f;
        float sideW = 0.0f;
        D2D1_RECT_F title{}, status{}, rail{}, side{}, main{}, term{};
        std::vector<D2D1_RECT_F> tabs;       // yalnizca GORUNUR sekmeler
        std::vector<D2D1_RECT_F> tabClose;   // tabs ile kilit adim, bos = kapatma yok
        D2D1_RECT_F overflow{};              // "+N" cipi, bos olabilir
        size_t tabFirst = 0;                 // tabs[0] -> m_tabs[tabFirst]
        int    hidden = 0;                   // seride sigmayan sekme sayisi
        int    tabTier = 2;                  // 2 FULL, 1 COMPACT, 0 MICRO
        D2D1_RECT_F menuBtn{}, newTab{}, btnQuake{}, btnMin{}, btnMax{}, btnClose{};
        D2D1_RECT_F btnSplitV{}, btnSplitH{}, btnSync{};
    };

    // Baslik olcumleri her karede yeniden hesaplanmasin diye sekme basina onbellek.
    struct LabelCache {
        std::wstring src, out;
        float w = -1.0f, px = 0.0f;
        std::wstring bsrc;
        std::wstring bfam;               // rozet mono fontla olculur; aile degisirse yeniden olc
        float bw = -1.0f, bpx = 0.0f;
    };

    // Secim mutlak satirlarda tutulur (Screen::ViewRowToAbs): kaydirma veya
    // yeni cikti gelince vurgu ve kopyalanan metin ayni satirlarda kalir.
    struct AbsSelection {
        bool    active = false;
        bool    alt = false;             // alternatif tamponda mi baslatildi
        int     x0 = 0, x1 = 0;
        int64_t y0 = 0, y1 = 0;
    };

    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    // ---- cizim ----
    void ComputeLayout();
    void Render();
    void DrawChrome();
    void DrawTabStrip();
    void DrawWindowButtons();
    void DrawSidebar();
    void DrawStatusBar();
    void DrawOverlay();
    void DrawIcon(Icon ic, const D2D1_RECT_F& r, uint32_t color);

    void DrawHostsScreen(const D2D1_RECT_F& a);
    void DrawHostDetail(const D2D1_RECT_F& a, Host& h);
    void DrawIdentitiesScreen(const D2D1_RECT_F& a);
    void DrawSettingsScreen(const D2D1_RECT_F& a);
    void DrawSftpScreen(const D2D1_RECT_F& a, SftpController* ctrl = nullptr);
    void DrawKnownHostsScreen(const D2D1_RECT_F& a);
    void DrawSnippetsScreen(const D2D1_RECT_F& a);
    void DrawLogsScreen(const D2D1_RECT_F& a);
    void DrawPortForwardScreen(const D2D1_RECT_F& a);
    void DrawSshStageView(TerminalTab* tab, const D2D1_RECT_F& a);
    void DrawComingSoon(const D2D1_RECT_F& a, const wchar_t* title,
                        const wchar_t* body, const wchar_t* milestone);

    // ---- durum ----
    void OnResize();
    void SyncGridToArea();
    void SetView(View v);
    uint32_t Accent() const;
    bool ClickIn(const D2D1_RECT_F& r) const;
    bool DblClickIn(const D2D1_RECT_F& r) const;

    bool NewTab(size_t profileIndex);
    bool ConnectHost(const Host& h);
    bool QuickConnect(const std::wstring& text);
    bool LaunchNode(const ConnectionNode& node);
    // Hizli dugumler hemen; docker/wsl taramasi (probeSlow) arka is parcaciginda.
    void RefreshHubNodes(bool probeSlow = true);
    void StartHubProbe();
    bool ApplyK8sEnv(ShellProfile& p) const;   // yeni yerel terminale KUBECONFIG
    void CloseTab(size_t index);
    void SelectTab(size_t index);
    void NewSftpTab(const Host* host = nullptr);
    bool NewK8sTab();
    TerminalTab* Active();

    // Oturum Kaliciligi ve Kurtarma (Session Persistence)
    void SaveSession();
    bool RestoreSession();
    std::shared_ptr<TerminalTab> RestoreTabFromNode(const json::Value& nodeVal, int cols, int rows);
    std::unique_ptr<PaneNode> RestorePaneNode(const json::Value& nodeVal, uint32_t& maxId, int cols, int rows);

    // Tiling Split Pane destegi (Herdr mimarisi)
    void SplitActiveTab(SplitDirection dir);
    void TogglePaneZoom();
    void CloseActivePaneOrTab();
    void CyclePaneFocus(bool forward = true);
    D2D1_RECT_F GetFocusedPaneArea() const;

    // Agent Approval Ribbon / HUD (Herdr ilhamli Ajan Onay Seridi)
    void DrawAgentApprovalRibbon(TerminalTab* tab, const D2D1_RECT_F& paneArea, bool isPaneFocused, uint32_t paneId);
    bool HitAgentRibbon(int x, int y) const;
    bool ApproveAgent(TerminalTab* tab, bool approve);
    bool ApproveFocusedAgent(bool approve);
    void FixFailedCommandWithAgent();

    // Multi-Agent Swarm Workspace Presets (Herdr ilhamli Ajan Suru Alanlari)
    bool LaunchSwarmPreset(SwarmPreset preset, const std::wstring& customCwd = L"",
                           const std::vector<std::string>& initialCommands = {});
    void ShowSwarmMenu(POINT screenPt);
    void HandleMcpSwarm(const json::Value& req, json::Value& resp);

    // Synchronized Keystroke Broadcasting (Herdr ilhamli Girdi Yayini)
    void ToggleBroadcastMode();
    void BroadcastInput(const char* data, size_t len);
    void BroadcastInput(const std::string& s) { BroadcastInput(s.data(), s.size()); }
    bool IsBroadcastMode() const { return m_broadcastMode; }

    // Agent Fleet Overview Drawer / Quick Status HUD (Herdr ilhamli Ajan Filo Denetcisi)
    void ToggleAgentFleetDrawer();
    void DrawAgentFleetDrawer(const D2D1_RECT_F& a);
    bool IsAgentFleetOpen() const { return m_agentFleetOpen; }

    // Session Recording & Compliance Audit (FT-AGT-10)
    void ToggleSessionRecording();
    void HandleMcpRecord(const json::Value& req, json::Value& resp);

    // MCP GUI Bridge IPC (Herdr ilhamli Ajan Orkestrasyonu)
    LRESULT OnCopyData(HWND fromHwnd, const COPYDATASTRUCT* cds);
    void HandleMcpRequest(const json::Value& req, json::Value& resp);
    void HandleMcpList(json::Value& resp);
    void HandleMcpSplit(const json::Value& req, json::Value& resp);
    void HandleMcpPrompt(const json::Value& req, json::Value& resp);
    void HandleMcpRead(const json::Value& req, json::Value& resp);
    void HandleMcpFocus(const json::Value& req, json::Value& resp);

    void ShowProfileMenu(POINT screenPt);
    void ShowTerminalContextMenu(POINT screenPt);
    void ShowTabContextMenu(size_t tabIdx, POINT screenPt);
    void ShowSftpContextMenu(POINT screenPt, int clientX, int clientY);
    void StartRenameTab(size_t tabIdx);
    void OpenRemoteFile(const std::wstring& remoteFileName);
    void OnFilesDropped(const std::vector<std::wstring>& files, POINT pt);
    void DrawRenameTabModal();
    int  ShowListMenu(POINT screenPt, const std::vector<std::wstring>& items);

    void OnChar(wchar_t ch, bool alt);
    void OnKeyDown(WPARAM vk, bool alt);
    void CopySelection();
    void PasteClipboard();
    void ClearSelection();
    Selection ViewSelection();           // mutlak secim -> Renderer'in gorunum koordinatlari
    bool CellFromPoint(int px, int py, int& col, int& row) const;
    void CellFromPointClamped(int px, int py, int& col, int& row) const;
    bool TerminalHasKeyboard() const;
    bool DrawerUp() const { return m_drawerOpen || m_drawerProgress > 0.001f; }

    // Fare raporlama (DECSET 1000/1002/1003, SGR 1006)
    bool MouseReporting(bool shiftHeld);
    void SendMouseReport(int button, bool press, bool motion, int col, int row);
    void SendWheel(int delta, bool shiftHeld, bool ctrlHeld, POINT clientPt);
    void Toast(const std::wstring& text);

    // ---- tepsi ----
    void AddTrayIcon();
    void RemoveTrayIcon();
    void HideToTray();
    void RestoreFromTray();
    void ShowTrayMenu();

    int  HitTab(int px, int py) const;
    int  HitTabClose(int px, int py) const;
    bool HitRect(const D2D1_RECT_F& r, int px, int py) const;

    // ---- Guake / Quake acilir modu ----
    // Normal: siradan pencere. Diger durumlar Guake modunun icindedir;
    // pencere gorev cubugunda gorunmez, en ustte durur, global kisayolla iner.
    enum class QuakeState { Normal, Hidden, DroppingDown, Visible, SlidingUp };
    enum QuakeCmd : int { QC_ENTER = 1, QC_EXIT, QC_REAPPLY, QC_FULL, QC_HIDE };
    bool InQuake() const { return m_quakeState != QuakeState::Normal; }
    void PostQuake(QuakeCmd c);          // cizim ortasinda pencere degismesin diye
    void EnterQuakeMode();
    void ExitQuakeMode();
    void QuakeToggle();                  // global kisayol
    void QuakeShow();
    void QuakeHide();
    void QuakeReapply();                 // ayar degisti, animasyonsuz yeniden yerlestir
    void QuakeApplyFrame(float frac);
    void UpdateQuakeAnimation();
    RECT QuakeWorkArea() const;
    RECT QuakeRect(const RECT& work) const;
    int  QuakeGrip() const;
    bool RegisterQuakeHotkey(bool announce);
    void UnregisterQuakeHotkey();
    std::wstring HotkeyName(int vk, int mods) const;
    void ForceForeground();
    void SetCornerPreference(bool round);
    size_t DefaultProfileIndex() const;
    void DrawVisorSettings(float x0, float x1, float labelW, float rowH, float& y);

    void ApplyColorScheme();
    void ToggleSlideDrawer();
    void UpdateDrawerAnimation();
    void DrawSlideDrawer(const D2D1_RECT_F& a);

    HWND      m_hwnd = nullptr;
    HINSTANCE m_inst = nullptr;
    Renderer  m_r;
    Ui        m_ui{ m_r };
    UiInput   m_in;
    Layout    m_lay;
    UINT      m_dpi = 96;
    HICON     m_icon = nullptr;

    Settings  m_cfg;
    Inventory m_inv;
    std::wstring m_dataDir;

    std::vector<ShellProfile> m_profiles;
    std::vector<std::shared_ptr<TerminalTab>> m_tabs;
    std::vector<std::unique_ptr<PaneLayout>>  m_tabLayouts;
    size_t m_active = 0;

    View m_view = View::Terminal;
    int  m_settingsTab = 0;
    bool m_sidebarOpen = true;
    bool m_trayVisible = false;
    bool m_reallyQuit = false;

    std::wstring m_selHost, m_selIdentity;
    std::wstring m_quick;      // hizli baglanti / arama kutusu
    bool m_hostDetail = false;
    float m_sideScroll = 0.0f;
    float m_mainScroll = 0.0f;
    float m_sshLogScroll = -1.0f;
    bool  m_hostDirty = false;

    // SFTP ve KnownHosts durumlari
    KnownHostsService m_knownHosts;
    std::unique_ptr<SftpController> m_sftp;
    std::wstring m_knownHostsFilter;
    float        m_knownHostsScroll = 0.0f;
    std::wstring m_sftpLocalFilter;
    std::wstring m_sftpRemoteFilter;
    bool         m_sftpSelectingHost = false;
    std::wstring m_sftpSearchHost;
    std::wstring m_sftpSelLocal;
    std::wstring m_sftpSelRemote;
    float        m_sftpLocalScroll = 0.0f;
    float        m_sftpRemoteScroll = 0.0f;
    bool         m_sftpNewFolderPrompt = false;
    bool         m_sftpNewFolderIsRemote = false;
    std::wstring m_sftpNewFolderName;
    bool         m_sftpRenamePrompt = false;
    bool         m_sftpRenameIsRemote = false;
    std::wstring m_sftpRenameOld;
    std::wstring m_sftpRenameNew;

    // Snippet, Tunnel ve Log durumlari
    SnippetModel m_snippets;
    TunnelModel  m_tunnels{ m_inv };

    std::wstring m_snippetSearch;
    std::wstring m_snippetCategory = L"Tümü";
    float        m_snippetScroll = 0.0f;
    bool         m_snippetAddOpen = false;
    std::wstring m_snippetEditingId;
    std::wstring m_snippetNewTitle;
    std::wstring m_snippetNewCmd;
    std::wstring m_snippetNewCat = L"Özel";
    std::wstring m_snippetNewDesc;
    bool         m_snippetNewIsYaml = false;
    std::wstring m_snippetNewYaml;
    bool         m_showHostPassword = false;
    bool         m_showIdPassword = false;
    bool         m_showIdPassphrase = false;

    float        m_tunnelScroll = 0.0f;
    bool         m_tunnelAddOpen = false;
    std::wstring m_tunnelNewName;
    std::wstring m_tunnelNewHostId;
    int          m_tunnelNewType = 0;
    std::wstring m_tunnelNewLocalPort = L"8080";
    std::wstring m_tunnelNewRemoteHost = L"localhost";
    std::wstring m_tunnelNewRemotePort = L"80";

    int          m_logsTab = 0; // 0: Canli Terminal, 1: Kayitlar, 2: SSH Guvenlik
    float        m_logsScroll = 0.0f;
    std::wstring m_logsSearch;

    std::vector<ConnectionNode> m_hubNodes;    // m_fastNodes + m_slowNodes
    std::vector<ConnectionNode> m_fastNodes;
    std::vector<ConnectionNode> m_slowNodes;   // son arka plan taramasinin sonucu
    uint64_t m_hubGen = 0;                     // bayat tarama sonuclarini ayiklar
    bool     m_hubProbing = false;
    bool     m_hubProbeQueued = false;
    int64_t m_lastHubScan = 0;
    int   m_hubFilter = 0;

    int  m_hoverTab = -1;
    int  m_hoverBtn = -1;
    int  m_hoverClose = -1;
    int  m_pressClose = -1;
    size_t m_tabScroll = 0;
    std::vector<LabelCache> m_labelCache;
    bool m_focused = true;
    bool m_dirty = true;
    bool m_cursorOn = true;
    bool m_selecting = false;
    AbsSelection m_sel;

    int  m_mouseBtn = -1;                // uygulamaya raporlanan basili dugme, -1 yok
    int  m_mouseCol = -1, m_mouseRow = -1;
    int  m_wheelAccum = 0;               // WHEEL_DELTA'nin kesirleri (dokunmatik yuzey)
    int  m_wheelTarget = 0;
    bool m_closePrompting = false;

    std::wstring m_error;
    bool         m_leavePending = false; // WM_MOUSELEAVE, bekleyen tik cizilene kadar ertelendi
    std::wstring m_fontEdit;          // Ayarlar > Font ailesi alani; uygulanana kadar ayara yazilmaz
    bool         m_fontEditInit = false;
    std::wstring m_toast;
    int64_t m_toastUntil = 0;
    wchar_t m_highSurrogate = 0;
    bool m_highSurrogateAlt = false;
    bool m_swallowChar = false;

    double m_fps = 0.0;
    int64_t m_frameCount = 0;
    int64_t m_fpsStart = 0;
    uint64_t m_lastRevision = 0;

    // Guake durumu
    QuakeState m_quakeState = QuakeState::Normal;
    int64_t m_quakeAnimStart = 0;
    float   m_quakeFrom = 0.0f;       // animasyon baslangicindaki gorunur oran
    float   m_quakeTo = 0.0f;         // hedef oran: 1 tam gorunur, 0 gizli
    float   m_quakeFrac = 0.0f;       // su anki gorunur oran
    int     m_quakeVisH = -1;         // son uygulanan gorunur yukseklik (px)
    RECT    m_quakeWork{};            // inis aninda secilen calisma alani, animasyon boyunca sabit
    RECT    m_quakeRect{};            // tam gorunurken pencere dikdortgeni
    bool    m_quakeFull = false;      // F11 / buyut: gecici tam yukseklik
    bool    m_quakeResizing = false;  // alt kenardan surukleyerek yukseklik ayari
    bool    m_hotkeyOk = false;
    bool    m_hotkeyViaHook = false;  // RegisterHotKey reddetti (duz F12), LL kanca yakaliyor
    bool    m_captureHotkey = false;  // Ayarlar: "bir tusa basin" bekleniyor
    bool    m_respawnTried = false;
    int64_t m_lastAutoHide = 0;
    WINDOWPLACEMENT m_savedNormalPlacement{};
    bool    m_savedPlacementValid = false;

    bool    m_drawerOpen = false;
    float   m_drawerProgress = 0.0f;
    int64_t m_drawerAnimStart = 0;
    D2D1_RECT_F m_drawerBtn{};

    bool    m_broadcastMode = false;
    bool    m_agentFleetOpen = false;
    float   m_fleetScroll = 0.0f;

    bool        m_renamingTab = false;
    size_t      m_renameTabIdx = 0;
    std::wstring m_renameTabText;
};

} // namespace ft
