#include "ui/MainWindowInternal.h"
#include "services/AgentDetector.h"
#include "core/Utf8.h"
#include <algorithm>

namespace ft {

void MainWindow::ToggleBroadcastMode() {
    m_broadcastMode = !m_broadcastMode;
    Toast(m_broadcastMode ? Tr(Msg::BroadcastOnToast) : Tr(Msg::BroadcastOffToast));
    m_dirty = true;
}

void MainWindow::BroadcastInput(const char* data, size_t len) {
    if (!data || len == 0) return;
    if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
        for (auto& tab : m_tabLayouts[m_active]->GetAllTabs()) {
            if (tab && tab->Alive()) {
                tab->Write(data, len);
                tab->screen().ScrollViewToBottom();
            }
        }
    } else if (auto* t = Active()) {
        if (t->Alive()) {
            t->Write(data, len);
            t->screen().ScrollViewToBottom();
        }
    }
}

void MainWindow::ToggleAgentFleetDrawer() {
    m_agentFleetOpen = !m_agentFleetOpen;
    m_fleetScroll = 0.0f;
    m_dirty = true;
}

namespace {

struct AgentItem {
    size_t tabIndex = 0;
    uint32_t paneId = 1;
    std::wstring tabTitle;
    std::wstring badge;
    uint32_t accent = 0;
    std::shared_ptr<TerminalTab> tab;
    AgentDetectionResult agent;
    bool isFocused = false;
};

} // namespace

void MainWindow::DrawAgentFleetDrawer(const D2D1_RECT_F& a) {
    if (!m_agentFleetOpen) return;

    const float s = m_lay.scale;
    // Arka plani hafif karart
    m_r.Fill(a, 0x000000, 0.45f);

    const float cardW = std::min(a.right - a.left - 40.0f * s, std::floor(720.0f * s));
    const float cardH = std::min(a.bottom - a.top - 60.0f * s, std::floor(500.0f * s));
    const float cx = std::floor((a.left + a.right) * 0.5f);
    const float cy = std::floor((a.top + a.bottom) * 0.5f);

    const D2D1_RECT_F card = D2D1::RectF(cx - cardW * 0.5f, cy - cardH * 0.5f,
                                         cx + cardW * 0.5f, cy + cardH * 0.5f);

    // Kart govdesi: cam efektli koyu arayuz
    m_r.FillRound(card, std::floor(12.0f * s), 0x131822, 0.98f);
    m_r.Stroke(card, m_broadcastMode ? theme::AcHi() : 0x2B3545, std::floor(1.5f * s));

    // 1. Baslik Alani
    const float headerH = std::floor(56.0f * s);
    const D2D1_RECT_F headerR = D2D1::RectF(card.left, card.top, card.right, card.top + headerH);
    m_r.FillRound(headerR, std::floor(12.0f * s), 0x181F2C, 1.0f);
    // Alt koseleri duzelt
    m_r.Fill(D2D1::RectF(card.left, card.top + headerH - std::floor(10.0f * s), card.right, card.top + headerH), 0x181F2C);
    m_r.Line(card.left, card.top + headerH, card.right, card.top + headerH, 0x2B3545, 1.0f);

    // Sol Baslik Metni
    m_r.Text(L"⚡ AI AGENT FLEET CONTROLLER",
             D2D1::RectF(card.left + 16.0f * s, card.top + 10.0f * s, card.right - 220.0f * s, card.top + 30.0f * s),
             theme::AcHi(), 14.0f * s, Renderer::Align::Left, true);

    // Tum ajanlari topla
    std::vector<AgentItem> agents;
    int blockedCount = 0;
    int workingCount = 0;
    int idleCount = 0;

    for (size_t ti = 0; ti < m_tabs.size(); ++ti) {
        if (!m_tabs[ti]) continue;
        const std::wstring title = m_tabs[ti]->Title();
        const std::wstring badge = m_tabs[ti]->profile().badge;
        const uint32_t ac = m_tabs[ti]->profile().accent ? m_tabs[ti]->profile().accent : theme::Ac();

        if (ti < m_tabLayouts.size() && m_tabLayouts[ti]) {
            auto panes = m_tabLayouts[ti]->ComputeLayout(m_lay.term);
            for (const auto& pane : panes) {
                if (!pane.tab) continue;
                AgentItem it;
                it.tabIndex = ti;
                it.paneId = pane.id;
                it.tabTitle = title;
                it.badge = badge;
                it.accent = ac;
                it.tab = pane.tab;
                it.agent = pane.tab->AgentStatus();
                it.isFocused = (ti == m_active && pane.isFocused);
                if (it.agent.isBlocked()) blockedCount++;
                else if (it.agent.isWorking()) workingCount++;
                else idleCount++;
                agents.push_back(it);
            }
        } else {
            AgentItem it;
            it.tabIndex = ti;
            it.paneId = 1;
            it.tabTitle = title;
            it.badge = badge;
            it.accent = ac;
            it.tab = m_tabs[ti];
            it.agent = m_tabs[ti]->AgentStatus();
            it.isFocused = (ti == m_active);
            if (it.agent.isBlocked()) blockedCount++;
            else if (it.agent.isWorking()) workingCount++;
            else idleCount++;
            agents.push_back(it);
        }
    }

    std::wstring subSummary = std::to_wstring(agents.size()) + L" Ajan Aktif";
    if (blockedCount > 0) subSummary += L" | " + std::to_wstring(blockedCount) + L" Onay Bekliyor";
    if (workingCount > 0) subSummary += L" | " + std::to_wstring(workingCount) + L" Çalışıyor";
    if (idleCount > 0)    subSummary += L" | " + std::to_wstring(idleCount) + L" Boşta";

    m_r.Text(subSummary,
             D2D1::RectF(card.left + 16.0f * s, card.top + 32.0f * s, card.right - 220.0f * s, card.top + 50.0f * s),
             blockedCount > 0 ? 0xFF6B6B : theme::TextDim, 11.0f * s, Renderer::Align::Left);

    // Sag: Broadcast Mode Toggle Butonu
    const float bBtnW = std::floor(140.0f * s);
    const float bBtnH = std::floor(28.0f * s);
    const D2D1_RECT_F bBtnR = D2D1::RectF(card.right - 180.0f * s, card.top + 14.0f * s,
                                         card.right - 180.0f * s + bBtnW, card.top + 14.0f * s + bBtnH);
    const std::wstring bText = m_broadcastMode ? L"📡 Yayın: AÇIK" : L"📡 Yayın: KAPALI";
    if (m_ui.Button(7500, bBtnR, bText, m_broadcastMode, false)) {
        ToggleBroadcastMode();
    }

    // Sag: Kapat Butonu [✕]
    const D2D1_RECT_F closeBtnR = D2D1::RectF(card.right - 36.0f * s, card.top + 14.0f * s,
                                             card.right - 12.0f * s, card.top + 14.0f * s + bBtnH);
    if (m_ui.Button(7501, closeBtnR, L"✕", false, false)) {
        m_agentFleetOpen = false;
        m_dirty = true;
    }

    // 2. Ajan Listesi (Govde)
    const float listTop = card.top + headerH + 8.0f * s;
    const float listBottom = card.bottom - 12.0f * s;
    const D2D1_RECT_F listArea = D2D1::RectF(card.left + 12.0f * s, listTop, card.right - 12.0f * s, listBottom);

    m_r.PushClip(listArea);

    const float itemH = std::floor(62.0f * s);
    const float itemGap = std::floor(6.0f * s);
    float curY = listTop - m_fleetScroll;

    for (size_t i = 0; i < agents.size(); ++i) {
        const auto& ag = agents[i];
        const D2D1_RECT_F itemR = D2D1::RectF(listArea.left, curY, listArea.right, curY + itemH);

        if (itemR.bottom >= listArea.top && itemR.top <= listArea.bottom) {
            // Satir kutusu
            const uint32_t bgCol = ag.isFocused ? 0x1F2937 : 0x161C26;
            m_r.FillRound(itemR, std::floor(8.0f * s), bgCol, 0.95f);

            uint32_t borderCol = 0x2A3444;
            if (ag.agent.isBlocked()) {
                borderCol = 0xFF4D4D;
            } else if (ag.isFocused) {
                borderCol = theme::Ac();
            }
            m_r.Stroke(itemR, borderCol, std::floor(1.2f * s));

            // Sol rozet: Sekme / Panel
            std::wstring tabLoc = L"#" + std::to_wstring(ag.tabIndex + 1) + L"." + std::to_wstring(ag.paneId);
            const D2D1_RECT_F locR = D2D1::RectF(itemR.left + 10.0f * s, itemR.top + 8.0f * s,
                                                itemR.left + 64.0f * s, itemR.top + 28.0f * s);
            m_r.FillRound(locR, std::floor(4.0f * s), 0x242D3D, 0.8f);
            m_r.Text(tabLoc, locR, theme::TextHi, 11.0f * s, Renderer::Align::Center, true);

            // Ajan Turu ve Baslik
            std::wstring agentName;
            switch (ag.agent.kind) {
            case AgentKind::Antigravity: agentName = L"Antigravity Agent"; break;
            case AgentKind::Claude:      agentName = L"Claude Code"; break;
            case AgentKind::Cursor:      agentName = L"Cursor Agent"; break;
            case AgentKind::Codex:       agentName = L"Codex Agent"; break;
            case AgentKind::Gemini:      agentName = L"Gemini CLI"; break;
            default:                     agentName = ag.badge.empty() ? L"Terminal Session" : ag.badge; break;
            }

            m_r.Text(agentName + L" (" + ag.tabTitle + L")",
                     D2D1::RectF(itemR.left + 72.0f * s, itemR.top + 8.0f * s, itemR.right - 220.0f * s, itemR.top + 28.0f * s),
                     theme::TextHi, 12.5f * s, Renderer::Align::Left, true);

            // Detay / Kural aciklamasi
            std::wstring detail = ag.agent.detail.empty() ? (ag.agent.ruleId.empty() ? L"Komut satırı hazır" : Utf8ToWide(ag.agent.ruleId))
                                                          : Utf8ToWide(ag.agent.detail);
            if (detail.size() > 60) detail = detail.substr(0, 57) + L"...";

            m_r.Text(detail,
                     D2D1::RectF(itemR.left + 72.0f * s, itemR.top + 32.0f * s, itemR.right - 220.0f * s, itemR.top + 52.0f * s),
                     theme::TextDim, 11.0f * s, Renderer::Align::Left);

            // Durum Rozeti (Sagda)
            const float stW = std::floor(110.0f * s);
            const float stH = std::floor(24.0f * s);
            const D2D1_RECT_F stR = D2D1::RectF(itemR.right - 215.0f * s, itemR.top + 19.0f * s,
                                               itemR.right - 215.0f * s + stW, itemR.top + 19.0f * s + stH);

            if (ag.agent.isBlocked()) {
                m_r.FillRound(stR, std::floor(4.0f * s), 0xFF3D3D, 0.25f);
                m_r.Stroke(stR, 0xFF4D4D, 1.0f);
                m_r.Text(L"🔴 BEKLİYOR", stR, 0xFF6B6B, 10.5f * s, Renderer::Align::Center, true);
            } else if (ag.agent.isWorking()) {
                m_r.FillRound(stR, std::floor(4.0f * s), theme::Ac(), 0.20f);
                m_r.Stroke(stR, theme::AcHi(), 1.0f);
                m_r.Text(L"🟢 ÇALIŞIYOR", stR, theme::AcHi(), 10.5f * s, Renderer::Align::Center, true);
            } else {
                m_r.FillRound(stR, std::floor(4.0f * s), 0x334155, 0.30f);
                m_r.Text(L"⚪ BOŞTA", stR, theme::TextMuted, 10.5f * s, Renderer::Align::Center);
            }

            // Eylem Butonlari
            const int baseBtnId = 7600 + static_cast<int>(i) * 4;
            const float btnW = std::floor(44.0f * s);
            const float btnH = std::floor(26.0f * s);
            const float by = itemR.top + 18.0f * s;

            // [↗ Odaklan] Butonu
            const D2D1_RECT_F focusBtnR = D2D1::RectF(itemR.right - 95.0f * s, by, itemR.right - 95.0f * s + btnW, by + btnH);
            if (m_ui.Button(baseBtnId, focusBtnR, L"↗ Git", false, false)) {
                SelectTab(ag.tabIndex);
                if (ag.tabIndex < m_tabLayouts.size() && m_tabLayouts[ag.tabIndex]) {
                    m_tabLayouts[ag.tabIndex]->SetFocusedPane(ag.paneId);
                    m_tabs[ag.tabIndex] = m_tabLayouts[ag.tabIndex]->GetFocusedTab();
                }
                m_agentFleetOpen = false;
                m_dirty = true;
            }

            // Eger Ajan onay bekliyorsa [✓] ve [✕] butonlari
            if (ag.agent.isBlocked()) {
                const D2D1_RECT_F okBtnR = D2D1::RectF(itemR.right - 46.0f * s, by, itemR.right - 46.0f * s + btnW * 0.9f, by + btnH);
                if (m_ui.Button(baseBtnId + 1, okBtnR, L"✓ Onay", true, false)) {
                    ApproveAgent(ag.tab.get(), true);
                }
            }
        }
        curY += itemH + itemGap;
    }

    m_r.PopClip();
}

} // namespace ft
