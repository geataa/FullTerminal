#define _CRT_RAND_S

#include "ui/MainWindowInternal.h"

namespace ft {

// ----------------------------------------------------------------- kurma ---

const wchar_t* MainWindow::ClassName() { return kClassName; }

size_t MainWindow::DefaultProfileIndex() const {
    for (size_t i = 0; i < m_profiles.size(); ++i) {
        if (!m_cfg.defaultProfile.empty() && m_profiles[i].id == m_cfg.defaultProfile) return i;
    }
    return 0;
}

bool MainWindow::Create(HINSTANCE inst, std::wstring* err) {
    m_inst = inst;

    if (!ConPty::Available()) {
        if (err) *err = L"Bu Windows surumunde ConPTY yok. Windows 10 1809 veya ustu gerekiyor.";
        return false;
    }

    m_dataDir = PortableDataDir();
    m_cfg.Load(m_dataDir);
    I18n::Init(m_cfg.language);
    m_inv.Load(m_dataDir);
    m_knownHosts.Load();
    m_sftp = std::make_unique<SftpController>(m_inv);
    m_snippets.Load(m_dataDir);
    m_tunnels.Load(m_dataDir);
    K8sManager::Instance().Init(m_dataDir);
    ApplyColorScheme();
    theme::SetAccent(Accent());   // simge asagida bu renkle uretiliyor

    m_profiles = DiscoverShellProfiles();
    if (m_profiles.empty()) {
        if (err) *err = L"Hicbir kabuk bulunamadi.";
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &MainWindow::WndProcStatic;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                   LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        if (err) *err = L"Pencere sinifi kaydedilemedi.";
        return false;
    }

    const UINT dpi = DpiFor(nullptr);
    const float s = dpi / 96.0f;

    m_hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kClassName, L"FullTerminal", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, (int)(1320 * s), (int)(800 * s),
                             nullptr, nullptr, inst, this);
    if (!m_hwnd) {
        if (err) *err = L"Pencere olusturulamadi.";
        return false;
    }

    m_dpi = DpiFor(m_hwnd);
    m_ui.SetOwner(m_hwnd);
    EnableRoundedCorners(m_hwnd);
    EnableWindowTransparency(m_hwnd);
    ChangeWindowMessageFilterEx(m_hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(m_hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(m_hwnd, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);
    DragAcceptFiles(m_hwnd, TRUE);

    m_icon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!m_icon) m_icon = MakeAppIcon(32, theme::Ac());
    if (m_icon) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)m_icon);
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)m_icon);
    }

    // Cerceve olculeri olusturma aninda onbelleklenir; WM_NCCALCSIZE'in etkili
    // olmasi icin bir kere frame degisikligi bildirmek sart.
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!m_r.Init(m_hwnd, m_dpi, err)) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }
    if (!m_cfg.fontFamily.empty()) m_r.SetFontFamily(m_cfg.fontFamily);
    m_r.SetFontSizePt(m_cfg.fontPt);

    ComputeLayout();
    RefreshHubNodes();

    if (!RestoreSession()) {
        NewTab(DefaultProfileIndex());
    }
    if (const int locked = m_inv.LockedSecretCount(); locked > 0) {
        Toast(std::to_wstring(locked) + L" kayitli parola bu Windows kullanicisinda cozulemedi "
              L"(portable_data baska makineden mi geldi?). Hostlarda yeniden gir.");
    }

    SetTimer(m_hwnd, kBlinkTimer, kBlinkMs, nullptr);
    m_fpsStart = NowTicks();

    // Global kisayol YALNIZCA Guake modunda kaydedilir; kapaliyken F12 gibi
    // bir tusu sistem genelinde calmak tarayicinin gelistirici aracini bile bozar.
    if (m_cfg.quakeMode) {
        m_cfg.quakeMode = false;
        EnterQuakeMode();
    }

    return true;
}

void MainWindow::Show(int nCmdShow) {
    if (InQuake()) return;   // EnterQuakeMode pencereyi zaten kendisi indiriyor
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
}

LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool MainWindow::TerminalHasKeyboard() const {
    if (m_view != View::Terminal || m_ui.Focus() != ID_NONE || m_renamingTab) return false;
    if (m_active < m_tabs.size() && m_tabs[m_active] && m_tabs[m_active]->IsSftp()) return false;
    return true;
}

uint32_t MainWindow::Accent() const {
    return theme::AccentChoices[std::clamp(m_cfg.accentChoice, 0, 4)];
}

bool MainWindow::ClickIn(const D2D1_RECT_F& r) const {
    return m_ui.Clicked(r);
}

bool MainWindow::DblClickIn(const D2D1_RECT_F& r) const {
    return m_ui.DblClicked(r);
}

void MainWindow::Toast(const std::wstring& text) {
    m_toast = text;
    m_toastUntil = NowTicks() + TickFreq() * 2;
    m_dirty = true;
}

// --------------------------------------------------------------- mesajlar --

LRESULT MainWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    // Explorer yeniden baslarsa tepsi simgeleri silinir; arka planda/Guake'de
    // calisan bir uygulama icin tek geri donus yolu o simge.
    static const UINT kTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (kTaskbarCreated && msg == kTaskbarCreated) {
        if (m_trayVisible) { m_trayVisible = false; AddTrayIcon(); }
        return 0;
    }

    switch (msg) {
    case WM_USER + 777:
        m_dirty = true;
        return 0;

    case WM_NCCALCSIZE:
        if (wp == TRUE) {
            if (IsZoomed(m_hwnd)) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                const int cx = MetricFor(SM_CXSIZEFRAME, m_dpi) + MetricFor(SM_CXPADDEDBORDER, m_dpi);
                const int cy = MetricFor(SM_CYSIZEFRAME, m_dpi) + MetricFor(SM_CXPADDEDBORDER, m_dpi);
                p->rgrc[0].left += cx; p->rgrc[0].right -= cx;
                p->rgrc[0].top += cy;  p->rgrc[0].bottom -= cy;
            }
            return 0;
        }
        break;

    case WM_HOTKEY:
        if (wp == kQuakeHotKeyId) {
            QuakeToggle();
            return 0;
        }
        break;

    case WM_SUMMON:   // ikinci kopya calistirildi
        if (InQuake()) {
            if (m_quakeState == QuakeState::Visible) ForceForeground();
            else QuakeShow();
        } else {
            RestoreFromTray();
        }
        return 0;

    case WM_QUAKE_CMD:
        switch ((int)wp) {
        case QC_ENTER:   EnterQuakeMode(); break;
        case QC_EXIT:    ExitQuakeMode(); break;
        case QC_REAPPLY: QuakeReapply(); break;
        case QC_FULL:    if (InQuake()) { m_quakeFull = !m_quakeFull; QuakeReapply(); } break;
        case QC_HIDE:    if (InQuake()) QuakeHide(); break;
        default: break;
        }
        return 0;

    case WM_COPYDATA:
        return OnCopyData(reinterpret_cast<HWND>(wp), reinterpret_cast<const COPYDATASTRUCT*>(lp));

    case WM_DROPFILES: {
        HDROP hDrop = reinterpret_cast<HDROP>(wp);
        POINT pt{};
        DragQueryPoint(hDrop, &pt);
        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> droppedFiles;
        for (UINT i = 0; i < fileCount; ++i) {
            wchar_t pathBuf[MAX_PATH * 2]{};
            if (DragQueryFileW(hDrop, i, pathBuf, static_cast<UINT>(std::size(pathBuf)))) {
                droppedFiles.push_back(pathBuf);
            }
        }
        DragFinish(hDrop);
        if (!droppedFiles.empty()) {
            OnFilesDropped(droppedFiles, pt);
        }
        return 0;
    }

    case WM_SETCURSOR:
        // Guake: alt kenar yukseklik tutamagi
        if (LOWORD(lp) == HTCLIENT && m_quakeState == QuakeState::Visible) {
            POINT pt{}; GetCursorPos(&pt); ScreenToClient(m_hwnd, &pt);
            RECT rc{}; GetClientRect(m_hwnd, &rc);
            if (m_quakeResizing || pt.y >= rc.bottom - QuakeGrip()) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                return TRUE;
            }
        }
        // OSC 8 / URL Baglanti imleci: Ctrl basiliyken baglanti uzerindeyse el imleci
        if (LOWORD(lp) == HTCLIENT && (GetKeyState(VK_CONTROL) & 0x8000) != 0 && m_view == View::Terminal) {
            POINT pt{}; GetCursorPos(&pt); ScreenToClient(m_hwnd, &pt);
            int col = 0, row = 0;
            if (CellFromPoint(pt.x, pt.y, col, row)) {
                if (auto* t = Active()) {
                    if (!t->screen().GetLinkAt(col, row).empty()) {
                        SetCursor(LoadCursorW(nullptr, IDC_HAND));
                        return TRUE;
                    }
                }
            }
        }
        break;

    case WM_SYSCOMMAND:
        // Tek basina Alt veya F10: gorunmez sistem menusune girilip siradaki
        // tus yutulmasin. Alt+Bosluk (lp == ' ') menuyu acmaya devam eder.
        if ((wp & 0xFFF0) == SC_KEYMENU && lp == 0) return 0;
        if (InQuake()) {
            const UINT sc = (UINT)(wp & 0xFFF0);
            if (sc == SC_MINIMIZE) { QuakeHide(); return 0; }
            if (sc == SC_MAXIMIZE) { PostQuake(QC_FULL); return 0; }
            if (sc == SC_MOVE || sc == SC_SIZE || sc == SC_RESTORE) return 0;
        }
        break;

    case WM_NCHITTEST: {
        if (InQuake()) {
            return HTCLIENT;   // surukleme/boyut yok; alt kenar tutamagini biz yonetiyoruz
        }
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(m_hwnd, &pt);
        RECT rc{}; GetClientRect(m_hwnd, &rc);
        const int b = (int)(6 * m_lay.scale);
        if (!IsZoomed(m_hwnd)) {
            const bool L = pt.x < b, R = pt.x >= rc.right - b;
            const bool T = pt.y < b, B = pt.y >= rc.bottom - b;
            if (T && L) return HTTOPLEFT;
            if (T && R) return HTTOPRIGHT;
            if (B && L) return HTBOTTOMLEFT;
            if (B && R) return HTBOTTOMRIGHT;
            if (L) return HTLEFT;
            if (R) return HTRIGHT;
            if (T) return HTTOP;
            if (B) return HTBOTTOM;
        }
        // Cekmece veya modal diyaloglar tum istemci alanini kaplar: baslik altinda kalsa bile
        // surukleme/cift tik/pencere dugmesi olmasin, tik modal veya cekmeceye gitsin.
        if (DrawerUp() || m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt) return HTCLIENT;
        if (pt.y < m_lay.titleH) {
            if (HitRect(m_lay.btnMin, pt.x, pt.y) || HitRect(m_lay.btnMax, pt.x, pt.y) ||
                HitRect(m_lay.btnClose, pt.x, pt.y) || HitRect(m_lay.btnQuake, pt.x, pt.y) ||
                HitRect(m_lay.newTab, pt.x, pt.y) || HitRect(m_lay.menuBtn, pt.x, pt.y) ||
                HitRect(m_lay.overflow, pt.x, pt.y) ||
                HitRect(m_lay.btnSplitV, pt.x, pt.y) || HitRect(m_lay.btnSplitH, pt.x, pt.y) ||
                HitRect(m_lay.btnSync, pt.x, pt.y) || HitTab(pt.x, pt.y) >= 0) {
                return HTCLIENT;
            }
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            // Guake penceresi simge durumuna inmez (Win+D vb.); gizlenmis sayilir.
            // QuakeShow simge durumundaysa once geri yukler.
            if (InQuake()) {
                ShowWindow(m_hwnd, SW_HIDE);
                m_quakeState = QuakeState::Hidden;
                m_quakeFrac = 0.0f;
                return 0;
            }
            if (m_cfg.minimizeToTray) { HideToTray(); return 0; }
            // Simge boyutuyla izgara 8x2'ye inip gorunur icerik yok olurdu;
            // geri yuklemede ayni boyut gelir, Resize hic calismaz.
            return 0;
        }
        OnResize();
        return 0;

    case WM_DPICHANGED: {
        m_dpi = HIWORD(wp);
        m_r.SetDpi(m_dpi);
        if (InQuake()) {
            // Onerilen dikdortgen normal pencere icindir; Guake geometrisini
            // calisma alanindan kendimiz hesapliyoruz.
            if (m_quakeState == QuakeState::Visible) PostQuake(QC_REAPPLY);
        } else {
            RECT* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(m_hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        OnResize();
        return 0;
    }

    case WM_PTY_DATA: m_dirty = true; return 0;

    case WM_HUB_NODES: {
        std::unique_ptr<HubProbeResult> res(reinterpret_cast<HubProbeResult*>(lp));
        m_hubProbing = false;
        if (res && res->gen == m_hubGen) {
            m_slowNodes = std::move(res->nodes);
            m_hubNodes = m_fastNodes;
            m_hubNodes.insert(m_hubNodes.end(), m_slowNodes.begin(), m_slowNodes.end());
            m_dirty = true;
        }
        if (m_hubProbeQueued) { m_hubProbeQueued = false; StartHubProbe(); }
        return 0;
    }

    // Oturum kapanisi/yeniden baslatma WM_CLOSE gondermez; surec burada biter.
    case WM_QUERYENDSESSION:
        m_cfg.Save(m_dataDir);
        m_inv.Save(m_dataDir);
        return TRUE;
    case WM_ENDSESSION:
        if (wp) {
            m_cfg.Save(m_dataDir);
            m_inv.Save(m_dataDir);
        }
        return 0;

    case WM_TRAY:
        if (InQuake()) {
            // Tek tik goster/odakla. Tepsiye tiklamak gorev cubugunu one getirir;
            // "odak kaybinda gizle" aciksa pencere az once gizlenmistir; bu tik
            // onu geri cagirmasin, kullanici zaten gizlemek istedi.
            if (LOWORD(lp) == WM_LBUTTONUP) {
                const bool justAutoHidden = m_lastAutoHide &&
                    (NowTicks() - m_lastAutoHide) < TickFreq() * 2 / 5;
                if (!justAutoHidden) {
                    if (m_quakeState == QuakeState::Visible) ForceForeground();
                    else QuakeShow();
                }
            } else if (LOWORD(lp) == WM_RBUTTONUP) {
                ShowTrayMenu();
            }
            return 0;
        }
        if (LOWORD(lp) == WM_LBUTTONDBLCLK || LOWORD(lp) == WM_LBUTTONUP) RestoreFromTray();
        else if (LOWORD(lp) == WM_RBUTTONUP) ShowTrayMenu();
        return 0;

    case WM_TIMER:
        if (wp == kBlinkTimer) {
            m_cursorOn = m_cfg.cursorBlink ? !m_cursorOn : true;
            m_ui.SetCaretVisible(m_cursorOn);
            m_dirty = true;
        }
        return 0;

    case WM_SETFOCUS:  m_focused = true;  m_dirty = true; return 0;
    case WM_KILLFOCUS: m_focused = false; m_dirty = true; return 0;
    case WM_ACTIVATE:  m_focused = (LOWORD(wp) != WA_INACTIVE); m_dirty = true; break;

    case WM_ACTIVATEAPP:
        // WM_KILLFOCUS degil: kendi mesaj kutumuz, dosya diyalogumuz veya
        // acilir menumuz odagi alinca Guake gizlenmemeli. ACTIVATEAPP yalnizca
        // BASKA bir uygulama one gecince FALSE gelir.
        if (!wp && InQuake() && m_cfg.quakeHideOnLoseFocus && !m_quakeResizing && !m_captureHotkey &&
            (m_quakeState == QuakeState::Visible || m_quakeState == QuakeState::DroppingDown)) {
            m_lastAutoHide = NowTicks();
            QuakeHide();
        }
        break;

    case WM_CAPTURECHANGED:
        if (m_quakeResizing && (HWND)lp != m_hwnd) {
            m_quakeResizing = false;
            m_cfg.Save(m_dataDir);
            m_dirty = true;
        }
        if (m_mouseBtn >= 0 && (HWND)lp != m_hwnd) {
            // Yakalama elden gitti: uygulamada dugme basili kalmasin.
            const int b = m_mouseBtn;
            m_mouseBtn = -1;
            SendMouseReport(b, false, false, m_mouseCol, m_mouseRow);
        }
        break;

    case WM_CHAR:
        OnChar((wchar_t)wp, false);
        return 0;

    case WM_SYSCHAR:
        // Alt+Bosluk: DefWindowProc sistem menusunu acar (Tasi/Boyut/Kapat).
        // Kisayol yakalama tusu aldiysa (m_swallowChar) menu acilmaz.
        if (wp == L' ') {
            if (m_swallowChar) { m_swallowChar = false; return 0; }
            break;
        }
        OnChar((wchar_t)wp, true);   // Alt+harf: Meta, ESC onekiyle gider
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        // Alt+F4 terminale ESC[1;3S olarak gitmesin; WM_CLOSE yolundan gecsin
        // (Guake'de gizler, confirmClose sorusu orada).
        if (msg == WM_SYSKEYDOWN && wp == VK_F4 && !m_captureHotkey &&
            !(GetKeyState(VK_CONTROL) & 0x8000)) {
            m_swallowChar = false;
            if (!(lp & (1 << 30))) PostMessageW(m_hwnd, WM_CLOSE, 0, 0);   // tekrar degil
            return 0;
        }
        OnKeyDown(wp, msg == WM_SYSKEYDOWN);
        if (msg == WM_SYSKEYDOWN && wp != VK_F10 && wp != VK_MENU) return 0;
        break;

    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (m_quakeResizing) {
            // Ekran koordinatiyla: pencere buyudukce istemci koordinati kayar.
            POINT sp{}; GetCursorPos(&sp);
            const int workH = m_quakeWork.bottom - m_quakeWork.top;
            if (workH > 0) {
                const int h = std::clamp((int)(sp.y - m_quakeRect.top), workH / 5, workH);
                if (h != m_quakeRect.bottom - m_quakeRect.top) {
                    m_quakeRect.bottom = m_quakeRect.top + h;
                    m_quakeVisH = h;
                    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, m_quakeRect.right - m_quakeRect.left, h,
                                 SWP_NOMOVE | SWP_NOACTIVATE);
                }
                m_cfg.quakeHeightPercent = std::clamp((h * 100 + workH / 2) / workH, 20, 100);
                m_quakeFull = false;
            }
            m_dirty = true;
            return 0;
        }
        m_in.mx = (float)x; m_in.my = (float)y;
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);

        if (DrawerUp()) {
            // Cekmece acik: alttaki baslik/sekme vurgusu ve secim guncellenmez.
            m_hoverTab = m_hoverBtn = m_hoverClose = -1;
            m_dirty = true;
            return 0;
        }

        if (m_mouseBtn >= 0 || MouseReporting((wp & MK_SHIFT) != 0)) {
            if (auto* t = Active()) {
                const int mode = t->screen().MouseMode();
                // 1002: yalniz basiliyken, 1003: her harekette; ayni hucrede tekrar yok
                const bool drag = (m_mouseBtn >= 0) && mode >= 1002;
                const bool any  = (m_mouseBtn < 0) && mode == 1003;
                int col = 0, row = 0;
                if (drag) CellFromPointClamped(x, y, col, row);
                const bool inside = drag || (any && CellFromPoint(x, y, col, row));
                if (inside && (col != m_mouseCol || row != m_mouseRow)) {
                    SendMouseReport(drag ? m_mouseBtn : 3, true, true, col, row);
                }
            }
        }

        const int oldTab = m_hoverTab, oldBtn = m_hoverBtn, oldX = m_hoverClose;
        m_hoverTab = HitTab(x, y);
        m_hoverClose = HitTabClose(x, y);
        m_hoverBtn = HitRect(m_lay.btnClose, x, y) ? BTN_CLOSE
                   : HitRect(m_lay.btnMax, x, y)   ? BTN_MAX
                   : HitRect(m_lay.btnMin, x, y)   ? BTN_MIN
                   : HitRect(m_lay.btnQuake, x, y) ? BTN_QUAKE
                   : HitRect(m_lay.newTab, x, y)   ? BTN_NEWTAB
                   : HitRect(m_lay.overflow, x, y) ? BTN_OVERFLOW
                   : HitRect(m_lay.menuBtn, x, y)  ? BTN_MENU : -1;
        if (oldTab != m_hoverTab || oldBtn != m_hoverBtn || oldX != m_hoverClose) m_dirty = true;
        // Ayni pill icinde metinden kapatma kutusuna gecmek yukaridakilerin
        // hicbirini degistirmez; baslik cubugunda her hareket kare istiyor.
        if (y < (int)m_lay.titleH) m_dirty = true;
        if (m_view != View::Terminal || HitRect(m_lay.side, x, y) || DrawerUp() || m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt) m_dirty = true;
        if (InQuake() && y >= (int)m_lay.status.top) m_dirty = true;   // tutamak vurgusu

        if (m_selecting) {
            int col, row;
            if (CellFromPoint(x, y, col, row)) {
                if (auto* t = Active()) {
                    m_sel.x1 = col;
                    m_sel.y1 = t->screen().ViewRowToAbs(row);
                    m_dirty = true;
                }
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_hoverTab = -1; m_hoverBtn = -1;
        m_hoverClose = -1; m_pressClose = -1;
        // Bekleyen bir tik henuz cizimde tuketilmediyse konumu silme: tiklayip
        // hemen pencereden cikan (veya LEAVE'i tiktan sonra kuyruga giren) fare
        // tiki kaybettiriyordu. Konum, tik islenen karenin sonunda temizlenir.
        if (m_in.clicked || m_in.dblClick) m_leavePending = true;
        else m_in.mx = m_in.my = -1.0f;
        m_dirty = true;
        return 0;

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        SetFocus(m_hwnd);
        m_in.mx = (float)x; m_in.my = (float)y;
        m_in.px = m_in.py = -1.0f;   // asagida arayuze dusmezse hicbir widget'a ait degil
        m_in.down = true;
        m_dirty = true;

        if (m_quakeState == QuakeState::Visible) {
            RECT rc{}; GetClientRect(m_hwnd, &rc);
            if (y >= rc.bottom - QuakeGrip()) {
                m_quakeResizing = true;
                m_in.down = false;
                SetCapture(m_hwnd);
                return 0;
            }
        }

        // Cekmece veya modal pencereler (sekme adlandirma vb.) kalicidir: alttaki
        // baslik dugmeleri, sekmeler ve terminal tik almaz.
        if (DrawerUp() || m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt) {
            m_in.px = (float)x; m_in.py = (float)y;
            return 0;
        }

        // Guake'de kapat/kucult pencereyi gizler: gorev cubugu dugmesi yok,
        // kapanan bir acilir konsol kisayolla geri gelemezdi.
        if (HitRect(m_lay.btnClose, x, y)) {
            if (InQuake()) QuakeHide(); else PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (HitRect(m_lay.btnMin, x, y)) {
            if (InQuake()) QuakeHide(); else ShowWindow(m_hwnd, SW_MINIMIZE);
            return 0;
        }
        if (HitRect(m_lay.btnMax, x, y)) {
            if (InQuake()) PostQuake(QC_FULL);
            else ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        if (HitRect(m_lay.btnQuake, x, y)) { PostQuake(InQuake() ? QC_EXIT : QC_ENTER); return 0; }
        if (HitRect(m_lay.menuBtn, x, y))  { m_sidebarOpen = !m_sidebarOpen; ComputeLayout(); SyncGridToArea(); return 0; }
        if (HitRect(m_lay.newTab, x, y)) {
            POINT p{ (LONG)m_lay.newTab.left, (LONG)m_lay.newTab.bottom };
            ClientToScreen(m_hwnd, &p);
            ShowProfileMenu(p);
            return 0;
        }
        if (HitRect(m_lay.btnSplitV, x, y) || HitRect(m_lay.btnSplitH, x, y) || HitRect(m_lay.btnSync, x, y)) {
            m_in.px = (float)x; m_in.py = (float)y;
            return 0;
        }
        // Kapatma yikici: mouse-DOWN'da yalniz kaydedilir, mouse-UP'ta calisir.
        if (const int c = HitTabClose(x, y); c >= 0) {
            m_pressClose = c;
            return 0;
        }

        // Tasma cipi: tum sekmelerin listesi
        if (m_lay.hidden > 0 && HitRect(m_lay.overflow, x, y)) {
            std::vector<std::wstring> items;
            items.reserve(m_tabs.size());
            for (size_t i = 0; i < m_tabs.size(); ++i) {
                if (!m_tabs[i]) continue;
                std::wstring line = (i == m_active) ? L"\u2022 " : L"   ";
                const std::wstring& bd = m_tabs[i]->profile().badge;
                if (!bd.empty()) line += L"[" + bd + L"] ";
                line += Trunc(m_tabs[i]->Title(), 56);
                items.push_back(MenuEscape(line));
            }
            POINT pm{ (LONG)m_lay.overflow.left, (LONG)m_lay.overflow.bottom };
            ClientToScreen(m_hwnd, &pm);
            const int sel = ShowListMenu(pm, items);
            if (sel >= 0 && (size_t)sel < m_tabs.size()) {
                SelectTab((size_t)sel);
                SetView(View::Terminal);
            }
            return 0;
        }

        const int t = HitTab(x, y);
        if (t >= 0) { SelectTab((size_t)t); SetView(View::Terminal); return 0; }

        if (m_view == View::Terminal) {
            if (auto* tab = Active(); tab && tab->IsSftp()) {
                m_in.px = static_cast<float>(x);
                m_in.py = static_cast<float>(y);
                return 0;
            }

            // Ajan Onay Seridine tiklandiysa basisi UI widget'ina aktar
            if (HitAgentRibbon(x, y)) {
                m_in.px = static_cast<float>(x);
                m_in.py = static_cast<float>(y);
                return 0;
            }

            if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active] && m_tabLayouts[m_active]->PaneCount() > 1) {
                auto panes = m_tabLayouts[m_active]->ComputeLayout(m_lay.term);
                uint32_t hitId = m_tabLayouts[m_active]->HitTestPane(panes, (float)x, (float)y);
                if (hitId != 0 && hitId != m_tabLayouts[m_active]->GetFocusedPaneId()) {
                    m_tabLayouts[m_active]->SetFocusedPane(hitId);
                    m_tabs[m_active] = m_tabLayouts[m_active]->GetFocusedTab();
                    m_dirty = true;
                }
            }
            int col, row;
            if (CellFromPoint(x, y, col, row)) {
                m_ui.SetFocus(ID_NONE);
                if (auto* tab = Active()) {
                    if ((wp & MK_CONTROL) != 0) {
                        std::string url = tab->screen().GetLinkAt(col, row);
                        if (!url.empty()) {
                            ShellExecuteW(nullptr, L"open", Utf8ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                            Toast(L"Bağlantı açılıyor: " + Trunc(Utf8ToWide(url), 35));
                            return 0;
                        }
                    }
                }
                if (MouseReporting((wp & MK_SHIFT) != 0)) {
                    // Uygulama fareyi istedi (vim, htop, mc); Shift+surukle yerel secim.
                    ClearSelection();
                    m_mouseBtn = 0;
                    SendMouseReport(0, true, false, col, row);
                    SetCapture(m_hwnd);
                    return 0;
                }
                if (auto* tab = Active()) {
                    m_selecting = true;
                    m_sel.active = true;
                    m_sel.alt = tab->screen().IsAltBuffer();
                    m_sel.x0 = m_sel.x1 = col;
                    m_sel.y0 = m_sel.y1 = tab->screen().ViewRowToAbs(row);
                    SetCapture(m_hwnd);
                }
                return 0;
            }
        }
        m_in.px = (float)x; m_in.py = (float)y;   // basis anlik-mod arayuzune ait
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        m_in.mx = (float)x; m_in.my = (float)y;
        m_in.px = (float)x; m_in.py = (float)y;
        m_in.dblClick = true;
        m_in.clicked = true;
        m_dirty = true;

        if (m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt) {
            m_in.px = (float)x; m_in.py = (float)y;
            return 0;
        }

        if (y < (int)m_lay.titleH) {
            int hit = HitTab(x, y);
            if (hit >= 0 && (size_t)hit < m_tabs.size()) {
                StartRenameTab((size_t)hit);
                return 0;
            }
        }

        // Cift tikin ikinci basisi WM_LBUTTONDOWN olarak gelmez; arayuz
        // dugmesi ikinci tiki da alabilsin diye basis noktasi burada kaydedilir.
        int col, row;
        const bool inTerm = (m_view == View::Terminal) && CellFromPoint(x, y, col, row);
        if (DrawerUp() || (y >= (int)m_lay.titleH && !inTerm)) {
            m_in.px = (float)x; m_in.py = (float)y;
        } else if (inTerm && MouseReporting((wp & MK_SHIFT) != 0)) {
            // Uygulama ikinci basisi da gormeli (cift tik secimi vb.)
            m_mouseBtn = 0;
            SendMouseReport(0, true, false, col, row);
            SetCapture(m_hwnd);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (m_quakeResizing) {
            m_quakeResizing = false;
            ReleaseCapture();
            m_cfg.Save(m_dataDir);
            m_dirty = true;
            return 0;
        }
        m_in.down = false;
        m_in.clicked = true;
        // Tik, birakildigi yerde degerlendirilir. Araya giren bir WM_MOUSELEAVE
        // (-1,-1) veya baska bir hareket mx/my'yi bayatlatmis olabilir.
        m_in.mx = (float)GET_X_LPARAM(lp);
        m_in.my = (float)GET_Y_LPARAM(lp);
        m_dirty = true;
        if (m_mouseBtn == 0) {
            int col, row;
            CellFromPointClamped(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), col, row);
            m_mouseBtn = -1;                 // ReleaseCapture'in CAPTURECHANGED'i tekrar gondermesin
            SendMouseReport(0, false, false, col, row);
            ReleaseCapture();
            return 0;
        }
        if (m_pressClose >= 0) {
            const int ux = GET_X_LPARAM(lp), uy = GET_Y_LPARAM(lp);
            const int c = m_pressClose;
            m_pressClose = -1;
            if (HitTabClose(ux, uy) == c && (size_t)c < m_tabs.size()) {
                CloseTab((size_t)c);
                Toast(L"Sekme kapatildi");
            }
            return 0;
        }
        if (m_selecting) {
            m_selecting = false;
            ReleaseCapture();
            if (m_sel.x0 == m_sel.x1 && m_sel.y0 == m_sel.y1) ClearSelection();
            else if (m_cfg.copyOnSelect) CopySelection();
        }
        return 0;
    }

    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        const int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        if (DrawerUp() || m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt) return 0;   // modal altindaki sekme kapanmasin, yapistirilmasin
        const int btn = (msg == WM_MBUTTONDOWN) ? 1 : 2;
        if (m_view == View::Terminal && m_mouseBtn < 0 && MouseReporting((wp & MK_SHIFT) != 0)) {
            int col, row;
            if (CellFromPoint(mx, my, col, row)) {
                m_mouseBtn = btn;
                SendMouseReport(btn, true, false, col, row);
                SetCapture(m_hwnd);
                return 0;
            }
        }
        if (msg == WM_RBUTTONDOWN) {
            if (m_view == View::Terminal && HitRect(m_lay.term, mx, my)) {
                if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
                    auto panes = m_tabLayouts[m_active]->ComputeLayout(m_lay.term);
                    uint32_t hitId = m_tabLayouts[m_active]->HitTestPane(panes, (float)mx, (float)my);
                    if (hitId != 0 && hitId != m_tabLayouts[m_active]->GetFocusedPaneId()) {
                        m_tabLayouts[m_active]->SetFocusedPane(hitId);
                        m_tabs[m_active] = m_tabLayouts[m_active]->GetFocusedTab();
                        m_dirty = true;
                    }
                }
            }
            break;
        }
        if (my < (int)m_lay.titleH) {
            // MIKRO kademede baslik gorunmuyor: hangi oturumu oldurdugunu
            // goremezsin, o yuzden orta tik orada kapatmaz.
            if (m_lay.tabTier > 0) {
                const int mt = HitTab(mx, my);
                if (mt >= 0 && (size_t)mt < m_tabs.size()) {
                    CloseTab((size_t)mt);
                    Toast(L"Sekme kapatildi");
                }
            }
            return 0;   // baslik cubugunda asla yapistirma
        }
        if (m_view == View::Terminal) PasteClipboard();
        return 0;
    }

    case WM_MBUTTONUP:
    case WM_RBUTTONUP: {
        const int btn = (msg == WM_MBUTTONUP) ? 1 : 2;
        if (m_mouseBtn == btn) {
            int col, row;
            CellFromPointClamped(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), col, row);
            m_mouseBtn = -1;
            SendMouseReport(btn, false, false, col, row);
            ReleaseCapture();
            return 0;
        }
        if (msg == WM_RBUTTONUP && !DrawerUp()) {
            const int mx = GET_X_LPARAM(lp);
            const int my = GET_Y_LPARAM(lp);
            POINT pt{ mx, my };
            ClientToScreen(m_hwnd, &pt);

            if (my < (int)m_lay.titleH) {
                int hit = HitTab(mx, my);
                if (hit >= 0 && (size_t)hit < m_tabs.size()) {
                    ShowTabContextMenu((size_t)hit, pt);
                    return 0;
                }
            }
            if (m_view == View::Terminal && HitRect(m_lay.term, mx, my)) {
                if (auto* tab = Active(); tab && tab->IsSftp()) {
                    ShowSftpContextMenu(pt, mx, my);
                } else {
                    ShowTerminalContextMenu(pt);
                }
                return 0;
            }
            if (m_view == View::Sftp && HitRect(m_lay.main, mx, my)) {
                ShowSftpContextMenu(pt, mx, my);
                return 0;
            }
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (delta == 0) return 0;
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(m_hwnd, &pt);
        if (DrawerUp()) { m_dirty = true; return 0; }   // alttaki terminal kaymasin
        // Hassas dokunmatik yuzey/pinch cok sayida kucuk delta yollar;
        // WHEEL_DELTA kesirleri biriktirilir (yon degisince sifirlanir).
        const bool ctrl  = (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0;
        const bool shift = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0;

        if (m_view == View::Terminal && !HitRect(m_lay.side, pt.x, pt.y)) {
            if (auto* tab = Active(); tab && tab->IsSftp()) {
                const float mid = std::floor((m_lay.term.left + m_lay.term.right) * 0.5f);
                const float step = (float)delta * 50.0f / (float)WHEEL_DELTA * m_lay.scale;
                if (pt.x < mid) {
                    m_sftpLocalScroll = std::max(0.0f, m_sftpLocalScroll - step);
                } else {
                    m_sftpRemoteScroll = std::max(0.0f, m_sftpRemoteScroll - step);
                }
                m_dirty = true;
            } else {
                SendWheel(delta, shift, ctrl, pt);
            }
        } else if (m_view == View::Sftp && !HitRect(m_lay.side, pt.x, pt.y)) {
            const float mid = std::floor((m_lay.main.left + m_lay.main.right) * 0.5f);
            const float step = (float)delta * 50.0f / (float)WHEEL_DELTA * m_lay.scale;
            if (pt.x < mid) {
                m_sftpLocalScroll = std::max(0.0f, m_sftpLocalScroll - step);
            } else {
                m_sftpRemoteScroll = std::max(0.0f, m_sftpRemoteScroll - step);
            }
        } else if (m_view == View::Snippets && !HitRect(m_lay.side, pt.x, pt.y)) {
            const float step = (float)delta * 50.0f / (float)WHEEL_DELTA * m_lay.scale;
            m_snippetScroll = std::max(0.0f, m_snippetScroll - step);
        } else if (m_view == View::PortForward && !HitRect(m_lay.side, pt.x, pt.y)) {
            const float step = (float)delta * 50.0f / (float)WHEEL_DELTA * m_lay.scale;
            m_tunnelScroll = std::max(0.0f, m_tunnelScroll - step);
        } else if (m_view == View::Logs && !HitRect(m_lay.side, pt.x, pt.y)) {
            const float step = (float)delta * 50.0f / (float)WHEEL_DELTA * m_lay.scale;
            m_logsScroll = std::max(0.0f, m_logsScroll - step);
        } else {
            m_wheelTarget = 0;
            m_wheelAccum = 0;
            const float step = (float)delta * 60.0f / (float)WHEEL_DELTA * m_lay.scale;
            if (HitRect(m_lay.side, pt.x, pt.y)) m_sideScroll = std::max(0.0f, m_sideScroll - step);
            else                                 m_mainScroll = std::max(0.0f, m_mainScroll - step);
        }
        m_dirty = true;
        return 0;
    }

    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        m_dirty = true;
        Render();
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case TRAY_SHOW:
            if (InQuake()) { if (m_quakeState == QuakeState::Visible) ForceForeground(); else QuakeShow(); }
            else RestoreFromTray();
            return 0;
        case TRAY_HIDE:  if (InQuake()) QuakeHide(); return 0;
        case TRAY_NEW:
            if (InQuake()) { if (m_quakeState != QuakeState::Visible) QuakeShow(); }
            else RestoreFromTray();
            NewTab(DefaultProfileIndex());
            return 0;
        case TRAY_QUAKE: PostQuake(InQuake() ? QC_EXIT : QC_ENTER); return 0;
        case TRAY_QUIT:  m_reallyQuit = true; PostMessageW(m_hwnd, WM_CLOSE, 0, 0); return 0;
        default: break;
        }
        break;

    case WM_CLOSE:
        m_cfg.Save(m_dataDir);
        m_inv.Save(m_dataDir);
        // Alt+F4 bir acilir konsolu kapatmaz, gizler (Guake davranisi).
        if (InQuake() && !m_reallyQuit) {
            QuakeHide();
            return 0;
        }
        if (m_cfg.runInBackground && !m_reallyQuit) {
            HideToTray();
            return 0;
        }
        // Buradan sonrasi gercek cikis: tum oturumlar olur. Gizleme yollari
        // (Guake, tepsi) oturumlari yasattigi icin yukarida soru sormadan doner.
        if (m_cfg.confirmClose) {
            if (m_closePrompting) return 0;   // soru zaten acik (tepsi Cikis ikinci kez)
            size_t live = 0;
            for (const auto& t : m_tabs) if (t && t->Alive()) ++live;
            if (live > 0) {
                if (InQuake()) {
                    if (m_quakeState != QuakeState::Visible) {
                        QuakeShow();
                        // Modal kutu acikken Tick donmez; inisi hemen bitir.
                        m_quakeAnimStart = NowTicks() - TickFreq() * 10;
                        UpdateQuakeAnimation();
                    }
                } else if (!IsWindowVisible(m_hwnd) || IsIconic(m_hwnd)) {
                    RestoreFromTray();
                }
                m_closePrompting = true;
                wchar_t msgText[160];
                swprintf_s(msgText, L"%zu acik oturum var. Hepsi kapatilacak. Cikilsin mi?", live);
                const int r = MessageBoxW(m_hwnd, msgText, L"FullTerminal",
                                          MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND);
                m_closePrompting = false;
                if (r != IDYES) {
                    m_reallyQuit = false;   // yoksa sonraki Alt+F4 gizlemek yerine cikardi
                    return 0;
                }
            }
        }
        DestroyWindow(m_hwnd);
        return 0;

    case WM_DESTROY:
        SaveSession();
        UnregisterQuakeHotkey();
        KillTimer(m_hwnd, kBlinkTimer);
        RemoveTrayIcon();
        m_tabLayouts.clear();
        m_tabs.clear();
        m_r.Shutdown();
        if (m_icon) { DestroyIcon(m_icon); m_icon = nullptr; }
        m_hwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ akis ---

bool MainWindow::Tick() {
    if (!m_hwnd) return false;

    UpdateQuakeAnimation();
    UpdateDrawerAnimation();

    bool changed = false;
    if (m_quakeState == QuakeState::DroppingDown || m_quakeState == QuakeState::SlidingUp ||
        (m_drawerProgress > 0.0f && m_drawerProgress < 1.0f)) {
        changed = true;
    }

    for (auto& layout : m_tabLayouts) {
        if (!layout) continue;
        for (auto& t : layout->GetAllTabs()) {
            if (t && t->Pump()) changed = true;
        }
    }

    for (size_t i = m_tabLayouts.size(); i-- > 0; ) {
        auto& layout = m_tabLayouts[i];
        if (!layout) continue;
        auto allTabs = layout->GetAllTabs();
        for (auto& t : allTabs) {
            if (t && t->Exited()) {
                if (layout->PaneCount() > 1) {
                    uint32_t pid = layout->FindPaneIdByTab(t.get());
                    if (pid != 0) {
                        t->Close();
                        layout->ClosePane(pid);
                        if (i < m_tabs.size()) m_tabs[i] = layout->GetFocusedTab();
                        SyncGridToArea();
                        changed = true;
                    }
                } else {
                    CloseTab(i);
                    changed = true;
                    break;
                }
            }
        }
    }
    if (m_tabs.empty() && m_hwnd) {
        if (InQuake()) {
            // Guake: son sekmede "exit" konsolu kapatmaz; yeni bir sekme
            // hazirlanir ve konsol gizlenir. Profil hic baslamiyorsa dongude
            // denemeyi surdurmeyelim, hata kaplamasi gorunsun.
            if (!m_respawnTried) {
                m_respawnTried = true;
                if (NewTab(DefaultProfileIndex()) && m_quakeState != QuakeState::Hidden) QuakeHide();
                changed = true;
            }
        } else if (!m_cfg.runInBackground) {
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            return false;
        }
    } else if (!m_tabs.empty()) {
        m_respawnTried = false;
    }

    if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
        for (const auto& tab : m_tabLayouts[m_active]->GetAllTabs()) {
            if (tab && tab->GetSshStage() == SshStage::Connecting) {
                changed = true;
                break;
            }
        }
    } else if (auto* t = Active()) {
        if (t->GetSshStage() == SshStage::Connecting) changed = true;
    }
    if (auto* t = Active()) {
        const uint64_t rev = t->screen().Revision();
        if (rev != m_lastRevision) { m_lastRevision = rev; changed = true; }
        if (t->BellPending() && m_cfg.bellSound) MessageBeep(MB_OK);
    }
    if (m_toastUntil && NowTicks() > m_toastUntil) { m_toast.clear(); m_toastUntil = 0; changed = true; }

    SftpController* sftpLive = nullptr;
    if (m_view == View::Sftp) {
        sftpLive = m_sftp.get();
    } else if (m_view == View::Terminal && Active() && Active()->IsSftp()) {
        sftpLive = Active()->Sftp();
    }
    if (sftpLive && (sftpLive->Busy() || sftpLive->HasTransferBanner())) {
        changed = true;
    }

    if (changed) m_dirty = true;
    if (!m_dirty) return false;
    if (!IsWindowVisible(m_hwnd)) { m_dirty = false; return false; } // tepside, cizmeye gerek yok

    Render();
    return true;
}

void MainWindow::OnResize() {
    if (!m_hwnd || IsIconic(m_hwnd)) return;   // simge boyutu yerlesimi bozmasin
    RECT rc{}; GetClientRect(m_hwnd, &rc);
    m_r.Resize(rc.right - rc.left, rc.bottom - rc.top);
    ComputeLayout();
    SyncGridToArea();
    m_dirty = true;
}

void MainWindow::SetView(View v) {
    if (m_view == v) return;
    m_view = v;
    m_ui.SetFocus(ID_NONE);
    m_mainScroll = 0.0f;
    m_snippetScroll = 0.0f;
    m_tunnelScroll = 0.0f;
    m_logsScroll = 0.0f;
    if (v == View::Hosts && m_hubNodes.empty() && m_lastHubScan == 0) {
        RefreshHubNodes();
    }
    m_dirty = true;
}

void MainWindow::ComputeLayout() {
    RECT rc{};
    if (m_hwnd) GetClientRect(m_hwnd, &rc);
    const float W = (float)(rc.right - rc.left);
    const float H = (float)(rc.bottom - rc.top);

    Layout& L = m_lay;
    L.scale = m_dpi / 96.0f;
    L.titleH = std::floor(theme::TitleBarH * L.scale);
    L.statusH = std::floor(26.0f * L.scale);
    L.railW = std::floor(theme::AccentRailW * L.scale);
    L.sideW = m_sidebarOpen ? std::floor(212 * L.scale) : 0.0f;

    L.rail   = D2D1::RectF(0, 0, L.railW, H);
    L.title  = D2D1::RectF(L.railW, 0, W, L.titleH);
    L.status = D2D1::RectF(L.railW, H - L.statusH, W, H);
    L.side   = D2D1::RectF(L.railW, L.titleH, L.railW + L.sideW, H - L.statusH);
    L.main   = D2D1::RectF(L.side.right, L.titleH, W, H - L.statusH);

    const float padX = std::floor(theme::TermPadX * L.scale);
    const float padY = std::floor(theme::TermPadY * L.scale);
    L.term = D2D1::RectF(L.main.left + padX, L.main.top + padY,
                         L.main.right - padX, L.main.bottom - padY);
    if (L.term.right < L.term.left) L.term.right = L.term.left;
    if (L.term.bottom < L.term.top) L.term.bottom = L.term.top;

    const float bw = std::floor(46 * L.scale);
    L.btnClose = D2D1::RectF(W - bw, 0, W, L.titleH);
    L.btnMax   = D2D1::RectF(W - bw * 2, 0, W - bw, L.titleH);
    L.btnMin   = D2D1::RectF(W - bw * 3, 0, W - bw * 2, L.titleH);
    // Guake dugmesi pencere dugmelerinin solunda; sekme seridi bundan once biter.
    L.btnQuake = D2D1::RectF(W - bw * 4, 0, W - bw * 3, L.titleH);

    const float mb = std::floor(36 * L.scale);
    L.menuBtn = D2D1::RectF(L.railW, 0, L.railW + mb, L.titleH);

    // ------------------------------------------------------- sekme seridi --
    // Pill genislikleri TEKDUZE: konum yalnizca (i, n, W) fonksiyonu. Aktife
    // ayricalikli genislik verilseydi tiklanan sekme imlecin altindan kacar,
    // kapatma dugmesiyle birlesince yanlis oturumu oldururdu.
    const float tabH  = std::floor(26.0f * L.scale);
    const float tTop  = std::floor((L.titleH - tabH) * 0.5f);
    const float tBot  = tTop + tabH;
    const float gap   = std::floor(6.0f  * L.scale);
    const float edge  = std::floor(6.0f  * L.scale);
    const float newW  = std::floor(30.0f * L.scale);
    const float chipW = std::floor(34.0f * L.scale);

    const float wIdeal    = std::floor(192.0f * L.scale);
    const float wFull     = std::floor(120.0f * L.scale);
    const float wCompact  = std::floor( 98.0f * L.scale);
    const float wMicro    = std::floor( 48.0f * L.scale);
    const float wMicroMax = std::floor( 56.0f * L.scale);

    const float stripL = L.menuBtn.right + edge;
    const float toolsW = (W >= std::floor(700.0f * L.scale)) ? std::floor(280.0f * L.scale) : 0.0f;
    const float stripR = std::max(stripL, L.btnQuake.left - edge - newW - gap - toolsW);

    L.tabs.clear();
    L.tabClose.clear();
    L.tabFirst = 0;
    L.hidden = 0;
    L.tabTier = 2;
    L.overflow = D2D1::RectF(0, 0, 0, 0);

    const size_t n = m_tabs.size();
    if (m_labelCache.size() != n) m_labelCache.resize(n);

    float packRight = stripL - gap;   // n == 0 ise "+" tam stripL'de baslar

    if (n > 0 && stripR > stripL) {
        float avail = stripR - stripL;
        size_t vis = n;

        float w = std::floor((avail - gap * (float)(n - 1)) / (float)n);
        if (w > wIdeal) w = wIdeal;

        if (w < wMicro) {              // taban asildi: "+N" cipi ve pencere
            avail = std::max(0.0f, avail - chipW - gap);
            const float slot = wMicro + gap;
            vis = (size_t)std::max(0.0f, std::floor((avail + gap) / slot));
            if (vis > n) vis = n;
            w = wMicro;
            L.hidden = (int)(n - vis);
        }

        L.tabTier = (w >= wFull) ? 2 : (w >= wCompact) ? 1 : 0;
        if (L.tabTier == 0) w = std::min(w, wMicroMax);

        // Kaydirma penceresi kalici durumdan turer, m_active'den degil:
        // gorunur bir sekmeye tiklamak seridi hic oynatmaz.
        if (vis >= n) {
            m_tabScroll = 0;
        } else {
            if (m_tabScroll + vis > n)              m_tabScroll = n - vis;
            if (m_active < m_tabScroll)             m_tabScroll = m_active;
            else if (m_active >= m_tabScroll + vis) m_tabScroll = m_active - vis + 1;
        }
        L.tabFirst = m_tabScroll;

        const float closeW = std::floor(16.0f * L.scale);
        const float cPadR  = std::floor(10.0f * L.scale);
        float x = stripL;
        for (size_t k = 0; k < vis; ++k) {
            const float right = std::min(x + w, stripR);
            if (right - x < wMicro * 0.5f) { L.hidden = (int)(n - (L.tabFirst + k)); break; }

            L.tabs.push_back(D2D1::RectF(x, tTop, right, tBot));

            D2D1_RECT_F cb = D2D1::RectF(0, 0, 0, 0);
            if (L.tabTier == 2) {
                const float cy = (tTop + tBot) * 0.5f;
                const float cR = right - cPadR;
                cb = D2D1::RectF(cR - closeW, cy - closeW * 0.5f, cR, cy + closeW * 0.5f);
            }
            L.tabClose.push_back(cb);
            x = right + gap;
        }
        if (!L.tabs.empty()) packRight = L.tabs.back().right;

        if (L.hidden > 0) {   // hicbir pill sigmasa bile cip cizilir
            const float cx0 = std::min(packRight + gap, std::max(stripL, stripR - chipW));
            L.overflow = D2D1::RectF(cx0, tTop, std::min(cx0 + chipW, stripR), tBot);
            packRight = L.overflow.right;
        }
    }

    const float maxNewX = L.btnQuake.left - edge - newW - toolsW;
    const float nx = std::min(packRight + gap, std::max(stripL, maxNewX));
    L.newTab = D2D1::RectF(nx, tTop, std::min(nx + newW, maxNewX + newW), tBot);
    if (L.newTab.right < L.newTab.left) L.newTab.right = L.newTab.left;

    // Split Panes & Broadcast / Sync Toolbar dugmeleri (btnQuake solunda)
    const float limitR = L.btnQuake.left - std::floor(10.0f * L.scale);
    const float btnH = std::floor(26.0f * L.scale);
    const float btnTop = std::floor((L.titleH - btnH) * 0.5f);
    const float btnBot = btnTop + btnH;
    const float btnGap = std::floor(6.0f * L.scale);
    const float minLeft = (L.newTab.right > L.newTab.left) ? (L.newTab.right + btnGap) : (L.menuBtn.right + std::floor(10.0f * L.scale));

    float curR = limitR;

    // 1. Coklu / Sync
    const std::wstring syncBtnLabel = m_broadcastMode ? L"📡 SYNC" : (L"📡 " + std::wstring(Tr(Msg::ToolbarBroadcast)));
    const float wSync = std::max(std::floor(68.0f * L.scale),
        (m_r.Ready() ? m_r.MeasureText(syncBtnLabel, 11.5f * L.scale, true) : 50.0f) + std::floor(18.0f * L.scale));
    if (curR - wSync >= minLeft) {
        L.btnSync = D2D1::RectF(curR - wSync, btnTop, curR, btnBot);
        curR -= wSync + btnGap;
    } else {
        L.btnSync = D2D1::RectF(0, 0, 0, 0);
    }

    // 2. Yatay Bol (Split Horizontal)
    const std::wstring splitHText = L"⬒ " + std::wstring(Tr(Msg::ActionSplitH));
    const float wSplitH = std::max(std::floor(66.0f * L.scale),
        (m_r.Ready() ? m_r.MeasureText(splitHText, 11.5f * L.scale, true) : 50.0f) + std::floor(16.0f * L.scale));
    if (curR - wSplitH >= minLeft) {
        L.btnSplitH = D2D1::RectF(curR - wSplitH, btnTop, curR, btnBot);
        curR -= wSplitH + btnGap;
    } else {
        L.btnSplitH = D2D1::RectF(0, 0, 0, 0);
    }

    // 3. Dikey Bol (Split Vertical)
    const std::wstring splitVText = L"◫ " + std::wstring(Tr(Msg::ActionSplitV));
    const float wSplitV = std::max(std::floor(66.0f * L.scale),
        (m_r.Ready() ? m_r.MeasureText(splitVText, 11.5f * L.scale, true) : 50.0f) + std::floor(16.0f * L.scale));
    if (curR - wSplitV >= minLeft) {
        L.btnSplitV = D2D1::RectF(curR - wSplitV, btnTop, curR, btnBot);
        curR -= wSplitV + btnGap;
    } else {
        L.btnSplitV = D2D1::RectF(0, 0, 0, 0);
    }
}

void MainWindow::SyncGridToArea() {
    if (!m_r.Ready() || !m_hwnd || IsIconic(m_hwnd)) return;
    const FontMetrics& fm = m_r.Metrics();
    // Tek hucreden kucuk alan (simge durumu, gecici 0x0) PTY'yi 8x2'ye
    // indirir; ekran icerigi kirpilir ve kabuklar yeniden cizer.
    if (fm.cellW <= 0.0f || fm.cellH <= 0.0f ||
        m_lay.term.right - m_lay.term.left < fm.cellW ||
        m_lay.term.bottom - m_lay.term.top < fm.cellH) return;

    for (size_t ti = 0; ti < m_tabLayouts.size(); ++ti) {
        auto& layout = m_tabLayouts[ti];
        if (!layout) continue;
        auto panes = layout->ComputeLayout(m_lay.term);
        for (auto& pane : panes) {
            if (!pane.tab) continue;
            const float pw = pane.area.right - pane.area.left;
            const float ph = pane.area.bottom - pane.area.top;
            const int cols = std::max(8, (int)(pw / fm.cellW));
            const int rows = std::max(2, (int)(ph / fm.cellH));
            pane.tab->Resize(cols, rows);
        }
    }
    ClearSelection();
}

// ----------------------------------------------------------------- ikonlar --

void MainWindow::DrawIcon(Icon ic, const D2D1_RECT_F& r, uint32_t color) {
    const float cx = (r.left + r.right) * 0.5f;
    const float cy = (r.top + r.bottom) * 0.5f;
    const float w = r.right - r.left;
    const float u = w / 18.0f;                       // birim
    const float th = std::max(1.0f, std::floor(1.5f * m_lay.scale));

    switch (ic) {
    case Icon::Terminal:
        m_r.Line(cx - 6 * u, cy - 4 * u, cx - 1 * u, cy, color, th);
        m_r.Line(cx - 1 * u, cy, cx - 6 * u, cy + 4 * u, color, th);
        m_r.Line(cx + 1 * u, cy + 4 * u, cx + 6 * u, cy + 4 * u, color, th);
        break;
    case Icon::Hosts:
        m_r.Stroke(D2D1::RectF(cx - 7 * u, cy - 7 * u, cx + 7 * u, cy - 1 * u), color, th);
        m_r.Stroke(D2D1::RectF(cx - 7 * u, cy + 1 * u, cx + 7 * u, cy + 7 * u), color, th);
        m_r.Disc(cx - 4 * u, cy - 4 * u, 1.2f * u, color);
        m_r.Disc(cx - 4 * u, cy + 4 * u, 1.2f * u, color);
        break;
    case Icon::Key:
        m_r.Ring(cx - 4.2f * u, cy - 3.4f * u, 2.7f * u, color, th);
        m_r.Line(cx - 2.4f * u, cy - 1.6f * u, cx + 6.5f * u, cy + 7.2f * u, color, th);
        m_r.Line(cx + 2.4f * u, cy + 3.1f * u, cx + 4.6f * u, cy + 0.9f * u, color, th);
        m_r.Line(cx + 4.2f * u, cy + 4.9f * u, cx + 6.0f * u, cy + 3.1f * u, color, th);
        break;
    case Icon::Forward:
        m_r.Line(cx - 7 * u, cy + 4 * u, cx + 1 * u, cy + 4 * u, color, th);
        m_r.Line(cx + 1 * u, cy + 4 * u, cx + 1 * u, cy - 4 * u, color, th);
        m_r.Line(cx + 1 * u, cy - 4 * u, cx + 6 * u, cy - 4 * u, color, th);
        m_r.Line(cx + 3 * u, cy - 6.5f * u, cx + 6.5f * u, cy - 4 * u, color, th);
        m_r.Line(cx + 3 * u, cy - 1.5f * u, cx + 6.5f * u, cy - 4 * u, color, th);
        break;
    case Icon::Snippet:
        m_r.Text(L"{ }", r, color, 13.0f * m_lay.scale, Renderer::Align::Center, true, true);
        break;
    case Icon::Shield:
        m_r.Ring(cx, cy, 6.5f * u, color, th);
        m_r.Line(cx - 3 * u, cy, cx - 0.8f * u, cy + 2.6f * u, color, th);
        m_r.Line(cx - 0.8f * u, cy + 2.6f * u, cx + 3.4f * u, cy - 2.4f * u, color, th);
        break;
    case Icon::Clock:
        m_r.Ring(cx, cy, 6.5f * u, color, th);
        m_r.Line(cx, cy, cx, cy - 3.6f * u, color, th);
        m_r.Line(cx, cy, cx + 3 * u, cy + 1 * u, color, th);
        break;
    case Icon::Gear:
        m_r.Ring(cx, cy, 5.4f * u, color, th);
        m_r.Ring(cx, cy, 2.2f * u, color, th);
        for (int i = 0; i < 4; ++i) {
            const float a = (float)i * 3.14159265f / 2.0f + 0.39f;
            m_r.Line(cx + std::cos(a) * 5.4f * u, cy + std::sin(a) * 5.4f * u,
                     cx + std::cos(a) * 7.6f * u, cy + std::sin(a) * 7.6f * u, color, th);
        }
        break;
    case Icon::Plus:
        m_r.Line(cx - 5 * u, cy, cx + 5 * u, cy, color, th);
        m_r.Line(cx, cy - 5 * u, cx, cy + 5 * u, color, th);
        break;
    case Icon::Search:
        m_r.Ring(cx - 1 * u, cy - 1 * u, 4.8f * u, color, th);
        m_r.Line(cx + 2.4f * u, cy + 2.4f * u, cx + 6.4f * u, cy + 6.4f * u, color, th);
        break;
    case Icon::Sftp:
    case Icon::Folder:
        m_r.Stroke(D2D1::RectF(cx - 6 * u, cy - 3 * u, cx + 6 * u, cy + 5 * u), color, th);
        m_r.Line(cx - 6 * u, cy - 3 * u, cx - 2 * u, cy - 3 * u, color, th);
        m_r.Line(cx - 2 * u, cy - 5 * u, cx + 2 * u, cy - 5 * u, color, th);
        m_r.Line(cx + 2 * u, cy - 5 * u, cx + 3 * u, cy - 3 * u, color, th);
        break;
    case Icon::Refresh:
        m_r.Ring(cx, cy, 4.8f * u, color, th);
        m_r.Line(cx + 3.0f * u, cy - 3.5f * u, cx + 5.5f * u, cy - 1.0f * u, color, th);
        break;
    case Icon::Download:
        m_r.Line(cx, cy - 5 * u, cx, cy + 3 * u, color, th);
        m_r.Line(cx - 3 * u, cy, cx, cy + 3 * u, color, th);
        m_r.Line(cx + 3 * u, cy, cx, cy + 3 * u, color, th);
        m_r.Line(cx - 5 * u, cy + 5 * u, cx + 5 * u, cy + 5 * u, color, th);
        break;
    case Icon::Upload:
        m_r.Line(cx, cy + 3 * u, cx, cy - 5 * u, color, th);
        m_r.Line(cx - 3 * u, cy - 2 * u, cx, cy - 5 * u, color, th);
        m_r.Line(cx + 3 * u, cy - 2 * u, cx, cy - 5 * u, color, th);
        m_r.Line(cx - 5 * u, cy + 5 * u, cx + 5 * u, cy + 5 * u, color, th);
        break;
    }
}

// ----------------------------------------------------------------- cizim ---

void MainWindow::Render() {
    if (!m_r.Begin()) return;
    m_dirty = false;
    m_ui.Begin(m_in, m_lay.scale);

    // Cekmece aciksa alttaki her sey fare girdisini "disarida" gorur; ayni
    // tik once alttaki widget'i tetikleyip sonra cekmeceye ulasmasin.
    std::unique_ptr<PointerMask> underDrawer;
    if (DrawerUp()) underDrawer = std::make_unique<PointerMask>(m_in, true);

    DrawChrome();
    if (m_sidebarOpen) DrawSidebar();

    bool isSftpActive = (m_view == View::Sftp);
    if (m_view == View::Terminal) {
        if (auto* t = Active(); t && t->IsSftp()) isSftpActive = true;
    }

    const bool transparent = (m_view == View::Terminal) && (!isSftpActive) && (m_cfg.opacity < 0.999f);
    if (!transparent) {
        m_r.Fill(m_lay.main, theme::Base);
    } else {
        // Dolgu halkasi terminalle ayni alfayla: yoksa terminalin etrafinda
        // tamamen saydam bir cerceve kalir. Her piksel tek kez doldurulur.
        const D2D1_RECT_F& M = m_lay.main;
        const D2D1_RECT_F& T = m_lay.term;
        const float a = m_cfg.opacity;
        m_r.Fill(D2D1::RectF(M.left, M.top, M.right, T.top), theme::TermBg, a);
        m_r.Fill(D2D1::RectF(M.left, T.bottom, M.right, M.bottom), theme::TermBg, a);
        m_r.Fill(D2D1::RectF(M.left, T.top, T.left, T.bottom), theme::TermBg, a);
        m_r.Fill(D2D1::RectF(T.right, T.top, M.right, T.bottom), theme::TermBg, a);
    }

    switch (m_view) {
    case View::Terminal:
        if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
            auto panes = m_tabLayouts[m_active]->ComputeLayout(m_lay.term);
            const bool multiPane = panes.size() > 1;
            for (const auto& pane : panes) {
                if (!pane.tab) continue;
                if (pane.tab->IsSftp()) {
                    DrawSftpScreen(pane.area, pane.tab->Sftp());
                } else if (pane.tab->GetSshStage() == SshStage::Connecting || pane.tab->GetSshStage() == SshStage::Failed) {
                    DrawSshStageView(pane.tab.get(), pane.area);
                } else {
                    const bool paneFocused = pane.isFocused && m_focused;
                    m_r.DrawTerminal(pane.tab->screen(), pane.area, paneFocused,
                                     paneFocused && m_cursorOn,
                                     m_cfg.cursorStyle ? m_cfg.cursorStyle : pane.tab->CursorStyle(),
                                     pane.isFocused ? ViewSelection() : Selection{},
                                     m_cfg.opacity);
                }
                if (multiPane) {
                    const uint32_t bCol = m_broadcastMode ? 0x00E5FF : (pane.isFocused ? Accent() : theme::Border);
                    const float bTh = m_broadcastMode ? 2.0f : (pane.isFocused ? 2.0f : 1.0f);
                    m_r.Stroke(pane.area, bCol, bTh);

                    if (m_broadcastMode) {
                        const float s = m_lay.scale;
                        D2D1_RECT_F syncBadge = D2D1::RectF(pane.area.right - 86.0f * s, pane.area.top + 4.0f * s,
                                                           pane.area.right - 6.0f * s, pane.area.top + 22.0f * s);
                        m_r.FillRound(syncBadge, std::floor(4.0f * s), 0x001B2E, 0.92f);
                        m_r.Stroke(syncBadge, 0x00E5FF, 1.0f);
                        m_r.Text(L"📡 SYNC", syncBadge, 0x00E5FF, 9.5f * s, Renderer::Align::Center, true);
                    }
                }
                if (!pane.tab->IsSftp()) {
                    DrawAgentApprovalRibbon(pane.tab.get(), pane.area, pane.isFocused, pane.id);
                }
            }
            if (m_tabLayouts[m_active]->IsZoomed()) {
                const float s = m_lay.scale;
                D2D1_RECT_F zoomBadge = D2D1::RectF(m_lay.term.right - 92.0f * s, m_lay.term.top + 6.0f * s,
                                                   m_lay.term.right - 6.0f * s, m_lay.term.top + 26.0f * s);
                m_r.Fill(zoomBadge, theme::Surface, 0.9f);
                m_r.Stroke(zoomBadge, Accent(), 1.0f);
                m_r.Text(L"[ ZOOMED ]", zoomBadge, Accent(), 11.0f * s, Renderer::Align::Center);
            }
        } else if (auto* t = Active()) {
            if (t->IsSftp()) {
                DrawSftpScreen(m_lay.term, t->Sftp());
            } else if (t->GetSshStage() == SshStage::Connecting || t->GetSshStage() == SshStage::Failed) {
                DrawSshStageView(t, m_lay.term);
            } else {
                m_r.DrawTerminal(t->screen(), m_lay.term, m_focused, m_cursorOn,
                                 m_cfg.cursorStyle ? m_cfg.cursorStyle : t->CursorStyle(),
                                 ViewSelection(), m_cfg.opacity);
            }
            if (!t->IsSftp()) {
                DrawAgentApprovalRibbon(t, m_lay.term, true, 1);
            }
        } else {
            m_r.Fill(m_lay.term, theme::TermBg, m_cfg.opacity);
        }
        DrawOverlay();
        break;
    case View::Hosts:       DrawHostsScreen(m_lay.main); break;
    case View::Sftp:        DrawSftpScreen(m_lay.main); break;
    case View::Keychain:    DrawIdentitiesScreen(m_lay.main); break;
    case View::KnownHosts:  DrawKnownHostsScreen(m_lay.main); break;
    case View::Settings:    DrawSettingsScreen(m_lay.main); break;
    case View::PortForward: DrawPortForwardScreen(m_lay.main); break;
    case View::Snippets:    DrawSnippetsScreen(m_lay.main); break;
    case View::Logs:        DrawLogsScreen(m_lay.main); break;
    }

    DrawStatusBar();
    if (InQuake()) {
        // Alt kenar: masaustunden ayiran cizgi + yukseklik tutamagi
        const float s = m_lay.scale;
        const float W = m_lay.status.right, H = m_lay.status.bottom;
        m_r.Fill(D2D1::RectF(0, H - 1, W, H), theme::BorderHi);
        const bool hot = m_quakeResizing ||
            (m_in.my >= H - (float)QuakeGrip() && m_in.my < H && m_quakeState == QuakeState::Visible);
        const float gw = std::floor(44 * s), gh = std::max(2.0f, std::floor(3 * s));
        const float cx = std::floor(W * 0.5f);
        const D2D1_RECT_F pill = D2D1::RectF(cx - gw * 0.5f, H - gh - std::floor(2 * s),
                                             cx + gw * 0.5f, H - std::floor(2 * s));
        m_r.FillRound(pill, gh * 0.5f, hot ? theme::Ac() : theme::BorderHi, hot ? 1.0f : 0.8f);
    }
    underDrawer.reset();   // gercek fare durumu yalnizca cekmeceye
    if (m_drawerProgress > 0.001f) {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        D2D1_RECT_F fullClient{ 0.0f, 0.0f, (float)(rc.right - rc.left), (float)(rc.bottom - rc.top) };
        DrawSlideDrawer(fullClient);
    }
    if (m_renamingTab) {
        DrawRenameTabModal();
    }
    m_ui.End();
    m_r.End();
    if (m_leavePending) {           // tik tuketildi; ertelenen WM_MOUSELEAVE simdi uygulanir
        m_leavePending = false;
        m_in.mx = m_in.my = -1.0f;
        m_dirty = true;
    }

    ++m_frameCount;
    const int64_t now = NowTicks();
    if (now - m_fpsStart >= TickFreq()) {
        m_fps = (double)m_frameCount * (double)TickFreq() / (double)(now - m_fpsStart);
        m_frameCount = 0;
        m_fpsStart = now;
    }
}


// ------------------------------------------------------------ durum/uyari --

void MainWindow::DrawStatusBar() {
    const Layout& L = m_lay;
    const float s = L.scale;
    m_r.Fill(L.status, theme::Surface);
    m_r.Fill(D2D1::RectF(L.status.left, L.status.top, L.status.right, L.status.top + 1), theme::Border);

    const float fs = 11.0f * s;
    const float padd = std::floor(12 * s);
    auto* t = Active();

    std::wstring left;
    if (!m_toast.empty()) {
        left = m_toast;
    } else if (t) {
        const ShellProfile& p = t->profile();
        left = p.name;
        if (p.kind == ProfileKind::Wsl && p.wslVersion) left += L"  WSL" + std::to_wstring(p.wslVersion);
        left += L"   " + std::to_wstring(t->screen().Cols()) + L"x" + std::to_wstring(t->screen().Rows());
        if (t->screen().IsAltBuffer()) left += L"   alt";
        if (t->screen().ViewOffset() > 0) left += L"   yukari " + std::to_wstring(t->screen().ViewOffset());

        const auto& agent = t->GetAgentStatus();
        if (agent.isBlocked()) {
            left += L"   | 🔴 ONAY BEKLIYOR";
            if (!agent.matchedPattern.empty()) {
                left += L" (" + Utf8ToWide(agent.matchedPattern) + L")";
            }
        } else if (agent.isWorking()) {
            left += L"   | ⚡ " + Utf8ToWide(AgentDetector::KindToString(agent.kind)) + L" Calisiyor...";
        } else if (agent.isIdle()) {
            left += L"   | ⚪ " + Utf8ToWide(AgentDetector::KindToString(agent.kind)) + L" Bosta";
        }

        if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active] && m_tabLayouts[m_active]->PaneCount() > 1) {
            left += L"   | [Panel: " + std::to_wstring(m_tabLayouts[m_active]->PaneCount()) + L"]";
            if (m_tabLayouts[m_active]->IsZoomed()) left += L" (Zoom)";
        }
    }
    m_r.Text(left, D2D1::RectF(L.status.left + padd, L.status.top, L.status.right, L.status.bottom),
             m_toast.empty() ? theme::TextMuted : theme::AcHi(), fs, Renderer::Align::Left, false, true);

    const float btnW = std::floor(154 * s);
    const float btnH = std::floor(20 * s);
    const float btnTop = (L.status.top + L.status.bottom) * 0.5f - btnH * 0.5f;
    m_drawerBtn = D2D1::RectF(L.status.right - btnW - padd, btnTop, L.status.right - padd, btnTop + btnH);
    if (m_ui.Button(9199, m_drawerBtn, m_drawerOpen ? Tr(Msg::StatusCloseDrawer) : Tr(Msg::StatusMenuDrawer), m_drawerOpen)) {
        ToggleSlideDrawer();
    }

    // Hizli Dil Degistirme Butonu [🌐 EN / TR / RU / UK ...]
    const float langBtnW = std::floor(74 * s);
    const D2D1_RECT_F langBtn = D2D1::RectF(m_drawerBtn.left - langBtnW - std::floor(8 * s), btnTop,
                                           m_drawerBtn.left - std::floor(8 * s), btnTop + btnH);
    std::wstring langLabel = std::wstring(L"🌐 ") + I18n::CurrentBadge();
    if (m_ui.Button(9198, langBtn, langLabel.c_str())) {
        I18n::CycleLanguage();
        m_cfg.language = I18n::CurrentCode();
        m_cfg.Save(m_dataDir);
        Toast(Tr(Msg::ToastLangChanged));
        m_dirty = true;
    }

    // Hizli Saydamlik Butonu [💧 %85 / %100 ...]
    const float opBtnW = std::floor(76 * s);
    const D2D1_RECT_F opBtn = D2D1::RectF(langBtn.left - opBtnW - std::floor(8 * s), btnTop,
                                          langBtn.left - std::floor(8 * s), btnTop + btnH);
    const int curPct = (int)std::round(m_cfg.opacity * 100.0f);
    wchar_t opLabel[32];
    swprintf_s(opLabel, L"💧 %%%d", curPct);
    if (m_ui.Button(9197, opBtn, opLabel)) {
        // Hizli gecis: 100% -> 85% -> 70% -> 50% -> 100%
        if (curPct >= 95)       m_cfg.opacity = 0.85f;
        else if (curPct >= 80)  m_cfg.opacity = 0.70f;
        else if (curPct >= 65)  m_cfg.opacity = 0.50f;
        else if (curPct >= 45)  m_cfg.opacity = 0.30f;
        else                    m_cfg.opacity = 1.0f;
        m_cfg.Save(m_dataDir);
        wchar_t tbuf[64];
        swprintf_s(tbuf, L"%s: %%%d", Tr(Msg::Opacity), (int)std::round(m_cfg.opacity * 100.0f));
        Toast(tbuf);
        m_dirty = true;
    }

    wchar_t buf[200];
    swprintf_s(buf, L"%s: %zu  |  %zu %s   %.0f FPS   %s %.0f",
               Tr(Msg::StatusInfrastructure),
               m_hubNodes.size(),
               m_tabs.size(),
               Tr(Msg::StatusSessions),
               m_fps, m_r.Metrics().family.c_str(), m_cfg.fontPt);
    m_r.Text(buf, D2D1::RectF(L.status.left, L.status.top, opBtn.left - padd, L.status.bottom),
             theme::TextDim, fs, Renderer::Align::Right, false, true);
}

void MainWindow::DrawOverlay() {
    if (m_agentFleetOpen) {
        DrawAgentFleetDrawer(m_lay.term);
    }
    if (m_error.empty()) return;
    const float s = m_lay.scale;
    const D2D1_RECT_F& a = m_lay.term;
    const D2D1_RECT_F box = D2D1::RectF(a.left + 20 * s, a.top + 20 * s,
                                        std::min(a.right - 20 * s, a.left + 660 * s),
                                        a.top + 112 * s);
    m_r.FillRound(box, 6 * s, theme::Surface);
    m_r.Fill(D2D1::RectF(box.left, box.top, box.left + 3 * s, box.bottom), theme::Red);
    m_r.Text(L"Oturum baslatilamadi",
             D2D1::RectF(box.left + 18 * s, box.top + 8 * s, box.right - 14 * s, box.top + 38 * s),
             theme::TextHi, 14.0f * s, Renderer::Align::Left, true);
    m_r.Text(m_error,
             D2D1::RectF(box.left + 18 * s, box.top + 38 * s, box.right - 14 * s, box.bottom - 10 * s),
             theme::TextMuted, 12.0f * s);
}

// --------------------------------------------------------------- oturum kaliciligi ----

static ft::json::Value SerializePaneNode(const ft::PaneNode* node) {
    ft::json::Value obj = ft::json::Value::Object();
    if (!node) return obj;
    obj["id"] = (int64_t)node->id;
    obj["isSplit"] = node->isSplit;
    if (node->isSplit) {
        obj["dir"] = (node->dir == ft::SplitDirection::Vertical) ? "v" : "h";
        obj["ratio"] = (double)node->ratio;
        obj["first"] = SerializePaneNode(node->first.get());
        obj["second"] = SerializePaneNode(node->second.get());
    } else {
        if (node->tab) {
            if (node->tab->IsSftp()) {
                obj["type"] = "sftp";
                if (node->tab->Sftp() && node->tab->Sftp()->ConnectedHost()) {
                    obj["hostId"] = WideToUtf8(node->tab->Sftp()->ConnectedHost()->id);
                }
            } else if (!node->tab->GetSshHost().address.empty()) {
                obj["type"] = "ssh";
                obj["hostId"] = WideToUtf8(node->tab->GetSshHost().id);
                obj["hostAddr"] = WideToUtf8(node->tab->GetSshHost().address);
                obj["hostPort"] = (int64_t)node->tab->GetSshHost().port;
                obj["hostUser"] = WideToUtf8(node->tab->GetSshHost().username);
            } else {
                obj["type"] = "terminal";
                obj["profileId"] = WideToUtf8(node->tab->profile().id);
                obj["profileName"] = WideToUtf8(node->tab->profile().name);
            }
            obj["customTitle"] = WideToUtf8(node->tab->CustomTitle());
        }
    }
    return obj;
}

void MainWindow::SaveSession() {
    if (m_dataDir.empty()) return;
    std::wstring path = m_dataDir + L"\\session.json";

    if (m_tabs.empty()) {
        DeleteFileW(path.c_str());
        return;
    }

    ft::json::Value root = ft::json::Value::Object();
    root["version"] = 1;
    root["activeTab"] = (int64_t)m_active;
    root["view"] = (int64_t)m_view;

    ft::json::Value tabsArr = ft::json::Value::Array();
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        ft::json::Value tabObj = ft::json::Value::Object();
        if (i < m_tabLayouts.size() && m_tabLayouts[i]) {
            tabObj["focusedPaneId"] = (int64_t)m_tabLayouts[i]->GetFocusedPaneId();
            tabObj["isZoomed"] = m_tabLayouts[i]->IsZoomed();
            tabObj["layout"] = SerializePaneNode(m_tabLayouts[i]->Root());
        }
        if (m_tabs[i]) {
            tabObj["customTitle"] = WideToUtf8(m_tabs[i]->CustomTitle());
        }
        tabsArr.push_back(tabObj);
    }
    root["tabs"] = tabsArr;

    std::string serialized = root.dump();
    std::ofstream ofs(path, std::ios::trunc | std::ios::binary);
    if (ofs.is_open()) {
        ofs.write(serialized.data(), serialized.size());
    }
}

std::shared_ptr<TerminalTab> MainWindow::RestoreTabFromNode(const ft::json::Value& nodeVal, int cols, int rows) {
    std::string type = nodeVal["type"].as_string("terminal");
    std::string customTitle = nodeVal["customTitle"].as_string();

    if (type == "sftp") {
        std::string hostId = nodeVal["hostId"].as_string();
        const Host* h = !hostId.empty() ? m_inv.FindHost(Utf8ToWide(hostId)) : nullptr;
        auto tab = std::make_shared<TerminalTab>();
        tab->StartSftp(h, m_inv);
        if (!customTitle.empty()) tab->SetCustomTitle(Utf8ToWide(customTitle));
        return tab;
    }

    if (type == "ssh") {
        std::string hostId = nodeVal["hostId"].as_string();
        Host resolvedHost;
        bool found = false;
        if (!hostId.empty()) {
            if (const Host* h = m_inv.FindHost(Utf8ToWide(hostId))) {
                resolvedHost = *h;
                found = true;
            }
        }
        if (!found) {
            resolvedHost.address = Utf8ToWide(nodeVal["hostAddr"].as_string());
            resolvedHost.port = (int)nodeVal["hostPort"].as_int(22);
            resolvedHost.username = Utf8ToWide(nodeVal["hostUser"].as_string());
            resolvedHost.label = resolvedHost.Target();
            if (!resolvedHost.address.empty()) found = true;
        }

        if (found) {
            if (!resolvedHost.identityId.empty()) {
                if (const Identity* id = m_inv.FindIdentity(resolvedHost.identityId)) {
                    if (resolvedHost.password.empty()) resolvedHost.password = id->password;
                    if (resolvedHost.username.empty()) resolvedHost.username = id->username;
                    resolvedHost.kind = id->kind;
                    if (resolvedHost.keyPath.empty()) resolvedHost.keyPath = id->keyPath;
                    if (resolvedHost.publicKeyPath.empty()) resolvedHost.publicKeyPath = id->publicKeyPath;
                    if (resolvedHost.certPath.empty()) resolvedHost.certPath = id->certPath;
                }
            }

            std::wstring cmdErr;
            const std::wstring cmd = m_inv.BuildSshCommand(resolvedHost, &cmdErr);
            if (!cmd.empty()) {
                ShellProfile p;
                p.id = L"host:" + resolvedHost.id;
                p.name = resolvedHost.Display();
                p.badge = resolvedHost.production ? L"PROD" : L"SSH";
                p.accent = resolvedHost.production ? theme::Red : (resolvedHost.accent ? resolvedHost.accent : theme::Ac());
                p.rawCommand = cmd;
                p.startDir = UserHomeDir();
                p.kind = ProfileKind::Custom;

                auto tab = std::make_shared<TerminalTab>();
                tab->SetSshSession(resolvedHost);
                std::wstring err;
                if (tab->Start(p, cols, rows, m_hwnd, WM_PTY_DATA, &err)) {
                    if (resolvedHost.kind == AuthKind::Password && !resolvedHost.password.empty()) {
                        tab->SetAutoPassword(resolvedHost.password, TrimWs(resolvedHost.username), TrimWs(resolvedHost.address));
                    }
                    if (!customTitle.empty()) tab->SetCustomTitle(Utf8ToWide(customTitle));
                    return tab;
                }
            }
        }
    }

    // Normal terminal
    std::string pid = nodeVal["profileId"].as_string();
    std::string pname = nodeVal["profileName"].as_string();
    size_t profIdx = DefaultProfileIndex();
    for (size_t i = 0; i < m_profiles.size(); ++i) {
        if (!pid.empty() && WideToUtf8(m_profiles[i].id) == pid) {
            profIdx = i;
            break;
        }
        if (!pname.empty() && WideToUtf8(m_profiles[i].name) == pname) {
            profIdx = i;
            break;
        }
    }
    if (profIdx >= m_profiles.size()) profIdx = 0;

    ShellProfile p = m_profiles.empty() ? ShellProfile{} : m_profiles[profIdx];
    ApplyK8sEnv(p);
    auto tab = std::make_shared<TerminalTab>();
    std::wstring err;
    tab->Start(p, cols, rows, m_hwnd, WM_PTY_DATA, &err);
    if (!customTitle.empty()) tab->SetCustomTitle(Utf8ToWide(customTitle));
    return tab;
}

std::unique_ptr<PaneNode> MainWindow::RestorePaneNode(const ft::json::Value& nodeVal, uint32_t& maxId, int cols, int rows) {
    if (!nodeVal.is_object()) return nullptr;
    auto node = std::make_unique<PaneNode>();
    node->id = (uint32_t)nodeVal["id"].as_int(1);
    if (node->id > maxId) maxId = node->id;
    node->isSplit = nodeVal["isSplit"].as_bool(false);

    if (node->isSplit) {
        std::string d = nodeVal["dir"].as_string("v");
        node->dir = (d == "h") ? SplitDirection::Horizontal : SplitDirection::Vertical;
        node->ratio = (float)nodeVal["ratio"].as_double(0.5);
        if (node->ratio < 0.1f || node->ratio > 0.9f) node->ratio = 0.5f;

        node->first = RestorePaneNode(nodeVal["first"], maxId, cols, rows);
        node->second = RestorePaneNode(nodeVal["second"], maxId, cols, rows);
    } else {
        node->tab = RestoreTabFromNode(nodeVal, cols, rows);
    }
    return node;
}

bool MainWindow::RestoreSession() {
    if (m_dataDir.empty() || !m_r.Ready()) return false;
    std::wstring path = m_dataDir + L"\\session.json";

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (content.empty()) return false;

    bool ok = false;
    ft::json::Value root = ft::json::Value::parse(content, &ok);
    if (!ok || !root.is_object() || !root.has("tabs")) return false;

    const auto& tabsArr = root["tabs"];
    if (!tabsArr.is_array() || tabsArr.size() == 0) return false;

    const FontMetrics& fm = m_r.Metrics();
    int cols = (fm.cellW > 0.0f) ? (int)((m_lay.term.right - m_lay.term.left) / fm.cellW) : 80;
    int rows = (fm.cellH > 0.0f) ? (int)((m_lay.term.bottom - m_lay.term.top) / fm.cellH) : 24;
    cols = std::clamp(cols, 80, 500);
    rows = std::clamp(rows, 24, 200);

    m_tabs.clear();
    m_tabLayouts.clear();
    m_labelCache.clear();

    for (size_t i = 0; i < tabsArr.size(); ++i) {
        const auto& tabVal = tabsArr[i];
        if (!tabVal.is_object()) continue;

        uint32_t maxId = 1;
        std::unique_ptr<PaneNode> rootNode;
        if (tabVal.has("layout")) {
            rootNode = RestorePaneNode(tabVal["layout"], maxId, cols, rows);
        }

        if (!rootNode) {
            auto singleTab = RestoreTabFromNode(tabVal, cols, rows);
            rootNode = std::make_unique<PaneNode>(1, singleTab);
        }

        uint32_t focusedId = (uint32_t)tabVal["focusedPaneId"].as_int(1);
        auto layout = std::make_unique<PaneLayout>();
        layout->SetRoot(std::move(rootNode), focusedId, maxId + 1);

        if (tabVal["isZoomed"].as_bool(false)) {
            layout->ToggleZoom();
        }

        auto activeTabInPane = layout->GetFocusedTab();
        if (tabVal.has("customTitle")) {
            std::string ct = tabVal["customTitle"].as_string();
            if (!ct.empty() && activeTabInPane) {
                activeTabInPane->SetCustomTitle(Utf8ToWide(ct));
            }
        }

        m_tabs.push_back(activeTabInPane);
        m_tabLayouts.push_back(std::move(layout));
    }

    if (m_tabs.empty()) return false;

    size_t act = (size_t)root["activeTab"].as_int(0);
    m_active = std::min(act, m_tabs.size() - 1);
    int vw = (int)root["view"].as_int(0);
    m_view = (vw >= 0 && vw <= 8) ? static_cast<View>(vw) : View::Terminal;

    ClearSelection();
    ComputeLayout();
    m_dirty = true;
    return true;
}

// --------------------------------------------------------------- oturum ----

TerminalTab* MainWindow::Active() {

    if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
        return m_tabLayouts[m_active]->GetFocusedTab().get();
    }
    if (m_active < m_tabs.size()) return m_tabs[m_active].get();
    return nullptr;
}

bool MainWindow::NewTab(size_t profileIndex) {
    if (profileIndex >= m_profiles.size() || !m_r.Ready()) return false;

    const FontMetrics& fm = m_r.Metrics();
    const int cols = std::max(8, (int)((m_lay.term.right - m_lay.term.left) / fm.cellW));
    const int rows = std::max(2, (int)((m_lay.term.bottom - m_lay.term.top) / fm.cellH));

    ShellProfile p = m_profiles[profileIndex];
    const bool k8sEnv = ApplyK8sEnv(p);

    auto tab = std::make_shared<TerminalTab>();
    std::wstring err;
    if (!tab->Start(p, cols, rows, m_hwnd, WM_PTY_DATA, &err)) {
        m_error = err;
        SetView(View::Terminal);
        m_dirty = true;
        return false;
    }
    m_error.clear();
    m_tabs.push_back(tab);
    m_tabLayouts.push_back(std::make_unique<ft::PaneLayout>(tab));
    m_active = m_tabs.size() - 1;
    ClearSelection();
    ComputeLayout();
    SetView(View::Terminal);
    m_dirty = true;
    if (k8sEnv) Toast(L"K8s ortami yuklendi: " + K8sManager::Instance().KubeContextSummary());
    SaveSession();
    return true;
}

void MainWindow::NewSftpTab(const Host* host) {
    if (!m_r.Ready()) return;
    auto tab = std::make_shared<TerminalTab>();
    tab->StartSftp(host, m_inv);
    m_tabs.push_back(tab);
    m_tabLayouts.push_back(std::make_unique<ft::PaneLayout>(tab));
    m_active = m_tabs.size() - 1;
    ClearSelection();
    ComputeLayout();
    SetView(View::Terminal);
    m_dirty = true;
    if (host) {
        Toast(L"SFTP sekmesi açıldı: " + host->Display());
    } else {
        Toast(L"SFTP Dosya Gezgini açıldı");
    }
    SaveSession();
}

bool MainWindow::NewK8sTab() {
    if (!m_r.Ready()) return false;
    size_t defProf = DefaultProfileIndex();
    if (defProf >= m_profiles.size()) defProf = 0;
    ShellProfile p = m_profiles.empty() ? ShellProfile{} : m_profiles[defProf];
    p.badge = L"K8S";
    p.accent = 0x326CE5;

    const auto env = K8sManager::Instance().TerminalEnv(p.kind == ProfileKind::Wsl);
    p.env.insert(p.env.end(), env.begin(), env.end());

    std::wstring ctxSummary = K8sManager::Instance().KubeContextSummary();
    if (!ctxSummary.empty()) {
        p.name = L"K8s: " + ctxSummary;
    } else {
        p.name = L"Kubernetes Shell";
    }

    const FontMetrics& fm = m_r.Metrics();
    const int cols = std::max(8, (int)((m_lay.term.right - m_lay.term.left) / fm.cellW));
    const int rows = std::max(2, (int)((m_lay.term.bottom - m_lay.term.top) / fm.cellH));

    auto tab = std::make_shared<TerminalTab>();
    std::wstring err;
    if (!tab->Start(p, cols, rows, m_hwnd, WM_PTY_DATA, &err)) {
        m_error = err;
        SetView(View::Terminal);
        m_dirty = true;
        return false;
    }
    m_error.clear();
    m_tabs.push_back(tab);
    m_tabLayouts.push_back(std::make_unique<ft::PaneLayout>(tab));
    m_active = m_tabs.size() - 1;
    ClearSelection();
    ComputeLayout();
    SetView(View::Terminal);
    m_dirty = true;
    Toast(L"Kubernetes terminali açıldı: " + (ctxSummary.empty() ? L"kubectl hazır" : ctxSummary));
    return true;
}

bool MainWindow::ApplyK8sEnv(ShellProfile& p) const {
    if (!m_cfg.k8sAutoEnv) return false;
    const auto env = K8sManager::Instance().TerminalEnv(p.kind == ProfileKind::Wsl);
    if (env.empty()) return false;
    p.env.insert(p.env.end(), env.begin(), env.end());
    return true;
}

bool MainWindow::ConnectHost(const Host& h) {
    Host resolvedHost = h;
    if (!resolvedHost.identityId.empty()) {
        if (const Identity* id = m_inv.FindIdentity(resolvedHost.identityId)) {
            if (resolvedHost.password.empty()) resolvedHost.password = id->password;
            if (resolvedHost.username.empty()) resolvedHost.username = id->username;
            resolvedHost.kind = id->kind;
            if (resolvedHost.keyPath.empty()) resolvedHost.keyPath = id->keyPath;
            if (resolvedHost.publicKeyPath.empty()) resolvedHost.publicKeyPath = id->publicKeyPath;
            if (resolvedHost.certPath.empty()) resolvedHost.certPath = id->certPath;
        }
    }

    std::wstring cmdErr;
    const std::wstring cmd = m_inv.BuildSshCommand(resolvedHost, &cmdErr);
    if (cmd.empty()) {
        // Bos komutun iki nedeni var: ssh.exe yok ya da adres/kullanici gecersiz.
        // Hepsine "ssh.exe bulunamadi" demek yanlis adresli hostta yaniltiyordu.
        if (FindSshExe().empty()) {
            m_error = L"Windows OpenSSH istemcisi (ssh.exe) bulunamadi. "
                      L"Ayarlar > Uygulamalar > Istege bagli ozellikler uzerinden kurabilirsin.";
        } else {
            m_error = cmdErr.empty() ? L"SSH komutu olusturulamadi." : cmdErr;
        }
        SetView(View::Terminal);
        m_dirty = true;
        return false;
    }
    if (!m_r.Ready()) return false;

    ShellProfile p;
    p.id = L"host:" + resolvedHost.id;
    p.name = resolvedHost.Display();
    p.badge = resolvedHost.production ? L"PROD" : L"SSH";
    p.accent = resolvedHost.production ? theme::Red : (resolvedHost.accent ? resolvedHost.accent : theme::Ac());
    p.rawCommand = cmd;
    p.startDir = UserHomeDir();
    p.kind = ProfileKind::Custom;

    const FontMetrics& fm = m_r.Metrics();
    const int cols = std::max(8, (int)((m_lay.term.right - m_lay.term.left) / fm.cellW));
    const int rows = std::max(2, (int)((m_lay.term.bottom - m_lay.term.top) / fm.cellH));

    m_sshLogScroll = -1.0f;
    auto tab = std::make_shared<TerminalTab>();
    tab->SetSshSession(resolvedHost);
    std::wstring err;
    if (!tab->Start(p, cols, rows, m_hwnd, WM_PTY_DATA, &err)) {
        m_error = err;
        SetView(View::Terminal);
        m_dirty = true;
        return false;
    }

    std::wstring pw = resolvedHost.password;
    std::wstring user = TrimWs(resolvedHost.username);
    AuthKind kind = resolvedHost.kind;
    if (!pw.empty() && kind == AuthKind::Password) {
        tab->SetAutoPassword(pw, user, TrimWs(resolvedHost.address));
    }
    SecureZeroMemory(pw.data(), pw.size() * sizeof(wchar_t));

    m_error.clear();
    m_tabs.push_back(tab);
    m_tabLayouts.push_back(std::make_unique<ft::PaneLayout>(tab));
    m_active = m_tabs.size() - 1;
    ClearSelection();
    ComputeLayout();
    SetView(View::Terminal);
    m_dirty = true;
    SaveSession();
    return true;
}

bool MainWindow::QuickConnect(const std::wstring& text) {
    std::wstring t = TrimWs(text);
    if (t.rfind(L"ssh ", 0) == 0) t = TrimWs(t.substr(4));
    if (t.empty()) return false;

    Host h;
    h.port = 22;
    h.kind = AuthKind::Agent;

    // "ssh -p 2222 -l root -J bastion host" bicimi: bilinen secenekleri once ayikla.
    // Eskiden "-p 2222 root" kullanici adina karisiyordu; ssh insasi artik '-' ile
    // baslayan alanlari (dogru olarak) reddettigi icin bunlari biz ayristirmaliyiz.
    {
        std::vector<std::wstring> words;
        size_t i = 0;
        while (i < t.size()) {
            while (i < t.size() && iswspace(t[i])) ++i;
            size_t j = i;
            while (j < t.size() && !iswspace(t[j])) ++j;
            if (j > i) words.push_back(t.substr(i, j - i));
            i = j;
        }
        std::wstring rest;
        for (size_t k = 0; k < words.size(); ++k) {
            const std::wstring& w = words[k];
            const bool hasNext = k + 1 < words.size();
            if (w == L"-p" && hasNext)      { h.port = std::clamp(_wtoi(words[++k].c_str()), 1, 65535); }
            else if (w == L"-l" && hasNext) { h.username = words[++k]; }
            else if (w == L"-J" && hasNext) { h.jumpHost = words[++k]; }
            else if (w.size() > 2 && w.rfind(L"-p", 0) == 0 && iswdigit(w[2])) { h.port = std::clamp(_wtoi(w.c_str() + 2), 1, 65535); }
            else if (!w.empty() && w[0] == L'-') {
                Toast(L"Hizli baglanti yalnizca -p, -l ve -J seceneklerini tanir: " + w);
                return false;
            } else if (rest.empty()) { rest = w; }
            else {
                Toast(L"Hizli baglanti tek bir hedef bekler (kullanici@host[:port])");
                return false;
            }
        }
        t = rest;
        if (t.empty()) return false;
    }

    const size_t at = t.find(L'@');
    if (at != std::wstring::npos) { h.username = t.substr(0, at); t = t.substr(at + 1); }
    const size_t colon = t.find(L':');
    if (colon != std::wstring::npos) {
        h.port = std::clamp(_wtoi(t.substr(colon + 1).c_str()), 1, 65535);
        t = t.substr(0, colon);
    }
    h.address = t;
    if (h.address.empty()) return false;
    h.label = h.Target();
    return ConnectHost(h);
}

// Yerel kabuklar, SSH ve K8s hemen (surec baslatmaz); docker/wsl sorgusu
// saniyeler surebilir, UI donmasin diye arka is parcaciginda. Onceki tarama
// sonucu yenisi gelene kadar gorunur kalir.
void MainWindow::RefreshHubNodes(bool probeSlow) {
    m_fastNodes = m_inv.FastNodes();
    m_hubNodes = m_fastNodes;
    m_hubNodes.insert(m_hubNodes.end(), m_slowNodes.begin(), m_slowNodes.end());
    m_lastHubScan = NowTicks();
    m_dirty = true;
    if (probeSlow) {
        if (m_hubProbing) m_hubProbeQueued = true;
        else StartHubProbe();
    }
}

void MainWindow::StartHubProbe() {
    if (!m_hwnd) return;
    m_hubProbing = true;
    const uint64_t gen = ++m_hubGen;
    const HWND hwnd = m_hwnd;
    try {
        std::thread([hwnd, gen] {
            // Is parcacigindan kacan istisna std::terminate olur; bos sonuc da
            // gonderilir ki m_hubProbing takili kalip sonraki taramalar durmasin.
            HubProbeResult* res = nullptr;
            try {
                res = new HubProbeResult{ gen, Inventory::ProbeSlowNodes() };
            } catch (...) {
                res = new (std::nothrow) HubProbeResult{};
                if (res) res->gen = gen;
            }
            if (res && !PostMessageW(hwnd, WM_HUB_NODES, 0, reinterpret_cast<LPARAM>(res))) delete res;
        }).detach();
    } catch (...) {
        m_hubProbing = false;   // is parcacigi acilamadi; hizli dugumler yine gorunur
    }
}

bool MainWindow::LaunchNode(const ConnectionNode& node) {
    if (!m_r.Ready()) return false;

    if (node.type == NodeType::SshHost) {
        if (Host* h = m_inv.FindHost(Utf8ToWide(node.id))) {
            return ConnectHost(*h);
        }
        // Host silinmis: eski kart hedefine (portsuz) baglanmak yerine listeyi
        // tazele. Dikkat: node m_hubNodes icinde olabilir, yenilemeden sonra kullanma.
        RefreshHubNodes(false);
        Toast(L"Host bulunamadi; liste yenilendi");
        return false;
    }

    ShellProfile p;
    p.id = Utf8ToWide(node.id);
    p.name = Utf8ToWide(node.name);
    p.startDir = UserHomeDir();

    if (node.type == NodeType::Wsl) {
        p.kind = ProfileKind::Wsl;
        p.badge = L"WSL";
        p.accent = 0x22C55E; // Emerald Green
        p.wslDistro = Utf8ToWide(node.name);
        p.rawCommand = ResolveTool(L"wsl.exe") + L" -d " + Utf8ToWide(node.name);
    } else if (node.type == NodeType::DockerContainer) {
        p.kind = ProfileKind::Custom;
        p.badge = L"DKR";
        p.accent = 0x0EA5E9; // Docker Sky Blue
        p.name = L"Docker: " + Utf8ToWide(node.name);
        p.rawCommand = ResolveTool(L"docker.exe") + L" exec -it " + Utf8ToWide(node.name) + L" sh";
    } else if (node.type == NodeType::Local) {
        bool found = false;
        for (const auto& prof : m_profiles) {
            if (prof.id == Utf8ToWide(node.id)) {
                p = prof;
                found = true;
                break;
            }
        }
        if (!found) {
            p.rawCommand = Utf8ToWide(node.target);
            p.badge = L"LOCAL";
            p.accent = theme::Ac();
        }
    } else if (node.type == NodeType::K8sPod) {
        p.kind = ProfileKind::Custom;
        p.badge = L"K8S";
        p.accent = 0x326CE5; // Kubernetes Blue
        p.name = L"K8s: " + Utf8ToWide(node.name);
        p.rawCommand = Utf8ToWide(node.target);
        if (p.rawCommand.empty()) {
            // Hedef yalnizca K8sManager::ExecCommand'den gelir (dogrulanmis, tirnakli).
            // Gorunen addan komut uydurmak "(konteyner)" ekli adlarda ve
            // Deployment'larda yanlis kaynaga gider.
            Toast(L"Bu Kubernetes kaynagina terminal acilamaz (kubectl exec hedefi degil)");
            return false;
        }
        for (const wchar_t* pre : { L"kubectl.exe ", L"kubectl " }) {
            const size_t n = wcslen(pre);
            if (p.rawCommand.compare(0, n, pre) == 0) {
                p.rawCommand = ResolveTool(L"kubectl.exe") + L" " + p.rawCommand.substr(n);
                break;
            }
        }
    } else {
        p.rawCommand = Utf8ToWide(node.target);
        p.badge = L"SHELL";
        p.accent = theme::Ac();
    }

    // SSH disindaki her yerel sekme eklenen kubeconfig'leri gorur.
    const bool k8sEnv = ApplyK8sEnv(p);

    const FontMetrics& fm = m_r.Metrics();
    const int cols = std::max(8, (int)((m_lay.term.right - m_lay.term.left) / fm.cellW));
    const int rows = std::max(2, (int)((m_lay.term.bottom - m_lay.term.top) / fm.cellH));

    auto tab = std::make_shared<TerminalTab>();
    std::wstring err;
    if (!tab->Start(p, cols, rows, m_hwnd, WM_PTY_DATA, &err)) {
        m_error = err;
        SetView(View::Terminal);
        m_dirty = true;
        return false;
    }

    m_error.clear();
    m_tabs.push_back(tab);
    m_tabLayouts.push_back(std::make_unique<ft::PaneLayout>(tab));
    m_active = m_tabs.size() - 1;
    ClearSelection();
    ComputeLayout();
    SetView(View::Terminal);
    m_dirty = true;
    if (k8sEnv) Toast(L"Oturum baslatildi: " + p.name + L"  |  K8s ortami yuklendi: " +
                      K8sManager::Instance().KubeContextSummary());
    else        Toast(L"Oturum baslatildi: " + p.name);
    SaveSession();
    return true;
}

void MainWindow::CloseTab(size_t index) {
    if (index >= m_tabs.size()) return;
    if (index < m_tabLayouts.size() && m_tabLayouts[index]) {
        for (auto& t : m_tabLayouts[index]->GetAllTabs()) {
            if (t) t->Close();
        }
        m_tabLayouts.erase(m_tabLayouts.begin() + (ptrdiff_t)index);
    } else {
        if (m_tabs[index]) m_tabs[index]->Close();
    }
    m_tabs.erase(m_tabs.begin() + (ptrdiff_t)index);
    if (index < m_labelCache.size()) {
        m_labelCache.erase(m_labelCache.begin() + (ptrdiff_t)index);
    }
    // Aktifin SOLUNDAKI bir sekme kapatilinca aktif indeks kaymali,
    // yoksa sessizce baska bir oturuma gecilir.
    if (m_tabs.empty())                 m_active = 0;
    else if (index < m_active)          --m_active;
    else if (m_active >= m_tabs.size()) m_active = m_tabs.size() - 1;

    m_hoverTab = -1;
    m_hoverClose = -1;
    m_pressClose = -1;
    ClearSelection();
    ComputeLayout();
    m_dirty = true;
    SaveSession();
}

void MainWindow::SelectTab(size_t index) {
    if (index >= m_tabs.size()) return;
    if (index != m_active) ClearSelection();   // ayni sekmeye tik secimi silmesin
    m_active = index;
    if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
        m_tabs[m_active] = m_tabLayouts[m_active]->GetFocusedTab();
    }
    m_lastRevision = 0;
    // Yalnizca kaydirma penceresini duzeltir; genislikler m_active'den
    // bagimsiz oldugu icin gorunur bir sekme secilince serit oynamaz.
    ComputeLayout();
    m_dirty = true;
    SaveSession();
}

void MainWindow::SplitActiveTab(ft::SplitDirection dir) {
    if (m_active >= m_tabLayouts.size() || !m_tabLayouts[m_active] || !m_r.Ready()) return;

    ShellProfile p;
    Host sshHost;
    bool isSsh = false;
    if (auto* cur = Active()) {
        p = cur->profile();
        if (!cur->GetSshHost().address.empty()) {
            sshHost = cur->GetSshHost();
            isSsh = true;
        }
    } else if (DefaultProfileIndex() < m_profiles.size()) {
        p = m_profiles[DefaultProfileIndex()];
    } else {
        return;
    }

    const FontMetrics& fm = m_r.Metrics();
    const int cols = std::max(8, (int)((m_lay.term.right - m_lay.term.left) / (fm.cellW * 2)));
    const int rows = std::max(2, (int)((m_lay.term.bottom - m_lay.term.top) / (fm.cellH * 2)));

    auto newTab = std::make_shared<TerminalTab>();
    if (isSsh) {
        newTab->SetSshSession(sshHost);
    }
    std::wstring err;
    if (!newTab->Start(p, cols, rows, m_hwnd, WM_PTY_DATA, &err)) {
        m_error = err;
        m_dirty = true;
        return;
    }
    if (isSsh && sshHost.kind == AuthKind::Password && !sshHost.password.empty()) {
        newTab->SetAutoPassword(sshHost.password, TrimWs(sshHost.username), TrimWs(sshHost.address));
    }

    m_tabLayouts[m_active]->SplitActive(dir, newTab);
    m_tabs[m_active] = m_tabLayouts[m_active]->GetFocusedTab();

    SyncGridToArea();
    ComputeLayout();
    m_dirty = true;
    SaveSession();
    Toast(dir == ft::SplitDirection::Vertical ? L"Panel dikey bolundu (Ctrl+Shift+D)"
                                              : L"Panel yatay bolundu (Ctrl+Shift+E)");
}

void MainWindow::TogglePaneZoom() {
    if (m_active >= m_tabLayouts.size() || !m_tabLayouts[m_active]) return;
    m_tabLayouts[m_active]->ToggleZoom();
    SyncGridToArea();
    m_dirty = true;
    Toast(m_tabLayouts[m_active]->IsZoomed() ? L"Panel tam ekran (Zoom: Ctrl+Shift+Z)"
                                            : L"Panel normal gorunume dondu");
}

void MainWindow::CloseActivePaneOrTab() {
    if (m_active >= m_tabLayouts.size() || !m_tabLayouts[m_active]) {
        if (m_active < m_tabs.size()) CloseTab(m_active);
        return;
    }
    auto& layout = m_tabLayouts[m_active];
    if (layout->PaneCount() > 1) {
        if (auto focused = layout->GetFocusedTab()) {
            focused->Close();
        }
        layout->CloseActive();
        m_tabs[m_active] = layout->GetFocusedTab();
        SyncGridToArea();
        ComputeLayout();
        m_dirty = true;
        SaveSession();
        Toast(L"Panel kapatildi");
    } else {
        CloseTab(m_active);
        Toast(L"Sekme kapatildi");
    }
}


void MainWindow::CyclePaneFocus(bool forward) {
    if (m_active >= m_tabLayouts.size() || !m_tabLayouts[m_active]) return;
    m_tabLayouts[m_active]->CycleFocus(forward);
    m_tabs[m_active] = m_tabLayouts[m_active]->GetFocusedTab();
    m_dirty = true;
}

D2D1_RECT_F MainWindow::GetFocusedPaneArea() const {
    if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active] &&
        m_tabLayouts[m_active]->PaneCount() > 1 && !m_tabLayouts[m_active]->IsZoomed()) {
        auto panes = const_cast<ft::PaneLayout*>(m_tabLayouts[m_active].get())->ComputeLayout(m_lay.term);
        uint32_t fId = m_tabLayouts[m_active]->GetFocusedPaneId();
        for (const auto& p : panes) {
            if (p.id == fId) return p.area;
        }
    }
    return m_lay.term;
}

void MainWindow::ShowProfileMenu(POINT screenPt) {
    enum ActionKind {
        ActNone,
        ActProfile,
        ActSsh,
        ActSftpEmpty,
        ActSftpHost,
        ActK8s,
        ActDocker,
        ActHub
    };
    struct ActionItem {
        ActionKind kind = ActNone;
        size_t index = 0;
    };

    std::vector<std::wstring> items;
    std::vector<ActionItem> actions;

    auto add = [&](const std::wstring& label, ActionKind k, size_t idx = 0) {
        items.push_back(label);
        actions.push_back({ k, idx });
    };

    // 1. Yerel Kabuk Profilleri
    for (size_t i = 0; i < m_profiles.size(); ++i) {
        std::wstring label = m_profiles[i].name;
        if (m_profiles[i].kind == ProfileKind::Wsl && m_profiles[i].wslDefault) label += L"  " + std::wstring(Tr(Msg::DefaultTag));
        add(label, ActProfile, i);
    }

    // 2. Kayıtlı SSH Sunucuları
    const auto& hosts = m_inv.hosts();
    if (!hosts.empty()) {
        add(L"---", ActNone);
        for (size_t i = 0; i < hosts.size(); ++i) {
            add(L"🌐  SSH: " + hosts[i].Display(), ActSsh, i);
        }
    }

    // 3. SFTP Gezgini Seçenekleri
    add(L"---", ActNone);
    add(L"📂  Yeni SFTP Gezgini (Yerel / Bağımsız)", ActSftpEmpty);
    for (size_t i = 0; i < hosts.size(); ++i) {
        add(L"📂  SFTP: " + hosts[i].Display(), ActSftpHost, i);
    }

    // 4. Docker Konteynerleri
    std::vector<ConnectionNode> dockerNodes;
    for (const auto& nd : m_hubNodes) {
        if (nd.type == NodeType::DockerContainer) {
            dockerNodes.push_back(nd);
        }
    }
    if (!dockerNodes.empty()) {
        add(L"---", ActNone);
        for (size_t i = 0; i < dockerNodes.size(); ++i) {
            add(L"🐳  Docker: " + Utf8ToWide(dockerNodes[i].name), ActDocker, i);
        }
    }

    // 6. Sistemler & Hub
    add(L"---", ActNone);
    add(L"⚡  Sistemler & Hub'ı Aç...", ActHub);

    const int sel = ShowListMenu(screenPt, items);
    if (sel >= 0 && (size_t)sel < actions.size()) {
        const auto& act = actions[sel];
        switch (act.kind) {
        case ActProfile:
            NewTab(act.index);
            break;
        case ActSsh:
            if (act.index < hosts.size()) {
                ConnectHost(hosts[act.index]);
            }
            break;
        case ActSftpEmpty:
            NewSftpTab(nullptr);
            break;
        case ActSftpHost:
            if (act.index < hosts.size()) {
                NewSftpTab(&hosts[act.index]);
            }
            break;
        case ActK8s:
            NewK8sTab();
            break;
        case ActDocker:
            if (act.index < dockerNodes.size()) {
                LaunchNode(dockerNodes[act.index]);
            }
            break;
        case ActHub:
            SetView(View::Hosts);
            break;
        default:
            break;
        }
    }
}

int MainWindow::ShowListMenu(POINT screenPt, const std::vector<std::wstring>& items) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return -1;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i] == L"---") {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        } else {
            AppendMenuW(menu, MF_STRING, (UINT_PTR)(i + 1), items[i].c_str());
        }
    }
    const int cmd = (int)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                          screenPt.x, screenPt.y, m_hwnd, nullptr);
    DestroyMenu(menu);
    m_dirty = true;
    return cmd > 0 ? cmd - 1 : -1;
}

void MainWindow::ShowTerminalContextMenu(POINT screenPt) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    const bool hasLayout = (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]);
    const bool isMultiPane = hasLayout && m_tabLayouts[m_active]->PaneCount() > 1;
    const bool isZoomed = hasLayout && m_tabLayouts[m_active]->IsZoomed();
    const bool hasSelection = m_sel.active;

    enum CtxCmd {
        CmdSplitV = 101,
        CmdSplitH,
        CmdBroadcast,
        CmdZoom,
        CmdClosePane,
        CmdCopy,
        CmdPaste,
        CmdClear,
        CmdFleet
    };

    const std::wstring splitVMenu = L"◫  " + std::wstring(Tr(Msg::ActionSplitV)) + L"\tCtrl+Shift+D";
    const std::wstring splitHMenu = L"⬒  " + std::wstring(Tr(Msg::ActionSplitH)) + L"\tCtrl+Shift+E";
    const std::wstring bcastMenu = m_broadcastMode
        ? (L"✔  📡 " + std::wstring(Tr(Msg::ToolbarBroadcast)) + L" (ON)\tCtrl+Shift+I")
        : (L"📡 " + std::wstring(Tr(Msg::ToolbarBroadcast)) + L" (OFF)\tCtrl+Shift+I");
    AppendMenuW(menu, MF_STRING, CmdSplitV, splitVMenu.c_str());
    AppendMenuW(menu, MF_STRING, CmdSplitH, splitHMenu.c_str());
    AppendMenuW(menu, MF_STRING | (m_broadcastMode ? MF_CHECKED : 0), CmdBroadcast, bcastMenu.c_str());

    if (isMultiPane) {
        const std::wstring zoomLabel = isZoomed
            ? (L"🔍 " + std::wstring(Tr(Msg::MenuZoomOut)) + L"\tCtrl+Shift+Z")
            : (L"🔍 " + std::wstring(Tr(Msg::MenuZoomIn)) + L"\tCtrl+Shift+Z");
        const std::wstring closePaneLabel = L"✕  " + std::wstring(Tr(Msg::MenuCloseActivePane)) + L"\tCtrl+Shift+W";
        AppendMenuW(menu, MF_STRING | (isZoomed ? MF_CHECKED : 0), CmdZoom, zoomLabel.c_str());
        AppendMenuW(menu, MF_STRING, CmdClosePane, closePaneLabel.c_str());
    } else {
        const std::wstring closeTabLabel = L"✕  " + std::wstring(Tr(Msg::MenuCloseActiveTab)) + L"\tCtrl+Shift+W";
        AppendMenuW(menu, MF_STRING, CmdClosePane, closeTabLabel.c_str());
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::wstring copyLabel = L"📋 " + std::wstring(Tr(Msg::MenuCopy)) + L"\tCtrl+Shift+C";
    const std::wstring pasteLabel = L"📥 " + std::wstring(Tr(Msg::MenuPaste)) + L"\tCtrl+Shift+V";
    const std::wstring clearLabel = L"🧹 " + std::wstring(Tr(Msg::MenuClearTerminal));
    AppendMenuW(menu, MF_STRING | (hasSelection ? 0 : MF_GRAYED), CmdCopy, copyLabel.c_str());
    AppendMenuW(menu, MF_STRING, CmdPaste, pasteLabel.c_str());
    AppendMenuW(menu, MF_STRING, CmdClear, clearLabel.c_str());

    // Snippets & Kubernetes YAML alt menüsü
    HMENU snipMenu = CreatePopupMenu();
    const auto& allSnippets = m_snippets.AllSnippets();
    std::map<std::wstring, HMENU> catMenus;

    for (size_t i = 0; i < allSnippets.size(); ++i) {
        const auto& snp = allSnippets[i];
        std::wstring cat = snp.category.empty() ? std::wstring(Tr(Msg::CustomColors)) : snp.category;
        if (snp.isYaml) cat = L"☸️ " + std::wstring(Tr(Msg::MenuSnippetsK8s));
        if (catMenus.find(cat) == catMenus.end()) {
            catMenus[cat] = CreatePopupMenu();
        }
        HMENU targetMenu = catMenus[cat];
        UINT_PTR cmdBase = 5000 + i * 2;
        if (snp.isYaml) {
            HMENU itemSub = CreatePopupMenu();
            const std::wstring applyLabel = L"🚀 " + std::wstring(Tr(Msg::MenuApplyYaml));
            const std::wstring exportLabel = L"📥 " + std::wstring(Tr(Msg::MenuExportYaml));
            AppendMenuW(itemSub, MF_STRING, cmdBase, applyLabel.c_str());
            AppendMenuW(itemSub, MF_STRING, cmdBase + 1, exportLabel.c_str());
            AppendMenuW(targetMenu, MF_POPUP, (UINT_PTR)itemSub, snp.title.c_str());
        } else {
            AppendMenuW(targetMenu, MF_STRING, cmdBase, snp.title.c_str());
        }
    }

    if (!catMenus.empty()) {
        for (const auto& [catName, hSub] : catMenus) {
            AppendMenuW(snipMenu, MF_POPUP, (UINT_PTR)hSub, catName.c_str());
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const std::wstring snipMenuTitle = L"📑  " + std::wstring(Tr(Msg::MenuSnippetsK8s));
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)snipMenu, snipMenuTitle.c_str());
    }

    SetForegroundWindow(m_hwnd);
    const int cmd = (int)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                          screenPt.x, screenPt.y, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd >= 5000) {
        size_t snpIdx = (size_t)(cmd - 5000) / 2;
        int subCmd = (cmd - 5000) % 2;
        if (snpIdx < allSnippets.size()) {
            const auto& snp = allSnippets[snpIdx];
            if (subCmd == 1 && snp.isYaml) {
                std::wstring outPath;
                if (m_snippets.ExportYaml(snp, m_dataDir + L"\\k8s", outPath)) {
                    Toast(L"YAML dışa aktarıldı: " + outPath);
                } else {
                    Toast(L"YAML aktarımı başarısız");
                }
            } else {
                std::string toSend;
                if (snp.isYaml) {
                    toSend = "cat << 'EOF' | kubectl apply -f -\n" + WideToUtf8(snp.yamlContent) + "\nEOF\n";
                } else {
                    toSend = WideToUtf8(snp.command) + "\n";
                }
                if (m_broadcastMode) {
                    BroadcastInput(toSend);
                } else if (auto* t = Active()) {
                    t->Write(toSend);
                }
                Toast(L"Komut terminale gönderildi: " + snp.title);
            }
        }
        return;
    }

    switch (cmd) {
    case CmdSplitV:
        SplitActiveTab(ft::SplitDirection::Vertical);
        break;
    case CmdSplitH:
        SplitActiveTab(ft::SplitDirection::Horizontal);
        break;
    case CmdBroadcast:
        ToggleBroadcastMode();
        break;
    case CmdZoom:
        TogglePaneZoom();
        break;
    case CmdClosePane:
        CloseActivePaneOrTab();
        break;
    case CmdCopy:
        CopySelection();
        break;
    case CmdPaste:
        PasteClipboard();
        break;
    case CmdClear:
        if (auto* t = Active()) {
            t->screen().Clear();
            m_dirty = true;
        }
        break;
    default:
        break;
    }
}


void MainWindow::StartRenameTab(size_t tabIdx) {
    if (tabIdx >= m_tabs.size() || !m_tabs[tabIdx]) return;
    m_renamingTab = true;
    m_renameTabIdx = tabIdx;
    m_renameTabText = m_tabs[tabIdx]->CustomTitle().empty() ? m_tabs[tabIdx]->Title() : m_tabs[tabIdx]->CustomTitle();
    m_ui.SetFocus(ID_TAB_RENAME);
    m_ui.SelectAll();
    m_in.clicked = false;
    m_in.px = m_in.py = -1.0f;
    m_dirty = true;
}

void MainWindow::ShowTabContextMenu(size_t tabIdx, POINT screenPt) {
    if (tabIdx >= m_tabs.size() || !m_tabs[tabIdx]) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    enum TabCmd {
        CmdRename = 201,
        CmdCopyTitle,
        CmdResetTitle,
        CmdClose,
        CmdCloseOthers,
        CmdCloseRight
    };

    AppendMenuW(menu, MF_STRING, CmdRename, Tr(Msg::MenuRenameTab));
    AppendMenuW(menu, MF_STRING, CmdCopyTitle, Tr(Msg::MenuCopyTabTitle));
    if (!m_tabs[tabIdx]->CustomTitle().empty()) {
        AppendMenuW(menu, MF_STRING, CmdResetTitle, Tr(Msg::MenuResetTabTitle));
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::wstring closeText = L"✕  " + std::wstring(Tr(Msg::ActionCloseTab));
    AppendMenuW(menu, MF_STRING, CmdClose, closeText.c_str());
    if (m_tabs.size() > 1) {
        AppendMenuW(menu, MF_STRING, CmdCloseOthers, Tr(Msg::MenuCloseOtherTabs));
        if (tabIdx + 1 < m_tabs.size()) {
            AppendMenuW(menu, MF_STRING, CmdCloseRight, Tr(Msg::MenuCloseRightTabs));
        }
    }

    SetForegroundWindow(m_hwnd);
    const int cmd = (int)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                          screenPt.x, screenPt.y, m_hwnd, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case CmdRename:
        StartRenameTab(tabIdx);
        break;
    case CmdCopyTitle:
        ClipboardSetText(m_hwnd, m_tabs[tabIdx]->Title());
        Toast(Tr(Msg::ToastCopied));
        break;
    case CmdResetTitle:
        m_tabs[tabIdx]->SetCustomTitle(L"");
        Toast(Tr(Msg::ToastTabReset));
        m_dirty = true;
        break;
    case CmdClose:
        CloseTab(tabIdx);
        break;
    case CmdCloseOthers: {
        auto keepTab = m_tabs[tabIdx];
        auto keepLayout = (tabIdx < m_tabLayouts.size()) ? std::move(m_tabLayouts[tabIdx]) : nullptr;
        m_tabs.clear();
        m_tabLayouts.clear();
        m_tabs.push_back(keepTab);
        if (keepLayout) m_tabLayouts.push_back(std::move(keepLayout));
        else {
            m_tabLayouts.push_back(std::make_unique<PaneLayout>(keepTab));
        }
        SelectTab(0);
        ComputeLayout();
        m_dirty = true;
        break;
    }
    case CmdCloseRight: {
        while (m_tabs.size() > tabIdx + 1) {
            CloseTab(m_tabs.size() - 1);
        }
        break;
    }
    default:
        break;
    }
}

void MainWindow::OpenRemoteFile(const std::wstring& remoteFileName) {
    SftpController* sftp = m_sftp.get();
    if (m_view == View::Terminal && Active() && Active()->IsSftp()) {
        sftp = Active()->Sftp();
    }
    if (!sftp || !sftp->RemoteFs() || sftp->RemoteState() != SftpConnectionState::Connected) {
        Toast(Tr(Msg::ToastNotConnected));
        return;
    }
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring destDir = std::wstring(tempDir) + L"FullTerminal_preview";
    CreateDirectoryW(destDir.c_str(), nullptr);
    std::wstring destFile = destDir + L"\\" + remoteFileName;

    std::wstring remoteFull = sftp->RemotePath();
    if (remoteFull.empty()) remoteFull = L"/";
    if (remoteFull.back() != L'/') remoteFull += L"/";
    remoteFull += remoteFileName;

    Toast(L"Dosya indiriliyor ve açılıyor: " + remoteFileName);
    auto* fs = sftp->RemoteFs();
    std::thread([this, fs, remoteFull, destFile, remoteFileName] {
        std::wstring err;
        if (fs && fs->Download(remoteFull, destFile, &err)) {
            ShellExecuteW(nullptr, L"open", destFile.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            PostMessageW(m_hwnd, WM_COMMAND, 0, 0);
        } else {
            PostMessageW(m_hwnd, WM_COMMAND, 0, 0);
        }
    }).detach();
}

void MainWindow::ShowSftpContextMenu(POINT screenPt, int clientX, int clientY) {
    SftpController* sftp = m_sftp.get();
    D2D1_RECT_F sftpArea = m_lay.main;
    if (m_view == View::Terminal && Active() && Active()->IsSftp()) {
        sftp = Active()->Sftp();
        sftpArea = m_lay.term;
    }
    if (!sftp) return;
    const float mid = std::floor((sftpArea.left + sftpArea.right) * 0.5f);
    const bool isRemote = (clientX >= mid);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    enum SftpCmd {
        CmdOpen = 301,
        CmdTransfer,     // Yükle veya İndir
        CmdRename,
        CmdDelete,
        CmdCopyPath,
        CmdNewFolder,
        CmdRefresh
    };

    const std::wstring selItem = isRemote ? m_sftpSelRemote : m_sftpSelLocal;
    const bool hasSel = !selItem.empty() && selItem != L"..";

    if (hasSel) {
        const std::wstring openLabel = L"📂  " + std::wstring(Tr(Msg::SftpMenuOpen));
        AppendMenuW(menu, MF_STRING, CmdOpen, openLabel.c_str());
        if (isRemote) {
            const std::wstring dlLabel = L"📥  " + std::wstring(Tr(Msg::SftpMenuDownload));
            AppendMenuW(menu, MF_STRING, CmdTransfer, dlLabel.c_str());
        } else {
            const bool canUpload = sftp->RemoteState() == SftpConnectionState::Connected;
            const std::wstring upLabel = L"📤  " + std::wstring(Tr(Msg::SftpMenuUpload));
            AppendMenuW(menu, MF_STRING | (canUpload ? 0 : MF_GRAYED), CmdTransfer, upLabel.c_str());
        }
        const std::wstring renLabel = L"✏️  " + std::wstring(Tr(Msg::SftpMenuRename));
        const std::wstring delLabel = L"🗑️  " + std::wstring(Tr(Msg::SftpMenuDelete));
        const std::wstring cpLabel = L"📋  " + std::wstring(Tr(Msg::SftpMenuCopyPath));
        AppendMenuW(menu, MF_STRING, CmdRename, renLabel.c_str());
        AppendMenuW(menu, MF_STRING, CmdDelete, delLabel.c_str());
        AppendMenuW(menu, MF_STRING, CmdCopyPath, cpLabel.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    const std::wstring newFolderLabel = L"📁  " + std::wstring(Tr(Msg::SftpMenuNewFolder));
    const std::wstring refreshLabel = L"🔄  " + std::wstring(Tr(Msg::SftpMenuRefresh));
    AppendMenuW(menu, MF_STRING, CmdNewFolder, newFolderLabel.c_str());
    AppendMenuW(menu, MF_STRING, CmdRefresh, refreshLabel.c_str());

    SetForegroundWindow(m_hwnd);
    const int cmd = (int)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                          screenPt.x, screenPt.y, m_hwnd, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case CmdOpen:
        if (isRemote) {
            OpenRemoteFile(selItem);
        } else {
            std::wstring fullPath = sftp->LocalPath();
            if (fullPath.back() != L'\\') fullPath += L'\\';
            fullPath += selItem;
            ShellExecuteW(nullptr, L"open", fullPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
    case CmdTransfer:
        if (isRemote) {
            std::wstring err;
            sftp->DownloadSelected(selItem, &err);
        } else {
            std::wstring err;
            sftp->UploadSelected(selItem, &err);
        }
        break;
    case CmdRename:
        m_sftpRenamePrompt = true;
        m_sftpRenameIsRemote = isRemote;
        m_sftpRenameOld = selItem;
        m_sftpRenameNew = selItem;
        m_ui.SetFocus(ID_SFTP_RENAME);
        m_dirty = true;
        break;
    case CmdDelete: {
        std::wstring err;
        if (isRemote) {
            sftp->DeleteRemoteItem(selItem, false, &err);
            m_sftpSelRemote.clear();
        } else {
            sftp->DeleteLocalItem(selItem, false, &err);
            m_sftpSelLocal.clear();
        }
        break;
    }
    case CmdCopyPath: {
        std::wstring p = isRemote ? (sftp->RemotePath() + L"/" + selItem) : (sftp->LocalPath() + L"\\" + selItem);
        ClipboardSetText(m_hwnd, p);
        Toast(L"Yol kopyalandı: " + selItem);
        break;
    }
    case CmdNewFolder:
        m_sftpNewFolderPrompt = true;
        m_sftpNewFolderIsRemote = isRemote;
        m_sftpNewFolderName.clear();
        m_ui.SetFocus(ID_SFTP_NEW_FOLDER);
        m_dirty = true;
        break;
    case CmdRefresh:
        if (isRemote) sftp->RefreshRemote();
        else sftp->RefreshLocal();
        break;
    default:
        break;
    }
}

void MainWindow::OnFilesDropped(const std::vector<std::wstring>& files, POINT pt) {
    if (files.empty()) return;

    SftpController* activeSftp = nullptr;
    if (m_view == View::Sftp) {
        activeSftp = m_sftp.get();
    } else if (m_view == View::Terminal && Active() && Active()->IsSftp()) {
        activeSftp = Active()->Sftp();
    }

    if (activeSftp) {
        if (activeSftp->RemoteState() == SftpConnectionState::Connected) {
            auto* fs = activeSftp->RemoteFs();
            if (!fs) return;
            std::wstring remoteDir = activeSftp->RemotePath();
            if (remoteDir.empty()) remoteDir = L"/";
            if (remoteDir.back() != L'/') remoteDir += L"/";

            Toast(L"Dosyalar sunucuya yükleniyor (" + std::to_wstring(files.size()) + L" adet)...");
            std::thread([this, files, remoteDir, fs, activeSftp] {
                size_t successCount = 0;
                for (const auto& localPath : files) {
                    size_t slash = localPath.rfind(L'\\');
                    std::wstring fname = (slash != std::wstring::npos) ? localPath.substr(slash + 1) : localPath;
                    std::wstring remoteTarget = remoteDir + fname;
                    std::wstring err;
                    if (fs->Upload(localPath, remoteTarget, &err)) {
                        successCount++;
                    }
                }
                PostMessageW(m_hwnd, WM_COMMAND, 0, 0);
                if (activeSftp) activeSftp->RefreshRemote();
            }).detach();
            return;
        } else {
            Toast(Tr(Msg::ToastNotConnected));
            return;
        }
    }

    // Kubernetes YAML dosyasi suruklendiyse otomatik ice aktar
    auto isYaml = [](const std::wstring& fname) {
        const size_t dot = fname.rfind(L'.');
        if (dot == std::wstring::npos) return false;
        std::wstring ext = fname.substr(dot);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        return ext == L".yaml" || ext == L".yml";
    };

    bool anyYaml = false;
    for (const auto& f : files) {
        if (isYaml(f)) { anyYaml = true; break; }
    }
    if (anyYaml) {
        size_t imported = 0;
        for (const auto& f : files) {
            if (isYaml(f)) {
                std::wstring ierr;
                if (K8sManager::Instance().ImportYamlFile(f, &ierr)) {
                    imported++;
                }
            }
        }
        if (imported > 0) {
            RefreshHubNodes();
            Toast(std::to_wstring(imported) + L" adet Kubernetes YAML / Kubeconfig dosyası içe aktarıldı.");
            m_dirty = true;
            return;
        }
    }

    if (m_view == View::Terminal) {
        if (auto* t = Active()) {
            std::string textToPaste;
            for (size_t i = 0; i < files.size(); ++i) {
                if (i > 0) textToPaste += " ";
                std::string u8 = WideToUtf8(files[i]);
                if (u8.find(' ') != std::string::npos) {
                    textToPaste += "\"" + u8 + "\"";
                } else {
                    textToPaste += u8;
                }
            }
            if (m_broadcastMode) {
                BroadcastInput(textToPaste);
            } else {
                t->Write(textToPaste);
            }
            m_dirty = true;
        }
    }
}

void MainWindow::DrawRenameTabModal() {
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const float W = (float)(rc.right - rc.left);
    const float H = (float)(rc.bottom - rc.top);
    const float s = m_lay.scale;

    D2D1_RECT_F fullClient = D2D1::RectF(0, 0, W, H);
    m_r.Fill(fullClient, 0x88000000);

    const float dw = std::floor(400 * s), dh = std::floor(150 * s);
    const float dx = std::floor((W - dw) * 0.5f);
    const float dy = std::floor((H - dh) * 0.5f);
    const D2D1_RECT_F dR = D2D1::RectF(dx, dy, dx + dw, dy + dh);

    // Diyalog disina tiklanirsa iptal edip modali kapat
    if (m_in.clicked && m_in.px >= 0.0f && !HitRect(dR, (int)m_in.px, (int)m_in.py) && !HitRect(dR, (int)m_in.mx, (int)m_in.my)) {
        m_renamingTab = false;
        m_ui.SetFocus(ID_NONE);
        m_dirty = true;
        return;
    }

    m_r.FillRound(dR, 8 * s, theme::Surface);
    m_r.Stroke(dR, theme::AcHi());

    m_r.Text(Tr(Msg::TabRenameTitle), D2D1::RectF(dx + 16 * s, dy + 14 * s, dx + dw - 16 * s, dy + 34 * s),
             theme::TextHi, 13.5f * s, Renderer::Align::Left, true);

    m_ui.Field(ID_TAB_RENAME, D2D1::RectF(dx + 16 * s, dy + 44 * s, dx + dw - 16 * s, dy + 76 * s),
               m_renameTabText, Tr(Msg::TabRenamePlaceholder));

    // İptal Butonu
    if (m_ui.Button(9880, D2D1::RectF(dx + dw - std::floor(180 * s), dy + std::floor(96 * s), dx + dw - std::floor(104 * s), dy + std::floor(128 * s)), Tr(Msg::ActionCancel))) {
        m_renamingTab = false;
        m_ui.SetFocus(ID_NONE);
        m_dirty = true;
        return;
    }

    // Sıfırla Butonu
    if (m_ui.Button(9881, D2D1::RectF(dx + 16 * s, dy + std::floor(96 * s), dx + std::floor(96 * s), dy + std::floor(128 * s)), Tr(Msg::ActionReset))) {
        if (m_renameTabIdx < m_tabs.size() && m_tabs[m_renameTabIdx]) {
            m_tabs[m_renameTabIdx]->SetCustomTitle(L"");
            Toast(Tr(Msg::ToastTabReset));
            SaveSession();
        }
        m_renamingTab = false;
        m_ui.SetFocus(ID_NONE);
        m_dirty = true;
        return;
    }

    // Kaydet Butonu
    if (m_ui.Button(9882, D2D1::RectF(dx + dw - std::floor(96 * s), dy + std::floor(96 * s), dx + dw - 16 * s, dy + std::floor(128 * s)), Tr(Msg::ActionSave), true)) {
        if (m_renameTabIdx < m_tabs.size() && m_tabs[m_renameTabIdx]) {
            m_tabs[m_renameTabIdx]->SetCustomTitle(m_renameTabText);
            Toast(Tr(Msg::ToastTabSaved));
            SaveSession();
        }
        m_renamingTab = false;
        m_ui.SetFocus(ID_NONE);
        m_dirty = true;
        return;
    }
}

// ----------------------------------------------------------------- tepsi ---

void MainWindow::AddTrayIcon() {
    if (!m_hwnd) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = m_icon ? m_icon : LoadIconW(nullptr, IDI_APPLICATION);
    if (m_cfg.quakeMode) {
        const std::wstring tip = L"FullTerminal - Visor (" +
            HotkeyName(m_cfg.quakeHotkeyVk, m_cfg.quakeHotkeyMods) + L")";
        wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    } else {
        wcscpy_s(nid.szTip, L"FullTerminal - arka planda calisiyor");
    }
    // Zaten varsa yalnizca ipucunu guncelle (Visor'a gecis, kisayol degisimi).
    if (m_trayVisible) { Shell_NotifyIconW(NIM_MODIFY, &nid); return; }
    m_trayVisible = (Shell_NotifyIconW(NIM_ADD, &nid) != FALSE);
}

void MainWindow::RemoveTrayIcon() {
    if (!m_trayVisible || !m_hwnd) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_trayVisible = false;
}

void MainWindow::HideToTray() {
    AddTrayIcon();
    ShowWindow(m_hwnd, SW_HIDE);
}

void MainWindow::RestoreFromTray() {
    if (InQuake()) { QuakeShow(); return; }
    ShowWindow(m_hwnd, SW_SHOW);
    if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
    ForceForeground();
    if (!m_cfg.minimizeToTray) RemoveTrayIcon();
    m_dirty = true;
}

void MainWindow::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    const bool quake = InQuake();
    AppendMenuW(menu, MF_STRING, TRAY_SHOW, Tr(Msg::TrayShow));
    if (quake) {
        AppendMenuW(menu, MF_STRING | (m_quakeState == QuakeState::Hidden ? MF_GRAYED : 0),
                    TRAY_HIDE, Tr(Msg::TrayHide));
    }
    AppendMenuW(menu, MF_STRING, TRAY_NEW, Tr(Msg::TrayNewSession));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (quake ? MF_CHECKED : 0), TRAY_QUAKE,
                quake ? Tr(Msg::TrayVisorToggleOff) : Tr(Msg::TrayVisorToggleOn));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TRAY_QUIT, Tr(Msg::TrayQuit));

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    const int cmd = (int)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                          pt.x, pt.y, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd) PostMessageW(m_hwnd, WM_COMMAND, (WPARAM)cmd, 0);
}

// ----------------------------------------------------------------- girdi ---

// alt: WM_SYSCHAR (sol Alt basili, Ctrl yok). AltGr (TR Q'da @ # [ ...)
// Ctrl+Alt olarak WM_CHAR ile gelir, oneksiz yazilir.
void MainWindow::OnChar(wchar_t ch, bool alt) {
    if (m_swallowChar) { m_swallowChar = false; return; }


    if (!TerminalHasKeyboard()) {
        if (alt) return;   // Alt+harf metin alanina yazilmasin
        m_in.chars.push_back(ch);
        m_dirty = true;
        return;
    }

    auto* t = Active();
    if (!t) return;
    if (t->Dead()) return;   // bitmis surece yazilmaz; Enter/Esc/Ctrl+D OnKeyDown'da kapatir

    // Meta: xterm/readline kurali, Alt+x = ESC x (Alt+b/f kelime atlama, Alt+. son arguman)
    const std::string pfx = alt ? std::string("\x1b") : std::string();

    auto sendInput = [this, t](const std::string& bytes) {
        if (m_broadcastMode) {
            BroadcastInput(bytes.data(), bytes.size());
        } else {
            t->Write(bytes);
            t->screen().ScrollViewToBottom();
        }
    };

    if (ch == L'\r') { sendInput(pfx + "\r"); m_dirty = true; return; }
    if (ch == L'\t') { sendInput(pfx + "\t"); return; }
    if (ch == 0x1B)  { sendInput(pfx + "\x1b"); return; }
    // Not: 0x08 burada yalnizca Ctrl+H'den gelir (Backspace OnKeyDown'da 0x7F
    // olarak gider); ^H oldugu gibi iletilir (emacs yardim oneki, stty erase).

    std::wstring w;
    if (ch >= 0xD800 && ch <= 0xDBFF) { m_highSurrogate = ch; m_highSurrogateAlt = alt; return; }
    bool meta = alt;
    if (ch >= 0xDC00 && ch <= 0xDFFF) {
        if (!m_highSurrogate) return;
        w.push_back(m_highSurrogate);
        w.push_back(ch);
        meta = m_highSurrogateAlt;   // cift icin tek onek
        m_highSurrogate = 0;
    } else {
        m_highSurrogate = 0;
        w.push_back(ch);
    }

    std::string utf8 = WideToUtf8(w);
    if (!utf8.empty()) {
        if (meta) utf8.insert(0, "\x1b");
        sendInput(utf8);
        m_dirty = true;
    }
}

void MainWindow::OnKeyDown(WPARAM vk, bool alt) {
    // Bayrak yalnizca BU tusun uretecegi WM_CHAR icin. Ok, F tuslari gibi
    // karakter uretmeyen bir tus bayragi acik birakirsa bir sonraki tusun
    // karakteri yutuluyordu ("exit" -> "xit").
    m_swallowChar = false;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const int mods = ModifierCode();

    if (m_renamingTab) {
        if (vk == VK_RETURN) {
            if (m_renameTabIdx < m_tabs.size() && m_tabs[m_renameTabIdx]) {
                m_tabs[m_renameTabIdx]->SetCustomTitle(m_renameTabText);
                Toast(Tr(Msg::ToastTabSaved));
                SaveSession();
            }
            m_renamingTab = false;
            m_ui.SetFocus(ID_NONE);
            m_dirty = true;
            return;
        }
        if (vk == VK_ESCAPE) {
            m_renamingTab = false;
            m_ui.SetFocus(ID_NONE);
            m_dirty = true;
            return;
        }
    }

    // Ayarlar > Guake > "Degistir": siradaki tus birlesimi yeni kisayol olur.
    if (m_captureHotkey) {
        m_swallowChar = true;
        if (IsModifierVk(vk)) return;           // asil tusu bekle
        m_captureHotkey = false;
        m_dirty = true;
        if (vk == VK_ESCAPE) {
            if (InQuake()) RegisterQuakeHotkey(false);
            Toast(L"Kisayol degistirilmedi");
            return;
        }
        int hm = 0;
        if (ctrl)  hm |= MOD_CONTROL;
        if (alt || (GetKeyState(VK_MENU) & 0x8000)) hm |= MOD_ALT;
        if (shift) hm |= MOD_SHIFT;
        if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) hm |= MOD_WIN;
        const bool fkey = (vk >= VK_F1 && vk <= VK_F24) || vk == VK_PAUSE || vk == VK_SCROLL;
        // Tek basina harf/rakam/bosluk kisayol olursa o tus sistem genelinde yazilamaz.
        if (hm == 0 && !fkey) {
            if (InQuake()) RegisterQuakeHotkey(false);
            Toast(L"Tek basina bu tus olmaz: Ctrl, Alt veya Win ile birlikte bas (ya da bir F tusu)");
            return;
        }
        const int oldVk = m_cfg.quakeHotkeyVk, oldMods = m_cfg.quakeHotkeyMods;
        m_cfg.quakeHotkeyVk = (int)vk;
        m_cfg.quakeHotkeyMods = hm;
        // Mod kapaliyken de musaitligi simdi dene: kullanici acinca surpriz olmasin.
        const bool ok = RegisterQuakeHotkey(true);
        if (!ok) {
            m_cfg.quakeHotkeyVk = oldVk;
            m_cfg.quakeHotkeyMods = oldMods;
        }
        if (InQuake()) { if (!ok) RegisterQuakeHotkey(false); }
        else UnregisterQuakeHotkey();
        m_cfg.Save(m_dataDir);
        return;
    }

    // Guake: F11 gecici tam yukseklik (Guake'deki tam ekran tusu)
    if (vk == VK_F11 && InQuake() && !ctrl && !alt && !shift) {
        m_swallowChar = true;
        PostQuake(QC_FULL);
        return;
    }

    if (ctrl && shift) {
        switch (vk) {
        case 'T': m_swallowChar = true; NewTab(DefaultProfileIndex()); return;
        case 'W':
            m_swallowChar = true;
            CloseActivePaneOrTab();
            return;
        case 'D':
            m_swallowChar = true;
            SplitActiveTab(ft::SplitDirection::Vertical);
            return;
        case 'E':
        case 'O':
            m_swallowChar = true;
            SplitActiveTab(ft::SplitDirection::Horizontal);
            return;
        case 'Z':
            m_swallowChar = true;
            TogglePaneZoom();
            return;
        case 'Y':
            if (ApproveFocusedAgent(true)) {
                m_swallowChar = true;
                return;
            }
            break;
        case 'N':
            if (ApproveFocusedAgent(false)) {
                m_swallowChar = true;
                return;
            }
            break;
        case 'P': {
            m_swallowChar = true;
            POINT pt{ static_cast<LONG>(m_lay.term.left + 40.0f * m_lay.scale),
                      static_cast<LONG>(m_lay.term.top + 40.0f * m_lay.scale) };
            ClientToScreen(m_hwnd, &pt);
            ShowSwarmMenu(pt);
            return;
        }
        case 'C':
            // Bir metin alani odakliysa kisayol alana ait; terminal calmasin.
            if (!TerminalHasKeyboard()) break;
            m_swallowChar = true; CopySelection(); return;
        case 'V':
            if (!TerminalHasKeyboard()) break;
            m_swallowChar = true; PasteClipboard(); return;
        case 'B': m_swallowChar = true; m_sidebarOpen = !m_sidebarOpen; ComputeLayout(); SyncGridToArea(); m_dirty = true; return;
        case 'H': m_swallowChar = true; SetView(View::Hosts); return;
        case 'S': m_swallowChar = true; SetView(View::Settings); return;
        case 'I':
        case 'M':
            m_swallowChar = true;
            ToggleBroadcastMode();
            return;
        case 'A':
            m_swallowChar = true;
            ToggleAgentFleetDrawer();
            return;
        case 'F':
            m_swallowChar = true;
            FixFailedCommandWithAgent();
            return;
        case 'R':
            m_swallowChar = true;
            ToggleSessionRecording();
            return;
        case VK_UP:
            if (Active() && Active()->screen().JumpToPreviousCommand()) {
                m_swallowChar = true;
                m_dirty = true;
                return;
            }
            break;
        case VK_DOWN:
            if (Active() && Active()->screen().JumpToNextCommand()) {
                m_swallowChar = true;
                m_dirty = true;
                return;
            }
            break;
        case 'Q': m_swallowChar = true; m_reallyQuit = true; PostMessageW(m_hwnd, WM_CLOSE, 0, 0); return;
        default: break;
        }
    }
    if (ctrl && alt) {
        if (vk == VK_LEFT || vk == VK_UP) {
            m_swallowChar = true;
            CyclePaneFocus(false);
            return;
        }
        if (vk == VK_RIGHT || vk == VK_DOWN) {
            m_swallowChar = true;
            CyclePaneFocus(true);
            return;
        }
    }
    if (ctrl && vk == VK_TAB) {
        m_swallowChar = true;
        if (!m_tabs.empty()) {
            const size_t n = m_tabs.size();
            SelectTab(shift ? (m_active + n - 1) % n : (m_active + 1) % n);
            SetView(View::Terminal);
        }
        return;
    }
    if (alt && vk >= '1' && vk <= '9') {
        // Ardindan gelen WM_SYSCHAR rakami yeni sekmeye yazmasin (sekme yoksa da).
        m_swallowChar = true;
        const size_t idx = (size_t)(vk - '1');
        if (idx < m_tabs.size()) { SelectTab(idx); SetView(View::Terminal); }
        return;
    }
    // Cekmece: Ctrl+Shift+M. Duz Ctrl+M terminalde CR (^M) demek, calinmamali.
    if (ctrl && shift && vk == 'M') {
        m_swallowChar = true;
        ToggleSlideDrawer();
        return;
    }
    if (vk == VK_ESCAPE && m_agentFleetOpen) {
        m_swallowChar = true;
        m_agentFleetOpen = false;
        m_dirty = true;
        return;
    }
    if (vk == VK_ESCAPE && m_drawerOpen) {
        m_swallowChar = true;
        ToggleSlideDrawer();
        return;
    }
    // Not: Guake kisayolu RegisterHotKey ile global; kayitliyken WM_KEYDOWN
    // olarak hic gelmez. Mod kapaliyken ayni tus (F12) terminale gitmeli.

    if (vk == VK_ESCAPE && m_ui.Focus() != ID_NONE) {
        m_ui.SetFocus(ID_NONE);
        m_dirty = true;
        return;
    }

    if (!TerminalHasKeyboard()) {
        if (vk == VK_RETURN && m_ui.Focus() == ID_QUICK) {
            m_swallowChar = true;
            QuickConnect(m_quick);
            return;
        }
        // Degistiriciler tusla birlikte saklanir: cizim aninda GetKeyState
        // okumak yaris yaratirdi.
        m_in.keys.push_back(UiKey{ (int)vk, ctrl, shift });
        m_dirty = true;
        return;
    }

    if (shift && vk == VK_INSERT) { PasteClipboard(); return; }
    // Alternatif ekranda scrollback yok: ana ekran gecmisi uygulamanin
    // ustune binmesin, tus uygulamaya gitsin (asagidaki CSI 5;2~ / 6;2~).
    const bool altScreen = Active() && Active()->screen().IsAltBuffer();
    if (shift && vk == VK_PRIOR && !altScreen) { if (auto* t = Active()) { t->screen().ScrollViewLines(t->screen().Rows() - 1); m_dirty = true; } return; }
    if (shift && vk == VK_NEXT && !altScreen)  { if (auto* t = Active()) { t->screen().ScrollViewLines(-(t->screen().Rows() - 1)); m_dirty = true; } return; }

    auto* t = Active();
    if (!t) return;

    // Hatayla biten surec (ssh baglanamadi vb.): sekme mesaji okunabilsin diye
    // acik kalir; Enter/Esc/Ctrl+D kapatir, baska tus PTY'ye gitmez.
    if (t->Dead()) {
        if (vk == VK_RETURN || vk == VK_ESCAPE || (ctrl && !shift && vk == 'D')) {
            m_swallowChar = true;
            CloseTab(m_active);
        }
        return;
    }

    const bool app = t->screen().AppCursorKeys();

    std::string seq;
    switch (vk) {
    case VK_UP:    seq = CsiFinal('A', mods, app); break;
    case VK_DOWN:  seq = CsiFinal('B', mods, app); break;
    case VK_RIGHT: seq = CsiFinal('C', mods, app); break;
    case VK_LEFT:  seq = CsiFinal('D', mods, app); break;
    case VK_HOME:  seq = CsiFinal('H', mods, false); break;
    case VK_END:   seq = CsiFinal('F', mods, false); break;
    case VK_PRIOR: seq = CsiTilde(5, mods); break;
    case VK_NEXT:  seq = CsiTilde(6, mods); break;
    case VK_INSERT:seq = CsiTilde(2, mods); break;
    case VK_DELETE:seq = CsiTilde(3, mods); break;
    case VK_BACK:
        seq = ctrl ? std::string("\x08") : std::string("\x7f");
        if (alt) seq.insert(0, "\x1b");   // Alt+Backspace: kelime sil (Meta-DEL)
        break;
    case VK_TAB:   if (shift) seq = "\x1b[Z"; break;
    case VK_SPACE:
        // Ctrl+Space = NUL (PSReadLine MenuComplete, emacs set-mark).
        // AltGr (Ctrl+Alt) bosluk uretiyorsa dokunulmaz.
        if (ctrl && !alt && !(GetKeyState(VK_MENU) & 0x8000)) seq.assign(1, '\0');
        break;
    case VK_F1: case VK_F2: case VK_F3: case VK_F4: {
        const char f = (char)('P' + (vk - VK_F1));
        seq = (mods <= 1) ? std::string("\x1bO") + f : "\x1b[1;" + std::to_string(mods) + f;
        break;
    }
    case VK_F5:  seq = CsiTilde(15, mods); break;
    case VK_F6:  seq = CsiTilde(17, mods); break;
    case VK_F7:  seq = CsiTilde(18, mods); break;
    case VK_F8:  seq = CsiTilde(19, mods); break;
    case VK_F9:  seq = CsiTilde(20, mods); break;
    case VK_F10: seq = CsiTilde(21, mods); break;
    case VK_F11: seq = CsiTilde(23, mods); break;
    case VK_F12: seq = CsiTilde(24, mods); break;
    default: break;
    }

    if (!seq.empty()) {
        m_swallowChar = true;
        if (m_broadcastMode) {
            BroadcastInput(seq.data(), seq.size());
        } else {
            t->Write(seq);
            t->screen().ScrollViewToBottom();
        }
        m_dirty = true;
    }
}

// ------------------------------------------------------------------ secim --

bool MainWindow::CellFromPoint(int px, int py, int& col, int& row) const {
    if (!m_r.Ready()) return false;
    const D2D1_RECT_F a = GetFocusedPaneArea();
    if (px < a.left || px >= a.right || py < a.top || py >= a.bottom) return false;
    const FontMetrics& fm = m_r.Metrics();
    col = (int)((px - a.left) / fm.cellW);
    row = (int)((py - a.top) / fm.cellH);
    return true;
}

void MainWindow::CellFromPointClamped(int px, int py, int& col, int& row) const {
    // Yakalama altinda pencere disina surukleme: en yakin hucreye kirpilir.
    col = row = 0;
    if (!m_r.Ready()) return;
    const D2D1_RECT_F a = GetFocusedPaneArea();
    const FontMetrics& fm = m_r.Metrics();
    const TerminalTab* t = (m_active < m_tabs.size()) ? m_tabs[m_active].get() : nullptr;
    const int cols = t ? t->screen().Cols() : 1;
    const int rows = t ? t->screen().Rows() : 1;
    col = std::clamp((int)std::floor((px - a.left) / fm.cellW), 0, std::max(0, cols - 1));
    row = std::clamp((int)std::floor((py - a.top) / fm.cellH), 0, std::max(0, rows - 1));
}

void MainWindow::ClearSelection() {
    if (m_sel.active) m_dirty = true;
    m_sel = AbsSelection{};
}

// Mutlak satirlardaki secim, cizim icin o anki gorunume cevrilir; gorunumun
// disinda kalan uclar kenara kirpilir.
Selection MainWindow::ViewSelection() {
    Selection v;
    auto* t = Active();
    if (!t || !m_sel.active || m_sel.alt != t->screen().IsAltBuffer()) return v;
    const Screen& sc = t->screen();

    int x0 = m_sel.x0, x1 = m_sel.x1;
    int64_t y0 = m_sel.y0, y1 = m_sel.y1;
    if (y1 < y0 || (y1 == y0 && x1 < x0)) { std::swap(x0, x1); std::swap(y0, y1); }

    int r0 = sc.AbsToViewRow(y0);
    int r1 = sc.AbsToViewRow(y1);
    const int rows = sc.Rows();
    if (r1 < 0 || r0 >= rows) return v;
    if (r0 < 0)     { r0 = 0; x0 = 0; }
    if (r1 >= rows) { r1 = rows - 1; x1 = sc.Cols() - 1; }

    v.active = true;
    v.x0 = x0; v.y0 = r0;
    v.x1 = x1; v.y1 = r1;
    return v;
}

void MainWindow::CopySelection() {
    auto* t = Active();
    if (!t || !m_sel.active || m_sel.alt != t->screen().IsAltBuffer()) return;
    int x0 = m_sel.x0, x1 = m_sel.x1;
    int64_t y0 = m_sel.y0, y1 = m_sel.y1;
    if (y1 < y0 || (y1 == y0 && x1 < x0)) { std::swap(x0, x1); std::swap(y0, y1); }
    const std::string text = t->screen().TextInAbsRange(x0, y0, x1, y1);
    if (text.empty()) return;

    if (ClipboardSetText(m_hwnd, Utf8ToWide(text))) Toast(L"Kopyalandi");
}

void MainWindow::PasteClipboard() {
    auto* t = Active();
    if (!t || t->Dead()) return;
    const std::wstring w = ClipboardGetText(m_hwnd);
    if (w.empty()) return;

    // Pano guvenilmez: ESC/C0/C1 kontrolleri atilir. Aksi halde gomulu bir
    // "ESC[201~" bracketed paste'i erken bitirip gerisini yazilmis gibi
    // calistirirdi; U+009B tek baytlik CSI'dir. Satir sonlari CR'ye esitlenir.
    std::wstring f;
    f.reserve(w.size());
    bool stripped = false;
    for (size_t i = 0; i < w.size(); ++i) {
        const wchar_t c = w[i];
        if (c == L'\r') {
            f.push_back(L'\r');
            if (i + 1 < w.size() && w[i + 1] == L'\n') ++i;
        } else if (c == L'\n') {
            f.push_back(L'\r');
        } else if (c == L'\t') {
            f.push_back(c);
        } else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F)) {
            stripped = true;
        } else {
            f.push_back(c);
        }
    }
    if (f.empty()) return;

    // Tek bir CR bile komutu calistirir: yalniz LF degil, her satir sonu uyarir.
    const size_t breaks = (size_t)std::count(f.begin(), f.end(), L'\r');
    if (m_cfg.pasteGuard && breaks > 0) {
        const int lines = (int)breaks + (f.back() == L'\r' ? 0 : 1);
        wchar_t msg[256];
        swprintf_s(msg, L"%d satirlik metin yapistirilacak; satir sonlari komutlari hemen "
                        L"calistirir. Devam edilsin mi?", lines);
        if (MessageBoxW(m_hwnd, msg, L"Yapistirma korumasi",
                        MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2) != IDOK) {
            return;
        }
    }

    const std::string clean = WideToUtf8(f);
    if (clean.empty()) return;

    auto writePaste = [this, t](const std::string& str) {
        if (m_broadcastMode) {
            BroadcastInput(str.data(), str.size());
        } else {
            t->Write(str);
            t->screen().ScrollViewToBottom();
        }
    };

    if (t->screen().BracketedPaste()) {
        writePaste("\x1b[200~" + clean + "\x1b[201~");
    } else {
        writePaste(clean);
    }
    if (stripped) Toast(L"Yapistirilan metinden kontrol karakterleri temizlendi");
    m_dirty = true;
}

// ------------------------------------------------------------ fare raporu --

bool MainWindow::MouseReporting(bool shiftHeld) {
    if (shiftHeld || m_view != View::Terminal || DrawerUp()) return false;
    auto* t = Active();
    return t && !t->Dead() && t->screen().MouseMode() != 0;
}

// button: 0 sol, 1 orta, 2 sag, 3 dugmesiz hareket, 64/65 tekerlek.
// col/row 0 tabanli hucre.
void MainWindow::SendMouseReport(int button, bool press, bool motion, int col, int row) {
    auto* t = Active();
    if (!t) return;
    t->screen().ScrollViewToBottom();   // gorunum satirlari ekran satirlariyla ayni olsun

    int b = button;
    if (motion) b += 32;
    if (GetKeyState(VK_MENU) & 0x8000)    b += 8;
    if (GetKeyState(VK_CONTROL) & 0x8000) b += 16;
    const int x = col + 1, y = row + 1;

    std::string seq;
    if (t->screen().MouseSgr()) {
        seq = "\x1b[<" + std::to_string(b) + ";" + std::to_string(x) + ";" +
              std::to_string(y) + (press ? "M" : "m");
    } else {
        // X10: birakma dugmesi 3. ConPTY girdisi UTF-8: 0x7F ustu tek bayt
        // gecersiz dizi olur (U+FFFD), bu yuzden Windows Terminal gibi 95'te kes.
        if (!press) b = (b & ~3) | 3;
        if (x > 95 || y > 95) return;
        seq = "\x1b[M";
        seq.push_back((char)(32 + std::min(b, 223)));
        seq.push_back((char)(32 + x));
        seq.push_back((char)(32 + y));
    }
    t->Write(seq);
    m_mouseCol = col;
    m_mouseRow = row;
}

// Tekerlek: fare modu -> uygulamaya 64/65; alternatif ekran (less, vim,
// htop) -> ok tuslari; aksi halde scrollback. Ctrl+tekerlek yazi boyutu.
void MainWindow::SendWheel(int delta, bool shiftHeld, bool ctrlHeld, POINT pt) {
    auto* t = Active();
    if (t && t->GetSshStage() == SshStage::Failed) {
        const float step = (float)delta * 30.0f / (float)WHEEL_DELTA * m_lay.scale;
        m_sshLogScroll = std::max(0.0f, (m_sshLogScroll < 0.0f ? 0.0f : m_sshLogScroll) - step);
        m_dirty = true;
        return;
    }
    int target = 1;                                   // scrollback
    if (ctrlHeld) target = 2;                         // yakinlastirma
    else if (t && MouseReporting(shiftHeld)) target = 3;
    else if (t && t->screen().IsAltBuffer()) target = 4;
    if (target != m_wheelTarget || (m_wheelAccum > 0) != (delta > 0)) m_wheelAccum = 0;
    m_wheelTarget = target;

    UINT lpn = 3;                                     // satir / centik
    if (target == 1 || target == 4) {
        UINT sys = 3;
        if (SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &sys, 0)) lpn = sys;
        if (lpn == WHEEL_PAGESCROLL) lpn = (UINT)std::max(1, t ? t->screen().Rows() - 1 : 1);
        if (lpn == 0) return;
    } else {
        lpn = 1;                                      // rapor ve yazi boyutu: centik basina bir
    }

    m_wheelAccum += delta * (int)lpn;
    const int steps = m_wheelAccum / WHEEL_DELTA;     // isaretli
    m_wheelAccum -= steps * WHEEL_DELTA;
    if (steps == 0) return;

    if (target == 2) {
        const float pt0 = m_cfg.fontPt;
        m_cfg.fontPt = std::clamp(m_cfg.fontPt + (float)steps, 6.0f, 32.0f);
        if (m_cfg.fontPt != pt0) {
            m_r.SetFontSizePt(m_cfg.fontPt);
            SyncGridToArea();
        }
        return;
    }
    if (!t) return;
    if (target == 4 && t->Dead()) return;             // bitmis surece tus yazilmaz
    if (target == 3) {
        int col, row;
        CellFromPointClamped(pt.x, pt.y, col, row);
        const int n = std::min(std::abs(steps), 10);
        for (int i = 0; i < n; ++i) SendMouseReport(steps > 0 ? 64 : 65, true, false, col, row);
        return;
    }
    if (target == 4) {
        const bool app = t->screen().AppCursorKeys();
        const char* key = (steps > 0) ? (app ? "\x1bOA" : "\x1b[A") : (app ? "\x1bOB" : "\x1b[B");
        std::string seq;
        const int n = std::min(std::abs(steps), 60);
        for (int i = 0; i < n; ++i) seq += key;
        t->Write(seq);
        return;
    }
    t->screen().ScrollViewLines(steps);
    if (m_selecting) {
        // Surukleyerek secerken kaydirma: secimin ucu imlecin yeni satirina.
        int col, row;
        CellFromPointClamped(pt.x, pt.y, col, row);
        m_sel.x1 = col;
        m_sel.y1 = t->screen().ViewRowToAbs(row);
    }
}

// ------------------------------------------------------------ hit testing --

bool MainWindow::HitRect(const D2D1_RECT_F& r, int px, int py) const {
    return px >= r.left && px < r.right && py >= r.top && py < r.bottom;
}

// Gorunur indeksi model indeksine cevirir. Dikeyde tum baslik cubugu,

} // namespace ft
