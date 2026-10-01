#include "ui/MainWindowInternal.h"

#if !defined(_WIN32)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <GL/glx.h>
#include <poll.h>
#include <iostream>

#ifdef None
#undef None
#endif
#ifdef Status
#undef Status
#endif
#ifdef Success
#undef Success
#endif
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif

namespace ft {

bool MainWindow::Create(HINSTANCE /*inst*/, std::wstring* err) {
    if (!PosixPty::Available()) {
        if (err) *err = L"Linux POSIX PTY kullanilamiyor.";
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
    theme::SetAccent(Accent());

    m_profiles = DiscoverShellProfiles();
    if (m_profiles.empty()) {
        if (err) *err = L"Sistemde hicbir kabuk profili bulunamadi.";
        return false;
    }

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        if (err) *err = L"X11 Display baslatilamadi (DISPLAY degiskeni tanimli mi?).";
        return false;
    }
    int screen = DefaultScreen(dpy);

    int glxAttribs[] = {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        0
    };
    XVisualInfo* vi = glXChooseVisual(dpy, screen, glxAttribs);
    if (!vi) {
        XCloseDisplay(dpy);
        if (err) *err = L"GLX Visual destegi bulunamadi.";
        return false;
    }

    Colormap cmap = XCreateColormap(dpy, RootWindow(dpy, screen), vi->visual, AllocNone);
    XSetWindowAttributes swa{};
    swa.colormap = cmap;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask | FocusChangeMask;

    const UINT dpi = 96;
    m_dpi = dpi;
    const float s = dpi / 96.0f;
    int initW = (int)(1320 * s);
    int initH = (int)(800 * s);

    Window xwin = XCreateWindow(dpy, RootWindow(dpy, screen), 50, 50, initW, initH, 0,
                                vi->depth, InputOutput, vi->visual,
                                CWColormap | CWEventMask, &swa);

    XStoreName(dpy, xwin, "FullTerminal");
    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", 0);
    XSetWMProtocols(dpy, xwin, &wmDelete, 1);

    // Register Quake global hotkey via XGrabKey on Root window
    KeyCode f12Code = XKeysymToKeycode(dpy, XK_F12);
    if (f12Code) {
        XGrabKey(dpy, f12Code, AnyModifier, RootWindow(dpy, screen), 1, GrabModeAsync, GrabModeAsync);
    }

    auto* handle = new LinuxWindowHandle();
    handle->display = dpy;
    handle->window = xwin;
    handle->screen = screen;
    m_hwnd = reinterpret_cast<HWND>(handle);
    m_ui.SetOwner(m_hwnd);

    if (!m_r.Init(m_hwnd, m_dpi, err)) {
        XDestroyWindow(dpy, xwin);
        XCloseDisplay(dpy);
        delete handle;
        m_hwnd = nullptr;
        return false;
    }
    m_r.Resize(initW, initH);
    OnResize();
    if (!m_cfg.fontFamily.empty()) m_r.SetFontFamily(m_cfg.fontFamily);
    m_r.SetFontSizePt(m_cfg.fontPt);

    ComputeLayout();
    RefreshHubNodes();

    if (!RestoreSession()) {
        NewTab(DefaultProfileIndex());
    }
    m_fpsStart = NowTicks();

    if (m_cfg.quakeMode) {
        m_cfg.quakeMode = false;
        EnterQuakeMode();
    }
    return true;
}

void MainWindow::Show(int /*nCmdShow*/) {
    if (InQuake() || !m_hwnd) return;
    auto* h = reinterpret_cast<LinuxWindowHandle*>(m_hwnd);
    Display* dpy = reinterpret_cast<Display*>(h->display);
    Window win = static_cast<Window>(h->window);
    XMapRaised(dpy, win);
    XFlush(dpy);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wp, LPARAM /*lp*/) {
    switch (msg) {
    case WM_PTY_DATA:
        m_dirty = true;
        return 0;
    case WM_QUAKE_CMD:
        switch (static_cast<QuakeCmd>(wp)) {
        case QC_HIDE: QuakeHide(); break;
        case QC_FULL: m_quakeFull = !m_quakeFull; QuakeReapply(); break;
        case QC_REAPPLY: QuakeReapply(); break;
        case QC_ENTER: EnterQuakeMode(); break;
        case QC_EXIT: ExitQuakeMode(); break;
        }
        return 0;
    case WM_CLOSE:
        SaveSession();
        m_tabs.clear();
        m_r.Shutdown();
        m_hwnd = nullptr;
        return 0;
    default:
        break;
    }
    return 0;
}

static WPARAM KeySymToVk(KeySym sym) {
    switch (sym) {
    case XK_BackSpace:   return VK_BACK;
    case XK_Tab:         return VK_TAB;
    case XK_Return:
    case XK_KP_Enter:    return VK_RETURN;
    case XK_Escape:      return VK_ESCAPE;
    case XK_space:       return VK_SPACE;
    case XK_Prior:
    case XK_KP_Prior:    return VK_PRIOR;
    case XK_Next:
    case XK_KP_Next:     return VK_NEXT;
    case XK_End:
    case XK_KP_End:      return VK_END;
    case XK_Home:
    case XK_KP_Home:     return VK_HOME;
    case XK_Left:
    case XK_KP_Left:     return VK_LEFT;
    case XK_Up:
    case XK_KP_Up:       return VK_UP;
    case XK_Right:
    case XK_KP_Right:    return VK_RIGHT;
    case XK_Down:
    case XK_KP_Down:     return VK_DOWN;
    case XK_Insert:
    case XK_KP_Insert:   return VK_INSERT;
    case XK_Delete:
    case XK_KP_Delete:   return VK_DELETE;
    case XK_F1:          return VK_F1;
    case XK_F2:          return VK_F2;
    case XK_F3:          return VK_F3;
    case XK_F4:          return VK_F4;
    case XK_F5:          return VK_F5;
    case XK_F6:          return VK_F6;
    case XK_F7:          return VK_F7;
    case XK_F8:          return VK_F8;
    case XK_F9:          return VK_F9;
    case XK_F10:         return VK_F10;
    case XK_F11:         return VK_F11;
    case XK_F12:         return VK_F12;
    default:
        if (sym >= XK_a && sym <= XK_z) return (WPARAM)('A' + (sym - XK_a));
        if (sym >= XK_A && sym <= XK_Z) return (WPARAM)sym;
        if (sym >= XK_0 && sym <= XK_9) return (WPARAM)sym;
        if (sym >= XK_KP_0 && sym <= XK_KP_9) return (WPARAM)(VK_NUMPAD0 + (sym - XK_KP_0));
        break;
    }
    return 0;
}

void MainWindow::RunLinuxLoop() {
    auto* h = reinterpret_cast<LinuxWindowHandle*>(m_hwnd);
    if (!h || !h->display) return;
    Display* dpy = reinterpret_cast<Display*>(h->display);
    Window xwin = static_cast<Window>(h->window);
    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", 0);

    int x11_fd = ConnectionNumber(dpy);
    bool running = true;
    auto lastBlink = std::chrono::steady_clock::now();

    while (running && m_hwnd != nullptr) {
        // Pop any queued messages from background threads
        LinuxMsg qmsg;
        while (LinuxPopMessage(qmsg)) {
            WndProc(qmsg.msg, qmsg.wp, qmsg.lp);
        }
        if (!m_hwnd) break;

        // Drain X11 events
        while (XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);

            // Modifiers
            if (ev.type == KeyPress || ev.type == KeyRelease ||
                ev.type == ButtonPress || ev.type == ButtonRelease ||
                ev.type == MotionNotify) {
                int mods = 0;
                unsigned int st = (ev.type == MotionNotify) ? ev.xmotion.state :
                                  (ev.type == KeyPress || ev.type == KeyRelease) ? ev.xkey.state :
                                  ev.xbutton.state;
                if (st & ControlMask) mods |= MOD_CONTROL;
                if (st & ShiftMask)   mods |= MOD_SHIFT;
                if (st & Mod1Mask)    mods |= MOD_ALT;
                if (st & Mod4Mask)    mods |= MOD_WIN;
                LinuxSetModifiers(mods);
            }

            switch (ev.type) {
            case Expose:
                Render();
                break;

            case ConfigureNotify:
                if (ev.xconfigure.width != (int)m_r.Width() || ev.xconfigure.height != (int)m_r.Height()) {
                    m_r.Resize(ev.xconfigure.width, ev.xconfigure.height);
                    OnResize();
                    m_dirty = true;
                }
                break;

            case FocusIn:
                m_focused = true;
                m_dirty = true;
                break;

            case FocusOut:
                m_focused = false;
                m_dirty = true;
                break;

            case KeyPress: {
                char buf[64]{};
                KeySym sym = 0;
                int len = XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &sym, nullptr);
                if (len > 0) buf[len] = '\0';

                // Quake hotkey check (F12)
                if (sym == XK_F12) {
                    QuakeToggle();
                    break;
                }

                WPARAM vk = KeySymToVk(sym);
                const bool alt = (ev.xkey.state & Mod1Mask) != 0;

                if (vk != 0) {
                    OnKeyDown(vk, alt);
                }

                // If text input
                if (!m_swallowChar && len > 0) {
                    wchar_t wch[16]{};
                    int wlen = mbstowcs(wch, buf, 15);
                    for (int i = 0; i < wlen; ++i) {
                        OnChar(wch[i], alt);
                    }
                }
                break;
            }

            case ButtonPress: {
                int mx = ev.xbutton.x;
                int my = ev.xbutton.y;
                m_in.mx = (float)mx;
                m_in.my = (float)my;

                if (ev.xbutton.button == 4) {
                    // Wheel Up
                    SendWheel(120, false, false, POINT{ mx, my });
                    m_in.wheel = 120;
                } else if (ev.xbutton.button == 5) {
                    // Wheel Down
                    SendWheel(-120, false, false, POINT{ mx, my });
                    m_in.wheel = -120;
                } else if (ev.xbutton.button == 1) {
                    // Left button
                    m_in.px = (float)mx;
                    m_in.py = (float)my;
                    m_in.down = true;
                    m_in.clicked = true;

                    int col = 0, row = 0;
                    if (m_view == View::Terminal && CellFromPoint(mx, my, col, row)) {
                        if (auto* tab = Active()) {
                            m_selecting = true;
                            m_sel.active = true;
                            m_sel.alt = tab->screen().IsAltBuffer();
                            m_sel.x0 = m_sel.x1 = col;
                            m_sel.y0 = m_sel.y1 = tab->screen().ViewRowToAbs(row);
                        }
                    }
                } else if (ev.xbutton.button == 2) {
                    // Middle button -> paste
                    if (m_view == View::Terminal) PasteClipboard();
                } else if (ev.xbutton.button == 3) {
                    // Right button -> context menu
                    ShowTerminalContextMenu(POINT{ mx, my });
                }
                m_dirty = true;
                break;
            }

            case ButtonRelease: {
                int mx = ev.xbutton.x;
                int my = ev.xbutton.y;
                m_in.mx = (float)mx;
                m_in.my = (float)my;

                if (ev.xbutton.button == 1) {
                    m_in.down = false;
                    if (m_selecting) {
                        m_selecting = false;
                        if (m_sel.x0 == m_sel.x1 && m_sel.y0 == m_sel.y1) ClearSelection();
                        else if (m_cfg.copyOnSelect) CopySelection();
                    }
                }
                m_dirty = true;
                break;
            }

            case MotionNotify: {
                int mx = ev.xmotion.x;
                int my = ev.xmotion.y;
                m_in.mx = (float)mx;
                m_in.my = (float)my;

                if (m_selecting && m_view == View::Terminal) {
                    int col = 0, row = 0;
                    CellFromPointClamped(mx, my, col, row);
                    if (auto* tab = Active()) {
                        m_sel.x1 = col;
                        m_sel.y1 = tab->screen().ViewRowToAbs(row);
                        m_dirty = true;
                    }
                }
                break;
            }

            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == wmDelete) {
                    SaveSession();
                    running = false;
                }
                break;

            default:
                break;
            }
        }

        // Caret blink timer simulation
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBlink).count() >= (int64_t)kBlinkMs) {
            lastBlink = now;
            m_cursorOn = m_cfg.cursorBlink ? !m_cursorOn : true;
            m_ui.SetCaretVisible(m_cursorOn);
            m_dirty = true;
        }

        bool drawn = Tick();

        struct pollfd pfd;
        pfd.fd = x11_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll(&pfd, 1, drawn ? 10 : 16);
    }

    if (m_hwnd) {
        m_tabs.clear();
        m_r.Shutdown();
        auto* lh = reinterpret_cast<LinuxWindowHandle*>(m_hwnd);
        if (lh) {
            if (lh->display && lh->window) XDestroyWindow(reinterpret_cast<Display*>(lh->display), static_cast<Window>(lh->window));
            if (lh->display) XCloseDisplay(reinterpret_cast<Display*>(lh->display));
            delete lh;
        }
        m_hwnd = nullptr;
    }
}

} // namespace ft

#endif // !defined(_WIN32)
