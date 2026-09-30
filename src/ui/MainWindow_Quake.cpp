#include "ui/MainWindowInternal.h"

namespace ft {

namespace {

// KENDI is parcaciginda: UI is parcacigi bir an tikanirsa (senkron komut,
// modal dongu) Windows kancayi zaman asimindan sessizce kaldirmasin.
std::atomic<int>  g_hookVk{ 0 };
std::atomic<int>  g_hookMods{ 0 };
std::atomic<HWND> g_hookTarget{ nullptr };
bool   g_hookKeyDown = false;          // yalnizca kanca is parcacigi
HANDLE g_hookThread = nullptr;
DWORD  g_hookThreadId = 0;

int ModsNow() {
    int m = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_MENU) & 0x8000)    m |= MOD_ALT;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)   m |= MOD_SHIFT;
    if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) m |= MOD_WIN;
    return m;
}

LRESULT CALLBACK LowLevelKbProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        const int vk = g_hookVk.load();
        if (vk && (int)k->vkCode == vk) {
            const bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
            const bool up   = (wp == WM_KEYUP || wp == WM_SYSKEYUP);
            if (down && ModsNow() == g_hookMods.load()) {
                if (!g_hookKeyDown) {             // MOD_NOREPEAT esdegeri
                    g_hookKeyDown = true;
                    if (HWND t = g_hookTarget.load()) PostMessageW(t, WM_HOTKEY, (WPARAM)kQuakeHotKeyId, 0);
                }
                return 1;                          // tus baska yere gitmesin
            }
            if (up && g_hookKeyDown) { g_hookKeyDown = false; return 1; }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

struct HookStart { HANDLE ready; bool ok; };

DWORD WINAPI HookThreadProc(LPVOID param) {
    auto* st = static_cast<HookStart*>(param);
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);   // kuyrugu olustur
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, &LowLevelKbProc, GetModuleHandleW(nullptr), 0);
    st->ok = (hook != nullptr);
    SetEvent(st->ready);          // bundan sonra st'ye dokunma, cagiran yigini terk edebilir
    if (!hook) return 1;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { /* kanca bu dongude cagrilir */ }
    UnhookWindowsHookEx(hook);
    return 0;
}

void StopKeyHook() {
    if (!g_hookThread) return;
    PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
    WaitForSingleObject(g_hookThread, 2000);
    CloseHandle(g_hookThread);
    g_hookThread = nullptr;
    g_hookThreadId = 0;
    g_hookVk = 0;
    g_hookTarget = nullptr;
}

bool StartKeyHook(HWND target, int vk, int mods) {
    g_hookVk = vk;
    g_hookMods = mods;
    g_hookTarget = target;
    if (g_hookThread) return true;                 // calisiyor; hedef guncellendi
    HookStart st{ CreateEventW(nullptr, TRUE, FALSE, nullptr), false };
    if (!st.ready) return false;
    g_hookThread = CreateThread(nullptr, 0, &HookThreadProc, &st, 0, &g_hookThreadId);
    // Sonsuz bekleme: is parcacigi st'ye yazmadan once bu cerceveden cikmamaliyiz.
    if (g_hookThread) WaitForSingleObject(st.ready, INFINITE);
    CloseHandle(st.ready);
    const bool ok = st.ok;
    if (!ok && g_hookThread) {
        WaitForSingleObject(g_hookThread, 1000);
        CloseHandle(g_hookThread);
        g_hookThread = nullptr;
        g_hookThreadId = 0;
    }
    return ok;
}


} // namespace

void MainWindow::ApplyColorScheme() {
    // 1) Tema uygulanmadan once sekmelerin tuvalini temizle/hazirla (kullanici talebi: clear oncesi)
    for (auto& tab : m_tabs) {
        if (tab) tab->ClearBeforeThemeApply();
    }

    // 2) Yeni tema paletini global olarak guncelle
    theme::SetColorScheme(m_cfg.colorScheme, m_cfg.customBgColor, m_cfg.customFgColor);

    // 3) Yeni tema paletiyle sekmelerin ekranini ve imlecini canlandir
    for (auto& tab : m_tabs) {
        if (tab) tab->ApplyTheme();
    }
    m_dirty = true;
}

void MainWindow::PostQuake(QuakeCmd c) {
    // Dugmeler cizim sirasinda tetikleniyor; pencere stilini ve boyutunu
    // BeginDraw/EndDraw arasinda degistirmek yerine bir sonraki mesaja birak.
    if (m_hwnd) PostMessageW(m_hwnd, WM_QUAKE_CMD, (WPARAM)c, 0);
}

int MainWindow::QuakeGrip() const {
    return std::max(4, (int)std::floor(5.0f * m_lay.scale));
}

void MainWindow::SetCornerPreference(bool round) {
    EnableRoundedCorners(m_hwnd, round);
}

RECT MainWindow::QuakeWorkArea() const {
    HMONITOR mon = nullptr;
    if (m_cfg.quakeMonitor == 1) {
        mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    } else {
        POINT pt{};
        GetCursorPos(&pt);
        mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) {
        RECT r{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
        return r;
    }
    return mi.rcWork;
}

RECT MainWindow::QuakeRect(const RECT& work) const {
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    const int h = m_quakeFull ? workH
                : std::max(1, workH * std::clamp(m_cfg.quakeHeightPercent, 20, 100) / 100);
    const int w = std::max(1, workW * std::clamp(m_cfg.quakeWidthPercent, 30, 100) / 100);
    int x = work.left + (workW - w) / 2;
    if (m_cfg.quakeAlign == 0) x = work.left;
    else if (m_cfg.quakeAlign == 2) x = work.right - w;
    return RECT{ x, work.top, x + w, work.top + h };
}

bool MainWindow::RegisterQuakeHotkey(bool announce) {
    UnregisterQuakeHotkey();
    const std::wstring name = HotkeyName(m_cfg.quakeHotkeyVk, m_cfg.quakeHotkeyMods);
    m_hotkeyOk = RegisterHotKey(m_hwnd, kQuakeHotKeyId,
                                (UINT)(m_cfg.quakeHotkeyMods & 0x0F) | MOD_NOREPEAT,
                                (UINT)m_cfg.quakeHotkeyVk) != FALSE;
    if (!m_hotkeyOk) {
        // Sistem veya baska bir uygulama tutuyor (duz F12 her zaman boyle).
        // Kullanici bu tusu acikca secti: klavye kancasiyla yakala.
        m_hotkeyViaHook = StartKeyHook(m_hwnd, m_cfg.quakeHotkeyVk, m_cfg.quakeHotkeyMods & 0x0F);
        m_hotkeyOk = m_hotkeyViaHook;
    }
    if (!m_hotkeyOk) {
        Toast(L"Kisayol kaydedilemedi: " + name + L". Ayarlar > Visor'dan baska bir tus sec.");
    } else if (announce) {
        Toast(L"Visor kisayolu: " + name + (m_hotkeyViaHook ? L" (klavye kancasi)" : L""));
    }
    if (m_trayVisible) AddTrayIcon();   // ipucundaki kisayol adi guncel kalsin
    return m_hotkeyOk;
}

void MainWindow::UnregisterQuakeHotkey() {
    if (m_hwnd) UnregisterHotKey(m_hwnd, kQuakeHotKeyId);
    StopKeyHook();
    m_hotkeyOk = false;
    m_hotkeyViaHook = false;
}

std::wstring MainWindow::HotkeyName(int vk, int mods) const {
    std::wstring s;
    if (mods & MOD_CONTROL) s += L"Ctrl+";
    if (mods & MOD_ALT)     s += L"Alt+";
    if (mods & MOD_SHIFT)   s += L"Shift+";
    if (mods & MOD_WIN)     s += L"Win+";

    if (vk >= VK_F1 && vk <= VK_F24) return s + L"F" + std::to_wstring(vk - VK_F1 + 1);
    switch (vk) {
    case VK_SPACE:  return s + L"Bosluk";
    case VK_PAUSE:  return s + L"Pause";
    case VK_SCROLL: return s + L"Scroll Lock";
    case VK_INSERT: return s + L"Insert";
    case VK_DELETE: return s + L"Delete";
    case VK_HOME:   return s + L"Home";
    case VK_END:    return s + L"End";
    case VK_PRIOR:  return s + L"PgUp";
    case VK_NEXT:   return s + L"PgDn";
    case VK_TAB:    return s + L"Tab";
    case VK_RETURN: return s + L"Enter";
    default: break;
    }
    // Yazilabilir tuslarda klavye duzenine gore karakteri goster (TR Q: " , OEM_3)
    const UINT ch = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_CHAR) & 0x7FFFFFFF;
    if (ch >= 0x21) return s + std::wstring(1, (wchar_t)towupper((wint_t)ch));
    const UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
    wchar_t buf[64]{};
    if (sc && GetKeyNameTextW((LONG)(sc << 16), buf, 64) > 0) return s + buf;
    wchar_t hex[16]; swprintf_s(hex, L"VK 0x%02X", vk);
    return s + hex;
}

void MainWindow::ForceForeground() {
    if (!m_hwnd) return;
    if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
    HWND fg = GetForegroundWindow();
    if (fg != m_hwnd) {
        // Kisayol/tepsi girdisi bize on plan hakki verir; yine de reddedilirse
        // on plandaki is parcacigina girdi kuyrugunu kisa sure baglayip dene.
        if (!SetForegroundWindow(m_hwnd)) {
            const DWORD me = GetCurrentThreadId();
            const DWORD other = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
            if (other && other != me && AttachThreadInput(me, other, TRUE)) {
                BringWindowToTop(m_hwnd);
                SetForegroundWindow(m_hwnd);
                AttachThreadInput(me, other, FALSE);
            }
        }
    }
    SetFocus(m_hwnd);
    m_focused = (GetForegroundWindow() == m_hwnd);
    m_dirty = true;
}

void MainWindow::EnterQuakeMode() {
    if (InQuake() || !m_hwnd) return;

    // Her giriste GUNCEL normal yerlesim saklanir; eskisi bayat kalmasin.
    m_savedNormalPlacement = WINDOWPLACEMENT{};
    m_savedNormalPlacement.length = sizeof(WINDOWPLACEMENT);
    m_savedPlacementValid = GetWindowPlacement(m_hwnd, &m_savedNormalPlacement) != FALSE;

    m_cfg.quakeMode = true;
    m_quakeFull = false;
    m_quakeFrac = 0.0f;
    m_quakeVisH = -1;

    // Gorev cubugu dugmesi yok (WS_EX_TOOLWINDOW), cerceve/boyut kenari yok.
    // Stil gizliyken degistirilir; gorunurken WS_EX_TOOLWINDOW gorev cubugunu guncellemez.
    ShowWindow(m_hwnd, SW_HIDE);
    SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    ex = (ex | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP) & ~(LONG_PTR)WS_EX_APPWINDOW;
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    SetCornerPreference(false);
    EnableWindowTransparency(m_hwnd);

    m_quakeState = QuakeState::Hidden;
    AddTrayIcon();   // gorev cubugu yok; tepsi her zaman bir geri donus yolu
    RegisterQuakeHotkey(false);
    m_cfg.Save(m_dataDir);

    QuakeShow();
    if (m_hotkeyOk) Toast(L"Visor modu: " + HotkeyName(m_cfg.quakeHotkeyVk, m_cfg.quakeHotkeyMods) +
                          L" ile gizle/goster, F11 tam yukseklik");
}

void MainWindow::ExitQuakeMode() {
    if (!InQuake() || !m_hwnd) return;

    UnregisterQuakeHotkey();
    m_quakeResizing = false;
    m_quakeState = QuakeState::Normal;
    m_cfg.quakeMode = false;
    m_quakeFull = false;

    ShowWindow(m_hwnd, SW_HIDE);
    SetWindowRgn(m_hwnd, nullptr, FALSE);
    SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    ex = (ex | WS_EX_NOREDIRECTIONBITMAP) & ~(LONG_PTR)WS_EX_TOOLWINDOW;
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    SetCornerPreference(true);
    EnableWindowTransparency(m_hwnd);

    if (m_savedPlacementValid) {
        WINDOWPLACEMENT wp = m_savedNormalPlacement;
        if (wp.showCmd != SW_SHOWMAXIMIZED) wp.showCmd = SW_SHOWNORMAL;
        SetWindowPlacement(m_hwnd, &wp);
    }
    ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_SHOWMAXIMIZED : SW_SHOW);
    ForceForeground();
    if (!m_cfg.minimizeToTray && !m_cfg.runInBackground) RemoveTrayIcon();

    m_cfg.Save(m_dataDir);
    OnResize();
    Toast(L"Normal pencere moduna donuldu");
}

void MainWindow::QuakeToggle() {
    switch (m_quakeState) {
    case QuakeState::Normal:
        EnterQuakeMode();
        break;
    case QuakeState::Hidden:
    case QuakeState::SlidingUp:
        QuakeShow();
        break;
    case QuakeState::DroppingDown:
        QuakeHide();
        break;
    case QuakeState::Visible: {
        // Gorunur ama arkada kalmissa once one getir; zaten ondeyse gizle.
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (fg != m_hwnd && pid != GetCurrentProcessId()) ForceForeground();
        else QuakeHide();
        break;
    }
    }
}

void MainWindow::QuakeShow() {
    if (!InQuake() || !m_hwnd) return;
    if (m_quakeState == QuakeState::Visible) { ForceForeground(); return; }

    if (m_quakeState == QuakeState::Hidden) {
        // Her inis farenin (veya birincil) ekranin calisma alanina gore.
        m_quakeWork = QuakeWorkArea();
        m_quakeRect = QuakeRect(m_quakeWork);
        m_quakeFrac = 0.0f;
        m_quakeVisH = -1;
        if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
    }

    m_quakeFrom = m_quakeFrac;   // yarim kalan cikisi tersine cevirir, sicrama yok
    m_quakeTo = 1.0f;
    m_quakeAnimStart = NowTicks();
    m_quakeState = QuakeState::DroppingDown;

    QuakeApplyFrame(m_quakeFrac);
    ShowWindow(m_hwnd, SW_SHOW);
    ForceForeground();
    OnResize();
    if (m_cfg.quakeAnimDurationMs <= 0) UpdateQuakeAnimation();
    m_dirty = true;
}

void MainWindow::QuakeHide() {
    if (!InQuake() || !m_hwnd) return;
    if (m_quakeState == QuakeState::Hidden || m_quakeState == QuakeState::SlidingUp) return;
    if (m_quakeResizing) { m_quakeResizing = false; ReleaseCapture(); }

    if (m_drawerOpen) {
        m_drawerOpen = false;
        m_drawerProgress = 0.0f;
    }
    m_quakeFrom = m_quakeFrac;
    m_quakeTo = 0.0f;
    m_quakeAnimStart = NowTicks();
    m_quakeState = QuakeState::SlidingUp;
    if (m_cfg.quakeAnimDurationMs <= 0) UpdateQuakeAnimation();
    m_dirty = true;
}

void MainWindow::QuakeReapply() {
    if (!InQuake() || !m_hwnd) return;
    // Ekran degistirilmediyse ayni calisma alani; tercih degistiyse yeniden sec.
    m_quakeWork = QuakeWorkArea();
    if (m_cfg.quakeMonitor == 0) {
        // Farenin ekrani secimi pencerenin su an bulundugu ekranda kalsin.
        HMONITOR mon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) m_quakeWork = mi.rcWork;
    }
    m_quakeRect = QuakeRect(m_quakeWork);
    EnableWindowTransparency(m_hwnd);
    if (m_quakeState == QuakeState::Visible) {
        m_quakeVisH = -1;
        QuakeApplyFrame(1.0f);
        OnResize();
    }
    m_dirty = true;
}

// Pencere kaydirilir VE bolgeyle kirpilir: calisma alaninin ustune tasan kisim
// hic cizilmez. Aksi halde ust uste dizilmis ikinci monitorde veya ustteki
// gorev cubugunun uzerinde konsolun bir seridi gorunurdu.
void MainWindow::QuakeApplyFrame(float frac) {
    const int W = m_quakeRect.right - m_quakeRect.left;
    const int H = m_quakeRect.bottom - m_quakeRect.top;
    if (W <= 0 || H <= 0) return;
    const int vh = std::clamp((int)std::lround(H * frac), 0, H);
    const int y = m_quakeRect.top - (H - vh);
    const bool growing = (m_quakeVisH < 0) || (vh >= m_quakeVisH);
    m_quakeVisH = vh;

    auto region = [&]() {
        if (vh >= H) { SetWindowRgn(m_hwnd, nullptr, TRUE); return; }
        HRGN rgn = CreateRectRgn(0, H - vh, W, H);
        if (rgn && !SetWindowRgn(m_hwnd, rgn, TRUE)) DeleteObject(rgn);
    };
    auto move = [&]() {
        SetWindowPos(m_hwnd, HWND_TOPMOST, m_quakeRect.left, y, W, H, SWP_NOACTIVATE);
    };
    // Buyurken once tasi sonra kirp, kuculurken tersi: ara karede bile
    // gorunen serit calisma alaninin ustune cikmaz.
    if (growing) { move(); region(); }
    else         { region(); move(); }
}

void MainWindow::DrawVisorSettings(float x0, float x1, float labelW, float rowH, float& y) {
    const float s = m_lay.scale;
    auto dp = [s](float v) { return std::floor(v * s); };
    auto label = [&](const wchar_t* t) {
        m_r.Text(t, D2D1::RectF(x0, y, x0 + labelW, y + rowH), theme::TextMuted, 12.0f * s);
    };
    auto note = [&](const std::wstring& t, float hdp) {
        const float h = dp(hdp);
        m_r.Text(t, D2D1::RectF(x0 + labelW, y, x1, y + h), theme::TextDim, 11.5f * s,
                 Renderer::Align::Left, false, false, 1.0f, true);
        y += h + dp(6);
    };
    bool save = false, reapply = false;
    const std::wstring hk = HotkeyName(m_cfg.quakeHotkeyVk, m_cfg.quakeHotkeyMods);

    // ---- durum karti ----
    {
        const D2D1_RECT_F card = D2D1::RectF(x0, y, x1, y + dp(72));
        m_r.FillRound(card, dp(8), theme::Surface);
        const bool on = InQuake();
        const uint32_t dot = !on ? theme::TextDim : (m_hotkeyOk ? theme::Green : theme::Amber);
        m_r.Disc(card.left + dp(20), card.top + dp(24), dp(4), dot);
        m_r.Text(L"Visor Modu (Kayan Terminal)", D2D1::RectF(card.left + dp(34), card.top + dp(10), card.right - dp(130), card.top + dp(38)),
                 theme::TextHi, 15.0f * s, Renderer::Align::Left, true);
        std::wstring sub = !on ? L"Kapali. Pencere normal davraniyor; kisayol kayitli degil."
                         : m_hotkeyOk ? (hk + (m_hotkeyViaHook ? L" (klavye kancasi)" : L"") +
                                         L" ile ekranin ustunden iner ve gizlenir. Gorev cubugunda yok, tepside var.")
                         : (L"Acik ama " + hk + L" kaydedilemedi (baska uygulama kullaniyor). Asagidan degistir.");
        m_r.Text(sub, D2D1::RectF(card.left + dp(34), card.top + dp(38), card.right - dp(130), card.bottom - dp(8)),
                 on && !m_hotkeyOk ? theme::Amber : theme::TextMuted, 11.5f * s,
                 Renderer::Align::Left, false, false, 1.0f, true);
        const D2D1_RECT_F btn = D2D1::RectF(card.right - dp(116), card.top + dp(20), card.right - dp(16), card.top + dp(52));
        if (m_ui.Button(8050, btn, on ? L"Kapat" : L"Ac", !on)) {
            PostQuake(on ? QC_EXIT : QC_ENTER);
        }
        y = card.bottom + dp(18);
    }

    // ---- kisayol ----
    label(L"Global kisayol");
    {
        const D2D1_RECT_F kb = D2D1::RectF(x0 + labelW, y, x0 + labelW + dp(230), y + rowH);
        if (m_ui.Button(8053, kb, m_captureHotkey ? L"Bir tusa basin... (Esc iptal)" : hk, m_captureHotkey)) {
            if (m_captureHotkey) {
                m_captureHotkey = false;
                if (InQuake()) RegisterQuakeHotkey(false);
            } else {
                // Mevcut kisayol kayitliyken ayni tusa basmak WM_HOTKEY olurdu, WM_KEYDOWN degil.
                UnregisterQuakeHotkey();
                m_captureHotkey = true;
                m_ui.SetFocus(ID_NONE);
            }
        }
        const D2D1_RECT_F rb = D2D1::RectF(kb.right + dp(10), y, kb.right + dp(130), y + rowH);
        if (m_ui.Button(8054, rb, L"F12'ye don", false, false,
                        m_cfg.quakeHotkeyVk != VK_F12 || m_cfg.quakeHotkeyMods != 0)) {
            m_captureHotkey = false;
            m_cfg.quakeHotkeyVk = VK_F12;
            m_cfg.quakeHotkeyMods = 0;
            if (InQuake()) RegisterQuakeHotkey(true);
            save = true;
        }
        y += rowH + dp(6);
        note(L"Sistem genelinde calisir ve YALNIZCA Visor modu acikken etkindir; kapaliyken bu tus "
             L"terminale ve diger uygulamalara gider. Duz F12 Windows'ta hata ayiklayiciya ayrildigi icin "
             L"klavye kancasiyla yakalanir (yonetici olarak calisan bir pencere ondeyken calismaz).", 50);
    }

    // ---- boyut ve konum ----
    label(L"Yukseklik");
    {
        static const int hv[] = { 30, 40, 50, 60, 75, 100 };
        int idx = -1;
        for (int i = 0; i < 6; ++i) if (m_cfg.quakeHeightPercent == hv[i]) idx = i;
        const std::vector<std::wstring> items = { L"%30", L"%40", L"%50", L"%60", L"%75", L"%100" };
        const D2D1_RECT_F cr = D2D1::RectF(x0 + labelW, y, x0 + labelW + dp(330), y + rowH);
        if (m_ui.Choice(8051, cr, items, idx)) {
            m_cfg.quakeHeightPercent = hv[idx];
            m_quakeFull = false;
            save = reapply = true;
        }
        wchar_t cur[48];
        swprintf_s(cur, L"%%%d%s", m_cfg.quakeHeightPercent, m_quakeFull ? L" (F11: tam)" : L"");
        m_r.Text(cur, D2D1::RectF(cr.right + dp(12), y, x1, y + rowH), theme::TextHi, 12.0f * s,
                 Renderer::Align::Left, true, true);
        y += rowH + dp(4);
        note(L"Ince ayar: konsolun alt kenarindaki tutamagi surukle. F11 gecici tam yukseklik.", 20);
    }

    label(L"Genislik");
    {
        static const int wv[] = { 50, 70, 85, 100 };
        int idx = -1;
        for (int i = 0; i < 4; ++i) if (m_cfg.quakeWidthPercent == wv[i]) idx = i;
        const std::vector<std::wstring> items = { L"%50", L"%70", L"%85", L"%100" };
        if (m_ui.Choice(8055, D2D1::RectF(x0 + labelW, y, x0 + labelW + dp(260), y + rowH), items, idx)) {
            m_cfg.quakeWidthPercent = wv[idx];
            save = reapply = true;
        }
        y += rowH + dp(8);
    }

    label(L"Hizalama");
    {
        int a = m_cfg.quakeAlign;
        const std::vector<std::wstring> items = { L"Sol", L"Orta", L"Sag" };
        if (m_ui.Choice(8056, D2D1::RectF(x0 + labelW, y, x0 + labelW + dp(220), y + rowH), items, a)) {
            m_cfg.quakeAlign = a;
            save = reapply = true;
        }
        y += rowH + dp(8);
    }

    label(L"Ekran");
    {
        int m = m_cfg.quakeMonitor;
        const std::vector<std::wstring> items = { L"Farenin oldugu ekran", L"Birincil ekran" };
        if (m_ui.Choice(8057, D2D1::RectF(x0 + labelW, y, x0 + labelW + dp(340), y + rowH), items, m)) {
            m_cfg.quakeMonitor = m;
            save = reapply = true;
        }
        y += rowH + dp(8);
    }

    label(L"Animasyon");
    {
        static const int av[] = { 0, 120, 200, 320 };
        int idx = -1;
        for (int i = 0; i < 4; ++i) if (m_cfg.quakeAnimDurationMs == av[i]) idx = i;
        const std::vector<std::wstring> items = { L"Kapali", L"Hizli", L"Normal", L"Yavas" };
        if (m_ui.Choice(8058, D2D1::RectF(x0 + labelW, y, x0 + labelW + dp(300), y + rowH), items, idx)) {
            m_cfg.quakeAnimDurationMs = av[idx];
            save = true;
        }
        y += rowH + dp(8);
    }

    if (m_ui.Check(8052, D2D1::RectF(x0 + labelW, y, x1, y + rowH), m_cfg.quakeHideOnLoseFocus,
                   L"Baska bir uygulamaya gecince otomatik gizlen")) {
        save = true;
    }
    y += rowH + dp(16);

    // ---- kisayol ozeti ----
    m_ui.Caption(D2D1::RectF(x0, y, x1, y + dp(18)), L"VISOR MODUNDA");
    y += dp(24);
    const std::pair<std::wstring, const wchar_t*> rows[] = {
        { hk,              L"Konsolu indir / gizle; arkada kaldiysa one getirir" },
        { L"F11",          L"Tam yukseklik ac / kapat (baslik cubugundaki cift ok da ayni)" },
        { L"Alt kenar",    L"Surukleyerek yukseklik; birakinca kaydedilir" },
        { L"X, Alt+F4",    L"Gizler, kapatmaz. Cikis: tepsi menusu > Cikis veya Ctrl+Shift+Q" },
        { L"exit",         L"Son sekme kapaninca yeni sekme hazirlanir, konsol gizlenir" },
        { L"Ctrl+Shift+M", L"Altyapi cekmecesi" },
    };
    for (const auto& r : rows) {
        m_r.Text(r.first, D2D1::RectF(x0, y, x0 + labelW, y + dp(24)), theme::AcHi(), 11.5f * s,
                 Renderer::Align::Left, true, true);
        m_r.Text(r.second, D2D1::RectF(x0 + labelW, y, x1, y + dp(24)), theme::Text, 12.0f * s);
        y += dp(26);
    }
    y += dp(8);

    if (save) m_cfg.Save(m_dataDir);
    if (reapply && InQuake()) PostQuake(QC_REAPPLY);
}

void MainWindow::UpdateQuakeAnimation() {
    if (m_quakeState != QuakeState::DroppingDown && m_quakeState != QuakeState::SlidingUp) return;

    const double dur = (double)std::max(0, m_cfg.quakeAnimDurationMs) / 1000.0;
    // Yarim kalmis bir hareketi ters cevirince kalan mesafe kadar surer.
    const double span = std::fabs((double)m_quakeTo - (double)m_quakeFrom);
    const double total = dur * span;
    const double elapsed = (double)(NowTicks() - m_quakeAnimStart) / (double)TickFreq();
    const float t = (total <= 0.0005) ? 1.0f : std::clamp((float)(elapsed / total), 0.0f, 1.0f);

    m_quakeFrac = m_quakeFrom + (m_quakeTo - m_quakeFrom) * EaseOutCubic(t);
    if (t >= 1.0f) m_quakeFrac = m_quakeTo;

    if (m_quakeState == QuakeState::DroppingDown) {
        QuakeApplyFrame(m_quakeFrac);
        if (t >= 1.0f) {
            m_quakeState = QuakeState::Visible;
            ForceForeground();
        }
    } else {
        if (t >= 1.0f) {
            m_quakeState = QuakeState::Hidden;
            ShowWindow(m_hwnd, SW_HIDE);
            m_quakeVisH = -1;
            SetWindowRgn(m_hwnd, nullptr, FALSE);
        } else {
            QuakeApplyFrame(m_quakeFrac);
        }
    }
    m_dirty = true;
}

void MainWindow::ToggleSlideDrawer() {
    m_drawerOpen = !m_drawerOpen;
    m_drawerAnimStart = NowTicks();
    if (m_drawerOpen) {
        // Cekmece kalici: alttaki bir alan yazi almaya, bekleyen sekme
        // kapatma tiki calismaya devam etmesin.
        m_ui.SetFocus(ID_NONE);
        m_pressClose = -1;
        m_hoverTab = m_hoverBtn = m_hoverClose = -1;
    }
    m_dirty = true;
}

void MainWindow::UpdateDrawerAnimation() {
    const float target = m_drawerOpen ? 1.0f : 0.0f;
    if (std::abs(m_drawerProgress - target) > 0.001f) {
        const float speed = 0.22f;
        m_drawerProgress += (target - m_drawerProgress) * speed;
        if (std::abs(m_drawerProgress - target) < 0.005f) {
            m_drawerProgress = target;
        }
        m_dirty = true;
    }
}

void MainWindow::DrawSlideDrawer(const D2D1_RECT_F& a) {
    if (m_drawerProgress <= 0.001f) return;

    const float s = m_lay.scale;
    // Arka plani karart
    m_r.Fill(a, 0x000000, 0.55f * m_drawerProgress);

    const float drawerW = std::min(420.0f * s, (a.right - a.left) - 40.0f * s);
    const float drawerLeft = a.right - drawerW * m_drawerProgress;
    const D2D1_RECT_F drawerRect = D2D1::RectF(drawerLeft, a.top, a.right, a.bottom);

    // Karartilmis dis alana tiklandiginda cekmeceyi kapat (basis da disarida olmali)
    if (m_in.clicked && m_in.mx >= a.left && m_in.mx < drawerLeft &&
        m_in.px >= a.left && m_in.px < drawerLeft) {
        ToggleSlideDrawer();
        return;
    }

    // Cekmece paneli arka plani ve sol kenarlik
    m_r.Fill(drawerRect, theme::Surface);
    m_r.Fill(D2D1::RectF(drawerRect.left, drawerRect.top, drawerRect.left + std::max(1.0f, std::floor(1.5f * s)), drawerRect.bottom), theme::BorderHi);
    m_r.PushClip(drawerRect);

    const float pad = std::floor(16 * s);
    float y = drawerRect.top + std::floor(16 * s);

    // Baslik & Kapatma Butonu
    m_r.Text(L"☰ Altyapi & Oturum Menusu",
             D2D1::RectF(drawerRect.left + pad, y, drawerRect.right - std::floor(44 * s), y + std::floor(26 * s)),
             theme::TextHi, 15.0f * s, Renderer::Align::Left, true);

    const D2D1_RECT_F closeBtnRect = D2D1::RectF(drawerRect.right - std::floor(40 * s), y,
                                                 drawerRect.right - pad, y + std::floor(26 * s));
    if (m_ui.Button(9300, closeBtnRect, L"✕")) {
        ToggleSlideDrawer();
        m_r.PopClip();
        return;
    }
    y += std::floor(36 * s);
    m_r.Fill(D2D1::RectF(drawerRect.left, y, drawerRect.right, y + 1), theme::Border);
    y += std::floor(12 * s);

    // --- 1. Aktif Sekmeler ---
    m_ui.Caption(D2D1::RectF(drawerRect.left + pad, y, drawerRect.right - pad, y + std::floor(18 * s)),
                 L"AKTIF SEKME VE OTURUMLAR (" + std::to_wstring(m_tabs.size()) + L")");
    y += std::floor(24 * s);

    const size_t maxTabShow = std::min((size_t)4, m_tabs.size());
    for (size_t ti = 0; ti < maxTabShow; ++ti) {
        if (!m_tabs[ti]) continue;
        const D2D1_RECT_F tr = D2D1::RectF(drawerRect.left + pad, y, drawerRect.right - pad, y + std::floor(34 * s));
        const bool active = (ti == m_active);

        m_r.FillRound(tr, std::floor(5 * s), active ? theme::Elevated : theme::BgCard);
        m_r.Stroke(tr, active ? theme::Ac() : theme::Border, std::max(1.0f, std::floor(1 * s)));

        const ShellProfile& p = m_tabs[ti]->profile();
        const D2D1_RECT_F bRect = D2D1::RectF(tr.left + std::floor(6 * s), tr.top + std::floor(6 * s),
                                              tr.left + std::floor(50 * s), tr.bottom - std::floor(6 * s));
        m_ui.Badge(bRect, p.badge.empty() ? L"TAB" : p.badge, p.accent ? p.accent : theme::Ac());

        std::wstring title = Trunc(m_tabs[ti]->Title(), 24);
        m_r.Text(title, D2D1::RectF(bRect.right + std::floor(8 * s), tr.top, tr.right - std::floor(36 * s), tr.bottom),
                 active ? theme::TextHi : theme::Text, 11.5f * s, Renderer::Align::Left, active);

        // Sekmeyi kapatma tusu
        const D2D1_RECT_F cTabBtn = D2D1::RectF(tr.right - std::floor(30 * s), tr.top + std::floor(4 * s),
                                                tr.right - std::floor(4 * s), tr.bottom - std::floor(4 * s));
        if (m_ui.Button(9320 + (int)ti, cTabBtn, L"✕")) {
            CloseTab(ti);
            m_r.PopClip();
            return;
        }

        if (ClickIn(tr) && !m_ui.Hot(cTabBtn)) {
            SelectTab(ti);
            SetView(View::Terminal);
            ToggleSlideDrawer();
            m_r.PopClip();
            return;
        }
        y += std::floor(38 * s);
    }

    if (m_ui.Button(9330, D2D1::RectF(drawerRect.left + pad, y, drawerRect.left + pad + std::floor(130 * s), y + std::floor(28 * s)), L"+ Yeni Sekme")) {
        NewTab(0);
        ToggleSlideDrawer();
        m_r.PopClip();
        return;
    }
    y += std::floor(36 * s);

    // --- 2. Altyapi ve Hizli Erisim ---
    m_r.Fill(D2D1::RectF(drawerRect.left, y, drawerRect.right, y + 1), theme::Border);
    y += std::floor(10 * s);

    m_ui.Caption(D2D1::RectF(drawerRect.left + pad, y, drawerRect.right - pad, y + std::floor(18 * s)),
                 L"ALTYAPI & HIZLI ERISIM (DOCKER / WSL / K8S)");
    y += std::floor(24 * s);

    const size_t maxHubShow = std::min((size_t)4, m_hubNodes.size());
    for (size_t hi = 0; hi < maxHubShow; ++hi) {
        const auto& node = m_hubNodes[hi];
        const D2D1_RECT_F hr = D2D1::RectF(drawerRect.left + pad, y, drawerRect.right - pad, y + std::floor(36 * s));
        m_r.FillRound(hr, std::floor(5 * s), theme::BgCard);
        m_r.Stroke(hr, theme::Border, std::max(1.0f, std::floor(1 * s)));

        uint32_t bcol = (node.type == NodeType::DockerContainer) ? 0x0EA5E9
                      : (node.type == NodeType::Wsl)             ? 0x22C55E
                      : (node.type == NodeType::K8sPod)          ? 0x326CE5
                      : (node.type == NodeType::SshHost)         ? theme::Red : theme::Ac();
        std::wstring btext = (node.type == NodeType::DockerContainer) ? L"DKR"
                           : (node.type == NodeType::Wsl)             ? L"WSL"
                           : (node.type == NodeType::K8sPod)          ? L"K8S"
                           : (node.type == NodeType::SshHost)         ? L"SSH" : L"SYS";

        const D2D1_RECT_F hbRect = D2D1::RectF(hr.left + std::floor(6 * s), hr.top + std::floor(6 * s),
                                               hr.left + std::floor(50 * s), hr.bottom - std::floor(6 * s));
        m_ui.Badge(hbRect, btext, bcol);

        m_r.Text(Trunc(Utf8ToWide(node.name), 22),
                 D2D1::RectF(hbRect.right + std::floor(8 * s), hr.top, hr.right - std::floor(70 * s), hr.bottom),
                 theme::Text, 11.5f * s, Renderer::Align::Left, true);

        const D2D1_RECT_F goBtn = D2D1::RectF(hr.right - std::floor(64 * s), hr.top + std::floor(4 * s),
                                              hr.right - std::floor(6 * s), hr.bottom - std::floor(4 * s));
        if (m_ui.Button(9340 + (int)hi, goBtn, L"Baslat")) {
            LaunchNode(node);
            ToggleSlideDrawer();
            m_r.PopClip();
            return;
        }
        y += std::floor(40 * s);
    }
    y += std::floor(6 * s);

    // --- 3. Kubernetes YAML Yonetimi ---
    m_r.Fill(D2D1::RectF(drawerRect.left, y, drawerRect.right, y + 1), theme::Border);
    y += std::floor(10 * s);

    m_ui.Caption(D2D1::RectF(drawerRect.left + pad, y, drawerRect.right - pad, y + std::floor(18 * s)),
                 L"KUBERNETES YAML YONETIMI");
    y += std::floor(22 * s);

    const auto& manifests = K8sManager::Instance().Manifests();
    const auto& pods = K8sManager::Instance().Pods();
    wchar_t kstat[160];
    swprintf_s(kstat, L"%zu Manifest  |  %zu Terminal hedefi  |  %zu kubeconfig", manifests.size(), pods.size(),
               K8sManager::Instance().KubeconfigCount());
    m_r.Text(kstat, D2D1::RectF(drawerRect.left + pad, y, drawerRect.right - pad, y + std::floor(20 * s)),
             theme::AcHi(), 11.5f * s, Renderer::Align::Left, true);
    y += std::floor(24 * s);

    const float btnHalfW = std::floor((drawerW - pad * 2 - std::floor(10 * s)) * 0.5f);
    if (m_ui.Button(9350, D2D1::RectF(drawerRect.left + pad, y, drawerRect.left + pad + btnHalfW, y + std::floor(30 * s)), L"+ YAML Ekle")) {
        std::wstring file;
        if (PickYamlFile(m_hwnd, file)) {
            std::wstring ierr;
            if (K8sManager::Instance().ImportYamlFile(file, &ierr)) {
                RefreshHubNodes();
                const std::wstring ctx = K8sManager::Instance().KubeContextSummary();
                Toast(ctx.empty() ? L"Kubernetes YAML dosyasi eklendi"
                                  : L"YAML eklendi. Yeni terminallerde: " + ctx);
            } else {
                Toast(L"YAML ice aktarilamadi: " + (ierr.empty() ? std::wstring(L"gecerli kaynak yok") : ierr));
            }
        }
    }
    if (m_ui.Button(9351, D2D1::RectF(drawerRect.left + pad + btnHalfW + std::floor(10 * s), y, drawerRect.right - pad, y + std::floor(30 * s)), L"📥 YAML Export", true)) {
        std::wstring exportPath = m_dataDir + L"\\k8s\\fullterminal-export.yaml";
        if (K8sManager::Instance().ExportInventory(m_inv, m_hubNodes, exportPath)) {
            RefreshHubNodes();
            Toast(L"K8s manifesti export edildi: portable_data/k8s/...");
        }
    }
    y += std::floor(42 * s);

    // --- 4. Hizli Ayarlar ve Mod Gecisi ---
    m_r.Fill(D2D1::RectF(drawerRect.left, y, drawerRect.right, y + 1), theme::Border);
    y += std::floor(12 * s);

    if (m_ui.Button(9360, D2D1::RectF(drawerRect.left + pad, y, drawerRect.left + pad + btnHalfW, y + std::floor(32 * s)), L"⚙ Ayarlar")) {
        SetView(View::Settings);
        ToggleSlideDrawer();
        m_r.PopClip();
        return;
    }
    const std::wstring qLabel = InQuake() ? L"Normal pencere" : L"Visor modu";
    if (m_ui.Button(9361, D2D1::RectF(drawerRect.left + pad + btnHalfW + std::floor(10 * s), y, drawerRect.right - pad, y + std::floor(32 * s)), qLabel)) {
        // Cizimin ortasinda pencere stilini degistirme; mesaj olarak ertele.
        PostQuake(InQuake() ? QC_EXIT : QC_ENTER);
        ToggleSlideDrawer();
        m_r.PopClip();
        return;
    }

    m_r.PopClip();
}


} // namespace ft
