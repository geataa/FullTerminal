#include "ui/MainWindowInternal.h"

namespace ft {

void MainWindow::DrawSidebar() {
    const Layout& L = m_lay;
    const float s = L.scale;
    const D2D1_RECT_F& a = L.side;
    if (a.right <= a.left) return;

    m_r.Fill(a, theme::Surface);
    m_r.Fill(D2D1::RectF(a.right - 1, a.top, a.right, a.bottom), theme::Border);
    m_r.PushClip(a);

    const float pad = std::floor(10 * s);
    const float rowH = std::floor(38 * s);
    float y = a.top + std::floor(10 * s);

    struct NavItem { View v; Icon ic; Msg msg; };
    const NavItem nav[] = {
        { View::Terminal,    Icon::Terminal, Msg::NavTerminal },
        { View::Hosts,       Icon::Hosts,    Msg::NavSystemsHub },
        { View::Sftp,        Icon::Folder,   Msg::NavSftpFiles },
        { View::Keychain,    Icon::Key,      Msg::NavIdentities },
        { View::PortForward, Icon::Forward,  Msg::NavPortForward },
        { View::Snippets,    Icon::Snippet,  Msg::NavSnippets },
        { View::KnownHosts,  Icon::Shield,   Msg::NavKnownHosts },
        { View::Logs,        Icon::Clock,    Msg::NavLogs },
        { View::Settings,    Icon::Gear,     Msg::NavSettings },
    };

    for (int i = 0; i < (int)std::size(nav); ++i) {
        const D2D1_RECT_F r = D2D1::RectF(a.left + std::floor(6 * s), y,
                                          a.right - std::floor(6 * s), y + rowH);
        const bool active = (m_view == nav[i].v);
        if (m_ui.Row(2000 + i, r, active, active ? theme::Ac() : 0)) SetView(nav[i].v);

        const float ib = std::floor(20 * s);
        const D2D1_RECT_F iconRect = D2D1::RectF(r.left + std::floor(13 * s), (r.top + r.bottom) * 0.5f - ib * 0.5f,
                                                 r.left + std::floor(13 * s) + ib, (r.top + r.bottom) * 0.5f + ib * 0.5f);
        DrawIcon(nav[i].ic, iconRect, active ? theme::AcHi() : theme::TextMuted);

        m_r.Text(Tr(nav[i].msg),
                 D2D1::RectF(iconRect.right + std::floor(12 * s), r.top, r.right - std::floor(30 * s), r.bottom),
                 active ? theme::TextHi : theme::Text, 12.5f * s, Renderer::Align::Left, active);

        if (nav[i].v == View::Terminal && !m_tabs.empty()) {
            m_r.Text(std::to_wstring(m_tabs.size()),
                     D2D1::RectF(r.left, r.top, r.right - std::floor(12 * s), r.bottom),
                     theme::TextDim, 11.0f * s, Renderer::Align::Right, false, true);
        }
        if (nav[i].v == View::Hosts && !m_hubNodes.empty()) {
            m_r.Text(std::to_wstring(m_hubNodes.size()),
                     D2D1::RectF(r.left, r.top, r.right - std::floor(12 * s), r.bottom),
                     theme::AcHi(), 11.0f * s, Renderer::Align::Right, false, true);
        }
        y += rowH + std::floor(1 * s);
    }

    y += std::floor(10 * s);
    m_ui.Separator(a.left + pad, a.right - pad, y);
    y += std::floor(12 * s);

    const float listTop = y;
    y -= m_sideScroll;
    // Kayan liste yukaridaki gezinme satirlarinin ustune cizilmesin ve
    // onlarla ayni tiki almasin.
    m_r.PushClip(D2D1::RectF(a.left, listTop, a.right, a.bottom));
    const bool inList = m_in.my >= listTop && m_in.py >= listTop &&
                        m_in.my < a.bottom && m_in.py < a.bottom;   // durum cubugu altta

    const bool isTr = (I18n::CurrentLang() == LangId::Tr);
    m_ui.Caption(D2D1::RectF(a.left + pad + std::floor(4 * s), y, a.right - pad, y + std::floor(18 * s)),
                 isTr ? L"AÇIK OTURUMLAR" : L"ACTIVE SESSIONS");
    y += std::floor(22 * s);

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const D2D1_RECT_F r = D2D1::RectF(a.left + std::floor(6 * s), y,
                                          a.right - std::floor(6 * s), y + std::floor(40 * s));
        const ShellProfile& p = m_tabs[i]->profile();
        const uint32_t accent = (i == m_active) ? theme::Ac() : (p.accent ? p.accent : theme::Ac());
        if (m_ui.Row(2100 + (int)i, r, i == m_active, accent) && inList) { SelectTab(i); SetView(View::Terminal); }
        m_r.Text(Trunc(m_tabs[i]->Title(), 22),
                 D2D1::RectF(r.left + std::floor(12 * s), r.top + std::floor(4 * s),
                             r.right - std::floor(8 * s), r.top + std::floor(22 * s)),
                 i == m_active ? theme::TextHi : theme::Text, 12.0f * s);
        wchar_t sub[96];
        swprintf_s(sub, L"%s  %dx%d", p.badge.c_str(),
                   m_tabs[i]->screen().Cols(), m_tabs[i]->screen().Rows());
        m_r.Text(sub, D2D1::RectF(r.left + std::floor(12 * s), r.top + std::floor(21 * s),
                                  r.right - std::floor(8 * s), r.bottom),
                 theme::TextDim, 10.5f * s, Renderer::Align::Left, false, true);
        y += std::floor(42 * s);
    }

    const D2D1_RECT_F btn = D2D1::RectF(a.left + pad, y + std::floor(6 * s),
                                        a.right - pad, y + std::floor(38 * s));
    const std::wstring btnText = isTr ? L"+  Yeni oturum" : L"+  New Session";
    if (m_ui.Button(2199, btn, btnText.c_str()) && inList) {
        POINT p{ (LONG)btn.left, (LONG)btn.bottom };
        ClientToScreen(m_hwnd, &p);
        ShowProfileMenu(p);
    }
    y = btn.bottom;

    const float contentH = y - listTop + m_sideScroll;
    const float viewH = a.bottom - listTop;
    if (contentH <= viewH) m_sideScroll = 0.0f;
    else if (m_sideScroll > contentH - viewH) m_sideScroll = contentH - viewH;

    m_r.PopClip();   // liste
    m_r.PopClip();
}

} // namespace ft
