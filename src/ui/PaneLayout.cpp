#include "ui/PaneLayout.h"
#include <algorithm>

namespace ft {

PaneLayout::PaneLayout() {
    m_root = std::make_unique<PaneNode>(1, nullptr);
    m_focusedId = 1;
    m_nextId = 2;
}

PaneLayout::PaneLayout(std::shared_ptr<TerminalTab> rootTab) {
    m_root = std::make_unique<PaneNode>(1, std::move(rootTab));
    m_focusedId = 1;
    m_nextId = 2;
}

bool PaneLayout::SplitActive(SplitDirection dir, std::shared_ptr<TerminalTab> newTab) {
    if (!m_root) return false;

    PaneNode* target = FindNode(m_root.get(), m_focusedId);
    if (!target || !target->IsLeaf()) return false;

    // Hedef yapragi ikiye bol:
    // Mevcut sekmeyi 'first' yaprağına tasi, yeni sekmeyi 'second' yaprağına koy.
    auto oldTab = std::move(target->tab);
    uint32_t firstId = m_nextId++;
    uint32_t secondId = m_nextId++;

    target->isSplit = true;
    target->dir = dir;
    target->ratio = 0.5f;
    target->first = std::make_unique<PaneNode>(firstId, std::move(oldTab));
    target->second = std::make_unique<PaneNode>(secondId, std::move(newTab));

    // Odagi yeni acilan panele ver
    m_focusedId = secondId;
    m_zoomed = false;
    return true;
}

bool PaneLayout::CloseActive() {
    return ClosePane(m_focusedId);
}

bool PaneLayout::ClosePane(uint32_t paneId) {
    if (!m_root || m_root->IsLeaf()) {
        // Tek panel varken split panel kapatilamaz
        return false;
    }

    PaneNode* parent = FindParent(m_root.get(), paneId);
    if (!parent || !parent->isSplit) return false;

    // Kardes dugumu bul
    std::unique_ptr<PaneNode> sibling;
    if (parent->first && parent->first->id == paneId) {
        sibling = std::move(parent->second);
    } else if (parent->second && parent->second->id == paneId) {
        sibling = std::move(parent->first);
    } else {
        return false;
    }

    // Kardes dugum ebeveynin yerini alir (Ağaç daralması)
    parent->isSplit = sibling->isSplit;
    parent->dir = sibling->dir;
    parent->ratio = sibling->ratio;
    parent->id = sibling->id;
    parent->tab = std::move(sibling->tab);
    parent->first = std::move(sibling->first);
    parent->second = std::move(sibling->second);

    // Kapatilan panel odaktaysa, kardes dugume odaklan
    if (m_focusedId == paneId) {
        m_focusedId = parent->id;
    }
    m_zoomed = false;
    return true;
}

void PaneLayout::ToggleZoom() {
    if (PaneCount() > 1) {
        m_zoomed = !m_zoomed;
    } else {
        m_zoomed = false;
    }
}

std::vector<PaneInfo> PaneLayout::ComputeLayout(const D2D1_RECT_F& totalArea) {
    std::vector<PaneInfo> result;
    if (!m_root) return result;

    if (m_zoomed) {
        PaneNode* focused = FindNode(m_root.get(), m_focusedId);
        if (focused && focused->IsLeaf()) {
            result.push_back({ focused->id, totalArea, focused->tab, true });
            return result;
        }
    }

    CollectPanes(m_root.get(), totalArea, result);
    return result;
}

void PaneLayout::CollectPanes(PaneNode* node, const D2D1_RECT_F& area, std::vector<PaneInfo>& out) {
    if (!node) return;

    if (node->IsLeaf()) {
        out.push_back({ node->id, area, node->tab, node->id == m_focusedId });
        return;
    }

    constexpr float kGap = 3.0f; // Paneller arasi ayirici bosluk
    const float w = area.right - area.left;
    const float h = area.bottom - area.top;
    const float r = std::clamp(node->ratio, 0.1f, 0.9f);

    if (node->dir == SplitDirection::Vertical) {
        float splitX = area.left + w * r;
        D2D1_RECT_F leftArea = D2D1::RectF(area.left, area.top, splitX - kGap * 0.5f, area.bottom);
        D2D1_RECT_F rightArea = D2D1::RectF(splitX + kGap * 0.5f, area.top, area.right, area.bottom);
        CollectPanes(node->first.get(), leftArea, out);
        CollectPanes(node->second.get(), rightArea, out);
    } else {
        float splitY = area.top + h * r;
        D2D1_RECT_F topArea = D2D1::RectF(area.left, area.top, area.right, splitY - kGap * 0.5f);
        D2D1_RECT_F bottomArea = D2D1::RectF(area.left, splitY + kGap * 0.5f, area.right, area.bottom);
        CollectPanes(node->first.get(), topArea, out);
        CollectPanes(node->second.get(), bottomArea, out);
    }
}

void PaneLayout::SetFocusedPane(uint32_t paneId) {
    PaneNode* node = FindNode(m_root.get(), paneId);
    if (node && node->IsLeaf()) {
        m_focusedId = paneId;
    }
}

std::shared_ptr<TerminalTab> PaneLayout::GetFocusedTab() const {
    if (!m_root) return nullptr;
    const PaneNode* node = const_cast<PaneLayout*>(this)->FindNode(m_root.get(), m_focusedId);
    if (node && node->IsLeaf()) {
        return node->tab;
    }
    // Geri donus: ilk bulunan yaprak
    std::vector<std::shared_ptr<TerminalTab>> tabs = GetAllTabs();
    return tabs.empty() ? nullptr : tabs.front();
}

void PaneLayout::CycleFocus(bool forward) {
    auto panes = const_cast<PaneLayout*>(this)->ComputeLayout(D2D1::RectF(0, 0, 1000, 1000));
    if (panes.size() <= 1) return;
    for (size_t i = 0; i < panes.size(); ++i) {
        if (panes[i].id == m_focusedId) {
            size_t next = forward ? (i + 1) % panes.size() : (i + panes.size() - 1) % panes.size();
            SetFocusedPane(panes[next].id);
            return;
        }
    }
}

uint32_t PaneLayout::FindPaneIdByTab(const TerminalTab* tab) const {
    if (!tab || !m_root) return 0;
    auto search = [&](auto& self, PaneNode* n) -> uint32_t {
        if (!n) return 0;
        if (n->IsLeaf()) {
            return (n->tab.get() == tab) ? n->id : 0;
        }
        uint32_t r = self(self, n->first.get());
        if (r) return r;
        return self(self, n->second.get());
    };
    return search(search, m_root.get());
}

uint32_t PaneLayout::HitTestPane(const std::vector<PaneInfo>& panes, float px, float py) const {
    for (const auto& pi : panes) {
        if (px >= pi.area.left && px <= pi.area.right &&
            py >= pi.area.top && py <= pi.area.bottom) {
            return pi.id;
        }
    }
    return 0;
}

std::vector<std::shared_ptr<TerminalTab>> PaneLayout::GetAllTabs() const {
    std::vector<std::shared_ptr<TerminalTab>> res;
    CollectTabs(m_root.get(), res);
    return res;
}

void PaneLayout::CollectTabs(PaneNode* node, std::vector<std::shared_ptr<TerminalTab>>& out) const {
    if (!node) return;
    if (node->IsLeaf()) {
        if (node->tab) out.push_back(node->tab);
        return;
    }
    CollectTabs(node->first.get(), out);
    CollectTabs(node->second.get(), out);
}

size_t PaneLayout::PaneCount() const {
    return CountLeaves(m_root.get());
}

size_t PaneLayout::CountLeaves(PaneNode* node) const {
    if (!node) return 0;
    if (node->IsLeaf()) return 1;
    return CountLeaves(node->first.get()) + CountLeaves(node->second.get());
}

PaneNode* PaneLayout::FindNode(PaneNode* node, uint32_t id) {
    if (!node) return nullptr;
    if (node->id == id && node->IsLeaf()) return node;
    if (node->isSplit) {
        PaneNode* f = FindNode(node->first.get(), id);
        if (f) return f;
        return FindNode(node->second.get(), id);
    }
    return (node->id == id) ? node : nullptr;
}

PaneNode* PaneLayout::FindParent(PaneNode* root, uint32_t childId) {
    if (!root || !root->isSplit) return nullptr;
    if ((root->first && root->first->id == childId) ||
        (root->second && root->second->id == childId)) {
        return root;
    }
    PaneNode* p = FindParent(root->first.get(), childId);
    if (p) return p;
    return FindParent(root->second.get(), childId);
}

void PaneLayout::SetRoot(std::unique_ptr<PaneNode> root, uint32_t focusedId, uint32_t nextId) {
    m_root = std::move(root);
    m_focusedId = focusedId;
    m_nextId = nextId;
}

} // namespace ft

