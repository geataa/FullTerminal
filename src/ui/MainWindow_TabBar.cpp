#include "ui/MainWindowInternal.h"

namespace ft {

void MainWindow::DrawChrome() {
    const Layout& L = m_lay;
    m_r.Fill(L.title, theme::Surface);
    m_r.Fill(D2D1::RectF(L.title.left, L.title.bottom - 1, L.title.right, L.title.bottom), theme::Border);

    uint32_t rail = theme::Ac();
    m_r.Fill(L.rail, rail);

    const float s = L.scale;
    const bool hotMenu = (m_hoverBtn == BTN_MENU);
    if (hotMenu && L.newTab.bottom > L.newTab.top) {
        const D2D1_RECT_F mr = D2D1::RectF(L.menuBtn.left + std::floor(4 * s), L.newTab.top,
                                           L.menuBtn.right - std::floor(4 * s), L.newTab.bottom);
        m_r.FillRound(mr, std::floor((mr.bottom - mr.top) * 0.5f), theme::Elevated);
    }
    const float mx = (L.menuBtn.left + L.menuBtn.right) * 0.5f;
    const float my = (L.menuBtn.top + L.menuBtn.bottom) * 0.5f;
    const uint32_t mc = hotMenu ? theme::TextHi : theme::TextMuted;
    const float lw = std::max(1.0f, std::floor(1.4f * s));
    for (int i = -1; i <= 1; ++i) {
        m_r.Line(mx - 6 * s, my + i * 5 * s, mx + 6 * s, my + i * 5 * s, mc, lw);
    }

    DrawTabStrip();
    DrawWindowButtons();
}

void MainWindow::DrawTabStrip() {
    const Layout& L = m_lay;
    const float s = L.scale;
    auto dp = [s](float v) { return std::floor(v * s); };

    const float padL     = dp(12.0f);
    const float padR     = dp(10.0f);
    const float badgeGap = dp(8.0f);
    const float closeGap = dp(6.0f);
    const float fBadge   = 9.5f * s;
    const float fTitle   = 12.0f * s;

    for (size_t i = 0; i < L.tabs.size(); ++i) {
        const size_t ti = L.tabFirst + i;
        if (ti >= m_tabs.size() || !m_tabs[ti]) continue;

        const D2D1_RECT_F& r = L.tabs[i];
        const float w = r.right - r.left;
        if (w <= 1.0f) continue;

        const float rad = std::floor((r.bottom - r.top) * 0.5f);  // tam kapsul
        const bool cur   = (ti == m_active);
        const bool live  = cur && (m_view == View::Terminal);
        const bool hover = ((int)ti == m_hoverTab);

        const ShellProfile& p = m_tabs[ti]->profile();
        const uint32_t userAc = theme::Ac();
        const auto& agent = m_tabs[ti]->GetAgentStatus();
        const bool isBlocked = agent.isBlocked();
        const bool isWorking = agent.isWorking();

        // Govde: uygulamanin kendi kapsul formulu (Ayarlar kategori seridiyle ayni).
        if (isBlocked) {
            // Ajan onay bekliyor: dikkat ceken kirmizi/turuncu vurus
            m_r.FillRound(r, rad, 0xFF5252, cur ? 0.35f : 0.22f);
            m_r.Ring((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f, rad, 0xFF5252, 1.5f * s, 0.8f);
        } else if (cur) {
            // Aktif sekme: kullanicinin sectigi Vurgu Rengini (theme::Ac()) gururla yansit
            m_r.FillRound(r, rad, userAc, live ? 0.25f : 0.18f);
            m_r.StrokeRound(r, rad, userAc, std::max(1.0f, std::floor(1.6f * s)), 0.90f);
            // Kapsul tabaninda vurgu cizgisi
            m_r.Line(r.left + rad * 0.4f, r.bottom - 1.0f, r.right - rad * 0.4f, r.bottom - 1.0f,
                     userAc, std::max(1.5f, std::floor(2.0f * s)));
        } else if (hover) {
            m_r.FillRound(r, rad, theme::Elevated);
        }

        // Rozet kutusuz, profil renginde mono metin, daima tam alfa:
        const uint32_t badgeBase = p.accent ? p.accent : userAc;
        const uint32_t badgeCol = cur ? theme::Mix(badgeBase, 0xFFFFFF, 0.30f) : badgeBase;

        const std::wstring& badge = p.badge;
        float badgeW = 0.0f;
        if (!badge.empty()) {
            if (ti < m_labelCache.size()) {
                LabelCache& lc = m_labelCache[ti];
                const std::wstring& fam = m_r.Metrics().family;
                if (lc.bw < 0.0f || lc.bpx != fBadge || lc.bsrc != badge || lc.bfam != fam) {
                    lc.bsrc = badge;
                    lc.bpx = fBadge;
                    lc.bfam = fam;
                    lc.bw = m_r.MeasureText(badge, fBadge, true, true);
                }
                badgeW = lc.bw;
            } else {
                badgeW = m_r.MeasureText(badge, fBadge, true, true);
            }
        }

        m_r.PushClip(r);

        // MIKRO kademe: yalniz rozet, ortalanmis.
        if (L.tabTier == 0) {
            if (isBlocked) {
                m_r.Disc(r.right - dp(6.0f), r.top + dp(6.0f), dp(3.0f), 0xFF5252);
            } else if (isWorking) {
                m_r.Disc(r.right - dp(6.0f), r.top + dp(6.0f), dp(2.5f), 0x00E676);
            }
            if (!badge.empty()) {
                const float room = w - dp(8.0f);
                const std::wstring lab = (badgeW <= room)
                    ? badge : ElideTail(m_r, badge, room, fBadge, true, true);
                m_r.Text(lab, r, badgeCol, fBadge, Renderer::Align::Center, true, true);
            }
            m_r.PopClip();
            continue;
        }

        float x = r.left + padL;
        if (isBlocked) {
            const float dotR = dp(3.5f);
            const float dotCy = (r.top + r.bottom) * 0.5f;
            m_r.Disc(x + dotR, dotCy, dotR, 0xFF5252, 1.0f);
            m_r.Ring(x + dotR, dotCy, dotR + dp(2.0f), 0xFF5252, 1.0f, 0.6f);
            x += dotR * 2.0f + badgeGap;
        } else if (isWorking) {
            const float dotR = dp(3.0f);
            const float dotCy = (r.top + r.bottom) * 0.5f;
            m_r.Disc(x + dotR, dotCy, dotR, 0x00E676, 0.95f);
            x += dotR * 2.0f + badgeGap;
        }

        if (!badge.empty()) {
            m_r.Text(badge, D2D1::RectF(x, r.top, r.right - padR, r.bottom),
                     badgeCol, fBadge, Renderer::Align::Left, true, true);
            x += badgeW + badgeGap;
        }

        float textRight = r.right - padR;

        const D2D1_RECT_F cb = (i < L.tabClose.size()) ? L.tabClose[i]
                                                       : D2D1::RectF(0, 0, 0, 0);
        if (cb.right > cb.left) {
            textRight = cb.left - closeGap;   // yuva daima ayrilir, baslik zipla
            if (cur || hover) {               // glif kosullu
                const bool hotX = (m_hoverClose == (int)ti);
                const bool downX = (m_pressClose == (int)ti);
                if (hotX) m_r.FillRound(cb, dp(4.0f), theme::Red, downX ? 0.34f : 0.20f);
                const float ccx = (cb.left + cb.right) * 0.5f;
                const float ccy = (cb.top + cb.bottom) * 0.5f;
                const float k = dp(3.5f);
                const float xw = std::max(1.0f, dp(1.2f));
                const uint32_t xc = hotX ? theme::TextHi
                                  : (cur ? theme::TextMuted : theme::TextDim);
                m_r.Line(ccx - k, ccy - k, ccx + k, ccy + k, xc, xw);
                m_r.Line(ccx + k, ccy - k, ccx - k, ccy + k, xc, xw);
            }
        }

        if (textRight > x) {
            const float titleW = textRight - x;
            // Olcum daima semibold: sekme aktiflesince kirpma noktasi kaymasin.
            std::wstring lab;
            if (ti < m_labelCache.size()) {
                LabelCache& lc = m_labelCache[ti];
                std::wstring src = m_tabs[ti]->Title();
                if (m_tabs[ti]->IsRecording()) {
                    src = L"🔴 REC " + src;
                }
                if (lc.w != titleW || lc.px != fTitle || lc.src != src) {
                    lc.src = src;
                    lc.w = titleW;
                    lc.px = fTitle;
                    lc.out = FitTitle(m_r, src, titleW, fTitle, true);
                }
                lab = lc.out;
            } else {
                std::wstring src = m_tabs[ti]->Title();
                if (m_tabs[ti]->IsRecording()) {
                    src = L"🔴 REC " + src;
                }
                lab = FitTitle(m_r, src, titleW, fTitle, true);
            }
            if (!lab.empty()) {
                const uint32_t tc = (live || cur) ? theme::TextHi
                                  : hover ? theme::Text : theme::TextMuted;
                m_r.Text(lab, D2D1::RectF(x, r.top, textRight, r.bottom),
                         tc, fTitle, Renderer::Align::Left, cur);
            }
        }

        m_r.PopClip();
    }

    // Tasma cipi
    if (L.hidden > 0 && L.overflow.right > L.overflow.left) {
        const float orad = std::floor((L.overflow.bottom - L.overflow.top) * 0.5f);
        const bool hotOv = (m_hoverBtn == BTN_OVERFLOW);
        if (hotOv) m_r.FillRound(L.overflow, orad, theme::Elevated);
        m_r.Text(L"+" + std::to_wstring(L.hidden), L.overflow,
                 hotOv ? theme::TextHi : theme::TextMuted,
                 11.0f * s, Renderer::Align::Center, true);
    }

    // Yeni sekme dugmesi: pill'lerin kardesi
    if (L.newTab.right > L.newTab.left) {
        const float nrad = std::floor((L.newTab.bottom - L.newTab.top) * 0.5f);
        const bool hoverNew = (m_hoverBtn == BTN_NEWTAB);
        if (hoverNew) m_r.FillRound(L.newTab, nrad, theme::Elevated);
        DrawIcon(Icon::Plus, L.newTab, hoverNew ? theme::TextHi : theme::TextMuted);
    }

    // Split Panes, Multi-tasking Broadcast & Fleet Toolbar (saga dayali, btnQuake solunda)
    if (L.btnSync.right > L.btnSync.left) {
        const std::wstring syncText = m_broadcastMode ? L"📡 SYNC" : (L"📡 " + std::wstring(Tr(Msg::ToolbarBroadcast)));
        if (m_ui.Button(7512, L.btnSync, syncText, m_broadcastMode, false)) {
            ToggleBroadcastMode();
        }
    }

    if (L.btnSplitH.right > L.btnSplitH.left) {
        const std::wstring splitHText = L"⬒ " + std::wstring(Tr(Msg::ActionSplitH));
        if (m_ui.Button(7511, L.btnSplitH, splitHText.c_str(), false, false)) {
            SplitActiveTab(ft::SplitDirection::Horizontal);
        }
    }

    if (L.btnSplitV.right > L.btnSplitV.left) {
        const std::wstring splitVText = L"◫ " + std::wstring(Tr(Msg::ActionSplitV));
        if (m_ui.Button(7510, L.btnSplitV, splitVText.c_str(), false, false)) {
            SplitActiveTab(ft::SplitDirection::Vertical);
        }
    }
}

void MainWindow::DrawWindowButtons() {
    const Layout& L = m_lay;
    const float s = L.scale;
    const float w = std::max(1.0f, std::floor(1.2f * s));
    auto center = [](const D2D1_RECT_F& r, float& x, float& y) {
        x = (r.left + r.right) * 0.5f; y = (r.top + r.bottom) * 0.5f;
    };
    float x, y;

    // Guake dugmesi: ust cizgiden inen ok. Mod acikken vurgu renginde ve
    // ok yukari doner (tiklamak normal pencereye dondurur).
    const bool quake = InQuake();
    if (m_hoverBtn == BTN_QUAKE) m_r.Fill(L.btnQuake, theme::Elevated);
    else if (quake) m_r.Fill(L.btnQuake, theme::Ac(), 0.14f);
    center(L.btnQuake, x, y);
    const uint32_t qc = quake ? theme::AcHi() : ((m_hoverBtn == BTN_QUAKE) ? theme::TextHi : theme::TextMuted);
    m_r.Line(x - 6 * s, y - 5 * s, x + 6 * s, y - 5 * s, qc, w);
    if (quake) {
        m_r.Line(x - 4 * s, y + 3 * s, x, y - 1 * s, qc, w);
        m_r.Line(x, y - 1 * s, x + 4 * s, y + 3 * s, qc, w);
    } else {
        m_r.Line(x - 4 * s, y - 1 * s, x, y + 3 * s, qc, w);
        m_r.Line(x, y + 3 * s, x + 4 * s, y - 1 * s, qc, w);
    }

    if (m_hoverBtn == BTN_MIN) m_r.Fill(L.btnMin, theme::Elevated);
    center(L.btnMin, x, y);
    m_r.Line(x - 5 * s, y, x + 5 * s, y, theme::Text, w);

    if (m_hoverBtn == BTN_MAX) m_r.Fill(L.btnMax, theme::Elevated);
    center(L.btnMax, x, y);
    if (quake) {
        // Guake: tam yukseklik anahtari, dikey cift ok
        m_r.Line(x, y - 6 * s, x, y + 6 * s, m_quakeFull ? theme::AcHi() : theme::Text, w);
        const uint32_t ac = m_quakeFull ? theme::AcHi() : theme::Text;
        m_r.Line(x - 3 * s, y - 3 * s, x, y - 6 * s, ac, w);
        m_r.Line(x, y - 6 * s, x + 3 * s, y - 3 * s, ac, w);
        m_r.Line(x - 3 * s, y + 3 * s, x, y + 6 * s, ac, w);
        m_r.Line(x, y + 6 * s, x + 3 * s, y + 3 * s, ac, w);
    } else if (IsZoomed(m_hwnd)) {
        m_r.Stroke(D2D1::RectF(x - 5 * s, y - 3 * s, x + 3 * s, y + 5 * s), theme::Text, w);
        m_r.Line(x - 3 * s, y - 5 * s, x + 5 * s, y - 5 * s, theme::Text, w);
        m_r.Line(x + 5 * s, y - 5 * s, x + 5 * s, y + 3 * s, theme::Text, w);
    } else {
        m_r.Stroke(D2D1::RectF(x - 5 * s, y - 5 * s, x + 5 * s, y + 5 * s), theme::Text, w);
    }

    if (m_hoverBtn == BTN_CLOSE) m_r.Fill(L.btnClose, theme::Red, 0.85f);
    center(L.btnClose, x, y);
    const uint32_t xc = (m_hoverBtn == BTN_CLOSE) ? 0xFFFFFF : theme::Text;
    m_r.Line(x - 5 * s, y - 5 * s, x + 5 * s, y + 5 * s, xc, w);
    m_r.Line(x + 5 * s, y - 5 * s, x - 5 * s, y + 5 * s, xc, w);
}

int MainWindow::HitTab(int px, int py) const {
    if (py < 0 || py >= (int)m_lay.titleH) return -1;
    for (size_t i = 0; i < m_lay.tabs.size(); ++i) {
        const D2D1_RECT_F& r = m_lay.tabs[i];
        if (r.right <= r.left) continue;
        if (px >= r.left && px < r.right) return (int)(m_lay.tabFirst + i);
    }
    return -1;
}

int MainWindow::HitTabClose(int px, int py) const {
    const float padY = 4.0f * m_lay.scale;   // 16dp hedef Fitts sinirinda
    for (size_t i = 0; i < m_lay.tabClose.size(); ++i) {
        const D2D1_RECT_F& c = m_lay.tabClose[i];
        if (c.right <= c.left) continue;
        if (px >= c.left && px < c.right &&
            py >= c.top - padY && py < c.bottom + padY) {
            return (int)(m_lay.tabFirst + i);
        }
    }
    return -1;
}

} // namespace ft
