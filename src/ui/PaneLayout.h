#pragma once
//
// FullTerminal — BSP Tree Tiling Split Pane Layout Engine
// Herdr layout.rs mimarisinden ilham alinan, sekmeler icinde
// sinirsiz yatay/dikey panel bolme (tiling) ve odak yonetimi saglayan motor.
//

#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include "core/Platform.h"

namespace ft {

class TerminalTab;

enum class SplitDirection {
    Vertical,   // Bolme cizgisi dikey: Sol ve Sag (Ctrl+Shift+D)
    Horizontal  // Bolme cizgisi yatay: Ust ve Alt (Ctrl+Shift+E)
};

struct PaneInfo {
    uint32_t id = 0;
    D2D1_RECT_F area{};
    std::shared_ptr<TerminalTab> tab;
    bool isFocused = false;
};

class PaneNode {
public:
    uint32_t id = 0;
    bool isSplit = false;
    SplitDirection dir = SplitDirection::Vertical;
    float ratio = 0.5f; // Ilk cocugun orani (0.1f .. 0.9f)

    std::unique_ptr<PaneNode> first;
    std::unique_ptr<PaneNode> second;

    // Eger yaprak ise terminal oturumu:
    std::shared_ptr<TerminalTab> tab;

    PaneNode() = default;
    PaneNode(uint32_t nid, std::shared_ptr<TerminalTab> t)
        : id(nid), isSplit(false), tab(std::move(t)) {}

    bool IsLeaf() const { return !isSplit; }
};

class PaneLayout {
public:
    PaneLayout();
    explicit PaneLayout(std::shared_ptr<TerminalTab> rootTab);

    // Aktif paneli dikey veya yatay olarak ikiye boler
    bool SplitActive(SplitDirection dir, std::shared_ptr<TerminalTab> newTab);

    // Belirli bir paneli veya aktif paneli kapatir. Kalan son panel kapatilamaz (false doner).
    bool CloseActive();
    bool ClosePane(uint32_t paneId);

    // Zoom modunu degistirir (tek paneli tam ekran yap / geri al)
    void ToggleZoom();
    bool IsZoomed() const { return m_zoomed; }

    // Geometri hesaplama: verilen terminal alanini panellere bolusturur
    std::vector<PaneInfo> ComputeLayout(const D2D1_RECT_F& totalArea);

    // Odak yonetimi
    void SetFocusedPane(uint32_t paneId);
    uint32_t GetFocusedPaneId() const { return m_focusedId; }
    std::shared_ptr<TerminalTab> GetFocusedTab() const;
    std::shared_ptr<TerminalTab> GetPrimaryTab() const;
    void CycleFocus(bool forward = true);

    // Sekme duzeyinde kullanici tarafindan verilen ozel baslik (split olsa dahi korunur)
    void SetCustomTitle(const std::wstring& title) { m_customTitle = title; }
    const std::wstring& CustomTitle() const { return m_customTitle; }

    // Sekmeye gore panel kimligini bulur
    uint32_t FindPaneIdByTab(const TerminalTab* tab) const;

    // Belirli bir noktadaki (px, py) paneli bulur
    uint32_t HitTestPane(const std::vector<PaneInfo>& panes, float px, float py) const;

    // Tum panellerdeki sekmeleri gezer
    std::vector<std::shared_ptr<TerminalTab>> GetAllTabs() const;

    size_t PaneCount() const;

    const PaneNode* Root() const { return m_root.get(); }
    void SetRoot(std::unique_ptr<PaneNode> root, uint32_t focusedId, uint32_t nextId);

private:
    std::unique_ptr<PaneNode> m_root;
    uint32_t m_focusedId = 1;
    uint32_t m_nextId = 2;
    bool m_zoomed = false;
    std::wstring m_customTitle;

    PaneNode* FindNode(PaneNode* node, uint32_t id);
    PaneNode* FindParent(PaneNode* root, uint32_t childId);
    void CollectPanes(PaneNode* node, const D2D1_RECT_F& area, std::vector<PaneInfo>& out);
    void CollectTabs(PaneNode* node, std::vector<std::shared_ptr<TerminalTab>>& out) const;
    size_t CountLeaves(PaneNode* node) const;
};

} // namespace ft
