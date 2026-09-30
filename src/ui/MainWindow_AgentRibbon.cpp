#include "ui/MainWindowInternal.h"
#include "core/Utf8.h"

namespace ft {

namespace {

D2D1_RECT_F ComputeRibbonRect(const D2D1_RECT_F& paneArea, float scale) {
    const float s = (scale > 0.1f) ? scale : 1.0f;
    const float ribbonH = 38.0f * s;
    const float margin = 6.0f * s;

    float top = paneArea.bottom - ribbonH - margin;
    if (top < paneArea.top + 20.0f * s) {
        top = paneArea.top + 2.0f * s;
    }
    return D2D1::RectF(paneArea.left + margin, top, paneArea.right - margin, paneArea.bottom - margin);
}

} // namespace

void MainWindow::DrawAgentApprovalRibbon(TerminalTab* tab, const D2D1_RECT_F& paneArea,
                                        bool isPaneFocused, uint32_t paneId) {
    if (!tab) return;

    const float s = m_lay.scale;
    const D2D1_RECT_F ribbon = ComputeRibbonRect(paneArea, s);
    if (ribbon.right <= ribbon.left || ribbon.bottom <= ribbon.top) return;

    if (tab->AgentStatus().isBlocked()) {
        // Koyu saydam uyari cami arka plani
        m_r.Fill(ribbon, 0x1A1215, 0.94f);

        // Nabiz atan parlak kirmizi kenarlik
        const int64_t ticks = NowTicks();
        const double sec = static_cast<double>(ticks) / static_cast<double>(TickFreq());
        const float pulse = static_cast<float>(0.5 + 0.5 * std::sin(sec * 4.0));
        const uint32_t borderCol = isPaneFocused ? 0xFF2A40 : 0xFF5252;
        const float borderTh = isPaneFocused ? (1.5f + pulse * 1.0f) : 1.2f;
        m_r.Stroke(ribbon, borderCol, borderTh);

        // Sol: Kirmizi uyari rozeti
        const float badgeW = 120.0f * s;
        const D2D1_RECT_F badgeRect = D2D1::RectF(
            ribbon.left + 6.0f * s, ribbon.top + 5.0f * s,
            ribbon.left + 6.0f * s + badgeW, ribbon.bottom - 5.0f * s);
        m_r.Fill(badgeRect, 0xE53935, 0.90f);
        m_r.Text(L"🔴 ONAY BEKLİYOR", badgeRect, 0xFFFFFF, 10.0f * s, Renderer::Align::Center);

        // Sag: Butonlar
        const float btnW = 92.0f * s;
        const float btnH = ribbon.bottom - ribbon.top - 10.0f * s;
        const float btnTop = ribbon.top + 5.0f * s;

        // 1. Reddet butonu (en sagda)
        float curRight = ribbon.right - 6.0f * s;
        const D2D1_RECT_F denyRect = D2D1::RectF(curRight - btnW, btnTop, curRight, btnTop + btnH);
        curRight -= (btnW + 6.0f * s);

        // 2. Onayla butonu (reddetin solunda)
        const float approveW = 104.0f * s;
        const D2D1_RECT_F approveRect = D2D1::RectF(curRight - approveW, btnTop, curRight, btnTop + btnH);
        curRight -= (approveW + 6.0f * s);

        // 3. Odaklan butonu (yer varsa ve panel odaksizsa)
        D2D1_RECT_F focusRect{};
        const bool showFocus = !isPaneFocused && (curRight - ribbon.left > 240.0f * s);
        if (showFocus) {
            const float fW = 76.0f * s;
            focusRect = D2D1::RectF(curRight - fW, btnTop, curRight, btnTop + btnH);
            curRight -= (fW + 6.0f * s);
        }

        // Orta: Ajan ve kural aciklama metni
        const D2D1_RECT_F descRect = D2D1::RectF(
            badgeRect.right + 10.0f * s, ribbon.top + 2.0f * s,
            curRight - 4.0f * s, ribbon.bottom - 2.0f * s);
        if (descRect.right > descRect.left) {
            std::wstring desc;
            const auto& st = tab->AgentStatus();
            if (st.kind == AgentKind::Claude) desc = L"Claude Code izin istiyor";
            else if (st.kind == AgentKind::Antigravity) desc = L"Antigravity izin istiyor";
            else if (st.kind == AgentKind::Codex) desc = L"Codex onay istiyor";
            else desc = L"Ajan izin istiyor";

            if (!st.detail.empty()) {
                desc += L": \"" + Trunc(Utf8ToWide(st.detail), 40) + L"\"";
            }
            m_r.Text(desc, descRect, 0xF0F0F0, 11.0f * s, Renderer::Align::Left);
        }

        // Interaktif buton tiklamalari
        const int baseId = ID_AGENT_APPROVE + static_cast<int>((paneId % 50) * 4);
        if (m_ui.Button(baseId, approveRect, L"✓ Onayla", true, false)) {
            ApproveAgent(tab, true);
        }
        if (m_ui.Button(baseId + 1, denyRect, L"✕ Reddet", false, true)) {
            ApproveAgent(tab, false);
        }
        if (showFocus && m_ui.Button(baseId + 2, focusRect, L"⌨ Odaklan", false, false)) {
            if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
                m_tabLayouts[m_active]->SetFocusedPane(paneId);
                m_tabs[m_active] = m_tabLayouts[m_active]->GetFocusedTab();
                m_dirty = true;
            }
        }
    } else if (tab->screen().LastCommandFailed()) {
        // OSC 133: Komut Hatasi Seridi (Semantic Failure HUD)
        m_r.Fill(ribbon, 0x1E1418, 0.94f);
        m_r.Stroke(ribbon, 0xDC4040, 1.2f);

        // Sol: Hata rozeti
        const float badgeW = 98.0f * s;
        const D2D1_RECT_F badgeRect = D2D1::RectF(
            ribbon.left + 6.0f * s, ribbon.top + 5.0f * s,
            ribbon.left + 6.0f * s + badgeW, ribbon.bottom - 5.0f * s);
        m_r.Fill(badgeRect, 0xC62828, 0.90f);
        m_r.Text(L"⚠️ HATA: " + std::to_wstring(tab->screen().LastExitCode()), badgeRect, 0xFFFFFF, 10.0f * s, Renderer::Align::Center);

        // Sag: Butonlar
        const float btnH = ribbon.bottom - ribbon.top - 10.0f * s;
        const float btnTop = ribbon.top + 5.0f * s;

        // 1. Kapat butonu (en sagda)
        const float closeW = 32.0f * s;
        float curRight = ribbon.right - 6.0f * s;
        const D2D1_RECT_F closeRect = D2D1::RectF(curRight - closeW, btnTop, curRight, btnTop + btnH);
        curRight -= (closeW + 6.0f * s);

        // 2. Ajan ile Duzelt butonu
        const float fixW = 186.0f * s;
        const D2D1_RECT_F fixRect = D2D1::RectF(curRight - fixW, btnTop, curRight, btnTop + btnH);
        curRight -= (fixW + 6.0f * s);

        // Orta: Hata aciklama satiri
        const D2D1_RECT_F descRect = D2D1::RectF(
            badgeRect.right + 10.0f * s, ribbon.top + 2.0f * s,
            curRight - 4.0f * s, ribbon.bottom - 2.0f * s);
        if (descRect.right > descRect.left) {
            std::string out = tab->screen().GetLastFailedCommandOutput();
            std::wstring desc = L"Komut basarisiz: " + Trunc(Utf8ToWide(out), 40);
            m_r.Text(desc, descRect, 0xEEEEEE, 11.0f * s, Renderer::Align::Left);
        }

        const int baseId = ID_AGENT_APPROVE + static_cast<int>((paneId % 50) * 4);
        if (m_ui.Button(baseId, fixRect, L"⚡ Ajan ile Düzelt (Ctrl+Shift+F)", true, false)) {
            FixFailedCommandWithAgent();
        }
        if (m_ui.Button(baseId + 1, closeRect, L"✕", false, false)) {
            tab->screen().DismissCommandFailure();
            m_dirty = true;
        }
    }
}

bool MainWindow::HitAgentRibbon(int x, int y) const {
    if (m_view != View::Terminal) return false;

    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);

    if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
        auto panes = m_tabLayouts[m_active]->ComputeLayout(m_lay.term);
        for (const auto& pane : panes) {
            if (!pane.tab) continue;
            if (!pane.tab->AgentStatus().isBlocked() && !pane.tab->screen().LastCommandFailed()) continue;
            const D2D1_RECT_F r = ComputeRibbonRect(pane.area, m_lay.scale);
            if (fx >= r.left && fx <= r.right && fy >= r.top && fy <= r.bottom) {
                return true;
            }
        }
    } else if (auto* t = const_cast<MainWindow*>(this)->Active()) {
        if (t->AgentStatus().isBlocked() || t->screen().LastCommandFailed()) {
            const D2D1_RECT_F r = ComputeRibbonRect(m_lay.term, m_lay.scale);
            if (fx >= r.left && fx <= r.right && fy >= r.top && fy <= r.bottom) {
                return true;
            }
        }
    }

    return false;
}

bool MainWindow::ApproveAgent(TerminalTab* tab, bool approve) {
    if (!tab || !tab->AgentStatus().isBlocked()) return false;

    const auto& st = tab->AgentStatus();
    std::string keystrokes;

    if (st.ruleId == "claude_live_blocked_form") {
        // Claude Code seçim/onay diyalogu (Enter to confirm, Esc to cancel)
        keystrokes = approve ? "\r\n" : "\x1b";
    } else if (st.ruleId == "generic_prompt_blocked" && st.detail.find("press enter") != std::string::npos) {
        // "press enter to continue"
        keystrokes = approve ? "\r\n" : "\x03"; // Ctrl+C
    } else {
        // Varsayilan y/n veya Antigravity/Claude izin onaylari
        keystrokes = approve ? "y\r\n" : "n\r\n";
    }

    tab->Write(keystrokes.data(), keystrokes.size());

    if (approve) {
        Toast(L"Ajan eylemi onaylandi (✓)");
    } else {
        Toast(L"Ajan eylemi reddedildi (✕)");
    }

    m_dirty = true;
    return true;
}

bool MainWindow::ApproveFocusedAgent(bool approve) {
    TerminalTab* t = Active();
    if (t && t->AgentStatus().isBlocked()) {
        return ApproveAgent(t, approve);
    }
    return false;
}

void MainWindow::FixFailedCommandWithAgent() {
    TerminalTab* t = Active();
    if (!t) return;
    int exitCode = t->screen().LastExitCode();
    std::string err = t->screen().GetLastFailedCommandOutput();
    t->screen().DismissCommandFailure();

    std::string prompt = "Terminal komutu basarisiz oldu (Exit Code: " + std::to_string(exitCode) + ").\n"
                         "Hata ciktisi:\n```\n" + err + "\n```\n"
                         "Lutfen hatanin nedenini analiz et ve cozumu oner / komutu duzelt.";
    ClipboardSetText(m_hwnd, Utf8ToWide(prompt));
    Toast(L"Hata istemi panoya kopyalandı (Kod: " + std::to_wstring(exitCode) + L")");
    m_dirty = true;
}

} // namespace ft
