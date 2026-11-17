#include "ui/MainWindowInternal.h"

#if !defined(_WIN32)
#include "platform/linux/LinuxNative.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <GL/glx.h>
#include <poll.h>
#include <unistd.h>
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

namespace {

constexpr UINT kWmHotkey = 0x0312;        // WM_HOTKEY
constexpr UINT kWmLButtonUp = 0x0202;
constexpr UINT kWmLButtonDblClk = 0x0203;
constexpr UINT kWmRButtonUp = 0x0205;

Display* Dpy(HWND h) { return h ? static_cast<Display*>(reinterpret_cast<LinuxWindowHandle*>(h)->display) : nullptr; }
Window XWin(HWND h) { return h ? static_cast<Window>(reinterpret_cast<LinuxWindowHandle*>(h)->window) : 0; }

// ------------------------------------------------------------ XDND (v5) --
struct XdndAtoms {
    Atom aware = 0, enter = 0, position = 0, status = 0, leave = 0, drop = 0, finished = 0;
    Atom selection = 0, actionCopy = 0, typeList = 0, uriList = 0, prop = 0;
};
XdndAtoms s_xa;

struct XdndState {
    Window source = 0;
    long version = 0;
    bool accept = false;
    POINT pt{};
};
XdndState s_dnd;

void InitXdndAtoms(Display* d) {
    if (s_xa.aware) return;
    auto A = [d](const char* n) { return XInternAtom(d, n, 0); };
    s_xa.aware = A("XdndAware");
    s_xa.enter = A("XdndEnter");
    s_xa.position = A("XdndPosition");
    s_xa.status = A("XdndStatus");
    s_xa.leave = A("XdndLeave");
    s_xa.drop = A("XdndDrop");
    s_xa.finished = A("XdndFinished");
    s_xa.selection = A("XdndSelection");
    s_xa.actionCopy = A("XdndActionCopy");
    s_xa.typeList = A("XdndTypeList");
    s_xa.uriList = A("text/uri-list");
    s_xa.prop = A("FT_XDND_DATA");
}

void SendXdnd(Display* d, Window target, Atom type, long l0, long l1, long l2, long l3, long l4) {
    XEvent e{};
    e.xclient.type = ClientMessage;
    e.xclient.display = d;
    e.xclient.window = target;
    e.xclient.message_type = type;
    e.xclient.format = 32;
    e.xclient.data.l[0] = l0;
    e.xclient.data.l[1] = l1;
    e.xclient.data.l[2] = l2;
    e.xclient.data.l[3] = l3;
    e.xclient.data.l[4] = l4;
    XSendEvent(d, target, 0, NoEventMask, &e);
    XFlush(d);
}

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<std::wstring> ParseUriList(const std::string& data) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        std::string line = data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? data.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\0')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::string path;
        if (line.rfind("file://", 0) == 0) {
            std::string rest = line.substr(7);
            if (!rest.empty() && rest[0] != '/') {   // file://host/yol
                const size_t sl = rest.find('/');
                if (sl == std::string::npos) continue;
                rest = rest.substr(sl);
            }
            for (size_t i = 0; i < rest.size(); ++i) {
                if (rest[i] == '%' && i + 2 < rest.size() && HexNibble(rest[i + 1]) >= 0 && HexNibble(rest[i + 2]) >= 0) {
                    path += (char)(HexNibble(rest[i + 1]) * 16 + HexNibble(rest[i + 2]));
                    i += 2;
                } else {
                    path += rest[i];
                }
            }
        } else if (line[0] == '/') {
            path = line;
        } else {
            continue;   // yerel olmayan URI (http, sftp ...): Win32 de yalniz dosya yolu verir
        }
        if (!path.empty()) out.push_back(Utf8ToWide(path));
    }
    return out;
}

Cursor s_cursors[11] = {};
RECT s_lastResizeRect{};

} // namespace

bool MainWindow::Create(HINSTANCE /*inst*/, std::wstring* err) {
    lnx::InstallHooks();

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
    theme::SetAccent(Accent());   // simge asagida bu renkle uretiliyor

    m_profiles = DiscoverShellProfiles();
    if (m_profiles.empty()) {
        if (err) *err = L"Sistemde hicbir kabuk profili bulunamadi.";
        return false;
    }

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        if (err) *err = L"X11 ekranina baglanilamadi (DISPLAY degiskeni tanimli mi?).";
        return false;
    }
    int screen = DefaultScreen(dpy);

    // 32-bit ARGB GLX FBConfig secimi: Mutter / KWin / Picom gercek pencere saydamligi icin depth=32 sarttir
    XVisualInfo* vi = nullptr;
    {
        const int fbcAttribs[] = {
            GLX_X_RENDERABLE, 1,
            GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
            GLX_RENDER_TYPE, GLX_RGBA_BIT,
            GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
            GLX_RED_SIZE, 8,
            GLX_GREEN_SIZE, 8,
            GLX_BLUE_SIZE, 8,
            GLX_ALPHA_SIZE, 8,
            GLX_DOUBLEBUFFER, 1,
            0
        };
        int fbCount = 0;
        GLXFBConfig* fbcs = glXChooseFBConfig(dpy, screen, fbcAttribs, &fbCount);
        if (fbcs) {
            for (int i = 0; i < fbCount; ++i) {
                XVisualInfo* cand = glXGetVisualFromFBConfig(dpy, fbcs[i]);
                if (!cand) continue;
                if (cand->depth == 32 && cand->red_mask == 0xff0000 && cand->green_mask == 0x00ff00 && cand->blue_mask == 0x0000ff) {
                    vi = cand;
                    break;
                }
                XFree(cand);
            }
            if (!vi) {
                for (int i = 0; i < fbCount; ++i) {
                    XVisualInfo* cand = glXGetVisualFromFBConfig(dpy, fbcs[i]);
                    if (cand && cand->depth == 32) {
                        vi = cand;
                        break;
                    }
                    if (cand) XFree(cand);
                }
            }
            XFree(fbcs);
        }
    }
    if (!vi) {
        int glxAttribs[] = {
            GLX_RGBA,
            GLX_DOUBLEBUFFER,
            GLX_RED_SIZE, 8,
            GLX_GREEN_SIZE, 8,
            GLX_BLUE_SIZE, 8,
            GLX_ALPHA_SIZE, 8,
            0
        };
        vi = glXChooseVisual(dpy, screen, glxAttribs);
    }
    if (!vi) {
        XCloseDisplay(dpy);
        if (err) *err = L"GLX Visual destegi bulunamadi.";
        return false;
    }

    Colormap cmap = XCreateColormap(dpy, RootWindow(dpy, screen), vi->visual, AllocNone);
    XSetWindowAttributes swa{};
    swa.colormap = cmap;
    swa.background_pixel = 0;
    swa.border_pixel = 0;
    swa.bit_gravity = NorthWestGravity;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     LeaveWindowMask | StructureNotifyMask | FocusChangeMask | PropertyChangeMask;

    // Win32 DpiFor karsiligi: masaustu olcegi (Xft.dpi / GDK_SCALE ...)
    const UINT dpi = lnx::SystemDpi(dpy);
    m_dpi = dpi;
    const float s = dpi / 96.0f;
    int initW = (int)(1320 * s);
    int initH = (int)(800 * s);
    initW = std::min(initW, DisplayWidth(dpy, screen));
    initH = std::min(initH, DisplayHeight(dpy, screen));

    // Konum verilmez (Win32 CW_USEDEFAULT): yerlesimi pencere yoneticisi secer.
    Window xwin = XCreateWindow(dpy, RootWindow(dpy, screen), 0, 0, initW, initH, 0,
                                vi->depth, InputOutput, vi->visual,
                                CWColormap | CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask, &swa);
    XFree(vi);

    XStoreName(dpy, xwin, "FullTerminal");
    {
        const char* title = "FullTerminal";
        XChangeProperty(dpy, xwin, XInternAtom(dpy, "_NET_WM_NAME", 0), XInternAtom(dpy, "UTF8_STRING", 0), 8,
                        PropModeReplace, reinterpret_cast<const unsigned char*>(title), (int)strlen(title));
        XClassHint ch{};
        ch.res_name = const_cast<char*>("fullterminal");
        ch.res_class = const_cast<char*>("FullTerminal");
        XSetClassHint(dpy, xwin, &ch);
        long pid = (long)getpid();
        XChangeProperty(dpy, xwin, XInternAtom(dpy, "_NET_WM_PID", 0), XA_CARDINAL, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(&pid), 1);
        char host[256]{};
        if (gethostname(host, sizeof(host) - 1) == 0) {
            XTextProperty tp{};
            char* hp = host;
            if (XStringListToTextProperty(&hp, 1, &tp)) {
                XSetWMClientMachine(dpy, xwin, &tp);
                XFree(tp.value);
            }
        }
        XWMHints* wh = XAllocWMHints();
        wh->flags = InputHint | StateHint;
        wh->input = 1;
        wh->initial_state = NormalState;
        XSetWMHints(dpy, xwin, wh);
        XFree(wh);
    }
    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", 0);
    XSetWMProtocols(dpy, xwin, &wmDelete, 1);

    // Dosya surukle-birak (Win32 DragAcceptFiles)
    InitXdndAtoms(dpy);
    {
        long ver = 5;
        XChangeProperty(dpy, xwin, s_xa.aware, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&ver), 1);
    }

    auto* handle = new LinuxWindowHandle();
    handle->display = dpy;
    handle->window = xwin;
    handle->screen = screen;
    m_hwnd = reinterpret_cast<HWND>(handle);
    m_ui.SetOwner(m_hwnd);
    lnx::SetMainWindow(m_hwnd);
    g_linuxLastWndProc = &MainWindow::WndProcStatic;
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Aktif monitorun dock/panel disindaki calisma alanina (rcWork) sigdir ve ortala:
    // Ubuntu sol dock (dash-to-dock) veya ust bar ile pencere ust uste binmesin.
    int initX = 64, initY = 48;
    {
        POINT cur{};
        GetCursorPos(&cur);
        HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(mon, &mi)) {
            const int workW = std::max<int>(480, mi.rcWork.right - mi.rcWork.left);
            const int workH = std::max<int>(320, mi.rcWork.bottom - mi.rcWork.top);
            initW = std::min(initW, std::max(480, workW - (int)(32 * s)));
            initH = std::min(initH, std::max(320, workH - (int)(32 * s)));
            initX = mi.rcWork.left + (workW - initW) / 2;
            initY = mi.rcWork.top + (workH - initH) / 2;
        }
    }
    lnx::NoteClientSize(initW, initH);
    SetWindowPos(m_hwnd, nullptr, initX, initY, initW, initH,
                 SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);

    // Pencere/tepsi simgesi: once gomulu app.png (Win32 kaynak 1), yoksa vurgu renkli cizim
    m_icon = LoadIconW(nullptr, nullptr);
    if (!m_icon) m_icon = MakeAppIcon(32, theme::Ac());
    if (m_icon) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)m_icon);
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)m_icon);
    }

    if (!m_r.Init(m_hwnd, m_dpi, err)) {
        lnx::SetMainWindow(nullptr);
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

    // Global kisayol YALNIZCA Guake modunda kaydedilir (Win32 ile ayni); kapaliyken
    // F12 gibi bir tusu sistem genelinde calmak diger uygulamalari bozar.
    if (m_cfg.launchAtStartup) {
        SetLaunchAtStartup(true);
    } else if (IsLaunchAtStartupEnabled()) {
        m_cfg.launchAtStartup = true;
        SetLaunchAtStartup(true);
    }
    if (m_cfg.quakeMode) {
        m_cfg.quakeMode = false;
        EnterQuakeMode(!m_startupLaunch);
    }
    lnx::PublishInstance(m_hwnd, true);
    return true;
}

void MainWindow::Show(int /*nCmdShow*/) {
    if (InQuake() || !m_hwnd) return;
    if (m_startupLaunch && (m_cfg.minimizeToTray || m_cfg.runInBackground)) {
        HideToTray();
        return;
    }
    Display* dpy = Dpy(m_hwnd);
    Window win = XWin(m_hwnd);
    XMapRaised(dpy, win);
    XFlush(dpy);

    // Ilk kare pencere yoneticisinin verdigi gercek boyutla cizilsin; yoksa
    // (orn. ekrani kaplayan WM) 1320x800'luk kare tamponun sol altinda kalir.
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    XEvent ev;
    while (std::chrono::steady_clock::now() < until) {
        if (XCheckTypedWindowEvent(dpy, win, MapNotify, &ev)) break;
        usleep(4000);
    }
    XSync(dpy, 0);
    XWindowAttributes wa{};
    if (XGetWindowAttributes(dpy, win, &wa) && wa.width > 0 && wa.height > 0 &&
        (wa.width != (int)m_r.Width() || wa.height != (int)m_r.Height())) {
        m_r.Resize(wa.width, wa.height);
        lnx::NoteClientSize(wa.width, wa.height);
        OnResize();
    }
    m_dirty = true;
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

void MainWindow::LinuxDestroy() {
    if (!m_hwnd) return;
    lnx::PublishInstance(m_hwnd, false);
    SaveSession();
    UnregisterQuakeHotkey();
    RemoveTrayIcon();
    m_tabLayouts.clear();
    m_tabs.clear();
    m_r.Shutdown();
    lnx::SetMainWindow(nullptr);
    if (m_icon) { DestroyIcon(m_icon); m_icon = nullptr; }
    auto* lh = reinterpret_cast<LinuxWindowHandle*>(m_hwnd);
    m_hwnd = nullptr;
    if (lh) {
        Display* dpy = static_cast<Display*>(lh->display);
        if (dpy) {
            for (Cursor& cur : s_cursors) {
                if (cur) { XFreeCursor(dpy, cur); cur = 0; }
            }
            if (lh->window) XDestroyWindow(dpy, static_cast<Window>(lh->window));
            XCloseDisplay(dpy);
        }
        delete lh;
    }
}

int MainWindow::LinuxHitResizeDir(int x, int y) const {
    if (!m_hwnd || InQuake() || IsZoomed(m_hwnd) || DrawerUp() || m_agentFleetOpen ||
        m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt || m_sftpNewFilePrompt) {
        return -1;
    }
    RECT rc{};
    if (!GetClientRect(m_hwnd, &rc) || rc.right <= rc.left || rc.bottom <= rc.top) return -1;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int b = std::max(6, (int)std::round(8 * m_lay.scale));
    const int c = std::max(14, (int)std::round(16 * m_lay.scale));
    const int outer = std::max(4, (int)std::round(5 * m_lay.scale));

    // Pencere dugmelerinin ic kisminda yalniz en dis 5px kenar boyutlandirmaya ayrilir
    if (y >= outer && x >= outer && x < w - outer &&
        (HitRect(m_lay.btnClose, x, y) || HitRect(m_lay.btnMax, x, y) ||
         HitRect(m_lay.btnMin, x, y) || HitRect(m_lay.btnQuake, x, y) ||
         HitRect(m_lay.menuBtn, x, y))) {
        return -1;
    }

    const bool L = x < b;
    const bool R = x >= w - b;
    const bool T = y < b;
    const bool B = y >= h - b;
    if (!L && !R && !T && !B) {
        if (x >= w - c && y >= h - c) return 4; // Sag alt durum cubugu tutamagi
        return -1;
    }

    const bool cL = x < c;
    const bool cR = x >= w - c;
    const bool cT = y < c;
    const bool cB = y >= h - c;

    if ((T && cL) || (L && cT)) return 0; // Sol-Ust
    if ((T && cR) || (R && cT)) return 2; // Sag-Ust
    if ((B && cR) || (R && cB)) return 4; // Sag-Alt
    if ((B && cL) || (L && cB)) return 6; // Sol-Alt
    if (T) return 1;                      // Ust
    if (R) return 3;                      // Sag
    if (B) return 5;                      // Alt
    if (L) return 7;                      // Sol
    return -1;
}

void MainWindow::LinuxOnDpiChanged(UINT dpi) {
    if (!m_hwnd || dpi == 0 || dpi == m_dpi) return;
    const UINT old = m_dpi;
    m_dpi = dpi;
    m_r.SetDpi(m_dpi);
    if (InQuake()) {
        // Guake geometrisi calisma alanindan yeniden hesaplanir
        if (m_quakeState == QuakeState::Visible) PostQuake(QC_REAPPLY);
    } else if (!IsZoomed(m_hwnd) && old) {
        // Win32 WM_DPICHANGED onerisi gibi: fiziksel boyut korunsun
        const unsigned w = (unsigned)std::max(200.0, std::round(m_r.Width() * (double)dpi / old));
        const unsigned h = (unsigned)std::max(150.0, std::round(m_r.Height() * (double)dpi / old));
        XResizeWindow(Dpy(m_hwnd), XWin(m_hwnd), w, h);
    }
    OnResize();
    m_dirty = true;
}

void MainWindow::LinuxUpdateCursor(int x, int y) {
    if (!m_hwnd) return;
    int want = 0;
    // Normal pencerede kenar/kose boyutlandirma imlecleri
    const int edgeOnly = std::max(3, (int)std::floor(4.0f * m_lay.scale));
    const bool overStatusBtns = (y < (int)m_r.Height() - edgeOnly) &&
        (x >= (int)(m_drawerBtn.left - std::floor(175.0f * m_lay.scale)));
    if (m_lnxResizing && m_lnxResizeDir >= 0 && m_lnxResizeDir < 8) {
        want = 3 + m_lnxResizeDir;
    } else if (m_quakeState == QuakeState::Visible &&
               (m_quakeResizing || (y >= (int)m_r.Height() - QuakeGrip() && !overStatusBtns))) {
        // Guake: alt kenar yukseklik tutamagi
        want = 2;
    } else if (const int dir = LinuxHitResizeDir(x, y); dir >= 0) {
        want = 3 + dir;
    } else if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && m_view == View::Terminal) {
        // OSC 8 / URL: Ctrl basiliyken baglanti uzerinde el imleci
        int col = 0, row = 0;
        if (CellFromPoint(x, y, col, row)) {
            if (auto* t = Active()) {
                if (!t->screen().GetLinkAt(col, row).empty()) want = 1;
            }
        }
    }
    if (want == m_lnxCursor) return;
    m_lnxCursor = want;
    static const unsigned kShapes[11] = {
        XC_left_ptr,            // 0: varsayilan ok
        XC_hand2,               // 1: baglanti eli
        XC_sb_v_double_arrow,   // 2: Guake alt tutamak
        XC_top_left_corner,     // 3: dir 0 (TL)
        XC_top_side,            // 4: dir 1 (T)
        XC_top_right_corner,    // 5: dir 2 (TR)
        XC_right_side,          // 6: dir 3 (R)
        XC_bottom_right_corner, // 7: dir 4 (BR)
        XC_bottom_side,         // 8: dir 5 (B)
        XC_bottom_left_corner,  // 9: dir 6 (BL)
        XC_left_side            // 10: dir 7 (L)
    };
    Display* dpy = Dpy(m_hwnd);
    if (!s_cursors[want]) s_cursors[want] = XCreateFontCursor(dpy, kShapes[want]);
    XDefineCursor(dpy, XWin(m_hwnd), s_cursors[want]);
    XFlush(dpy);
}

bool MainWindow::LinuxHandleXdnd(void* xevent) {
    XEvent& ev = *static_cast<XEvent*>(xevent);
    Display* dpy = Dpy(m_hwnd);
    Window win = XWin(m_hwnd);
    if (!dpy) return false;

    if (ev.type == SelectionNotify) {
        if (ev.xselection.property != s_xa.prop || ev.xselection.selection != s_xa.selection) return false;
        std::vector<std::wstring> files;
        Atom type = 0;
        int format = 0;
        unsigned long n = 0, after = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(dpy, win, s_xa.prop, 0, 1 << 20, 1, AnyPropertyType, &type, &format, &n, &after, &data) == 0 && data) {
            files = ParseUriList(std::string(reinterpret_cast<char*>(data), n));
            XFree(data);
        }
        if (s_dnd.source) {
            SendXdnd(dpy, s_dnd.source, s_xa.finished, (long)win, files.empty() ? 0 : 1,
                     files.empty() ? 0 : (long)s_xa.actionCopy, 0, 0);
        }
        const POINT pt = s_dnd.pt;
        s_dnd = XdndState{};
        if (!files.empty()) OnFilesDropped(files, pt);
        m_dirty = true;
        return true;
    }

    if (ev.type != ClientMessage) return false;
    const Atom t = ev.xclient.message_type;
    if (t == s_xa.enter) {
        s_dnd = XdndState{};
        s_dnd.source = (Window)ev.xclient.data.l[0];
        s_dnd.version = (ev.xclient.data.l[1] >> 24) & 0xFF;
        if (ev.xclient.data.l[1] & 1) {
            Atom type = 0;
            int format = 0;
            unsigned long n = 0, after = 0;
            unsigned char* data = nullptr;
            if (XGetWindowProperty(dpy, s_dnd.source, s_xa.typeList, 0, 1024, 0, XA_ATOM, &type, &format, &n, &after, &data) == 0 && data) {
                const auto* atoms = reinterpret_cast<unsigned long*>(data);
                for (unsigned long i = 0; i < n; ++i) if ((Atom)atoms[i] == s_xa.uriList) s_dnd.accept = true;
                XFree(data);
            }
        } else {
            for (int i = 2; i <= 4; ++i) if ((Atom)ev.xclient.data.l[i] == s_xa.uriList) s_dnd.accept = true;
        }
        return true;
    }
    if (t == s_xa.position) {
        const int rx = (int)((ev.xclient.data.l[2] >> 16) & 0xFFFF);
        const int ry = (int)(ev.xclient.data.l[2] & 0xFFFF);
        Window child = 0;
        int cx = 0, cy = 0;
        XTranslateCoordinates(dpy, DefaultRootWindow(dpy), win, rx, ry, &cx, &cy, &child);
        s_dnd.pt = POINT{ cx, cy };
        if (!s_dnd.source) s_dnd.source = (Window)ev.xclient.data.l[0];
        SendXdnd(dpy, s_dnd.source, s_xa.status, (long)win, s_dnd.accept ? 3 : 0, 0, 0,
                 s_dnd.accept ? (long)s_xa.actionCopy : 0);
        return true;
    }
    if (t == s_xa.leave) {
        s_dnd = XdndState{};
        return true;
    }
    if (t == s_xa.drop) {
        if (!s_dnd.source) s_dnd.source = (Window)ev.xclient.data.l[0];
        if (s_dnd.accept) {
            const Time when = s_dnd.version >= 1 ? (Time)ev.xclient.data.l[2] : (Time)0L /*CurrentTime*/;
            XConvertSelection(dpy, s_xa.selection, s_xa.uriList, s_xa.prop, win, when);
            XFlush(dpy);
        } else {
            SendXdnd(dpy, s_dnd.source, s_xa.finished, (long)win, 0, 0, 0, 0);
            s_dnd = XdndState{};
        }
        return true;
    }
    return false;
}

void MainWindow::LinuxSysCommand(UINT sc) {
    if (!sc || !m_hwnd) return;
    if (InQuake()) {
        // Win32 WM_SYSCOMMAND Guake davranisi
        if (sc == lnx::kScMinimize) { QuakeHide(); return; }
        if (sc == lnx::kScMaximize) { PostQuake(QC_FULL); return; }
        if (sc == lnx::kScMove || sc == lnx::kScSize || sc == lnx::kScRestore) return;
    }
    lnx::ApplyWindowCommand(m_hwnd, sc);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_USER + 777:
        m_dirty = true;
        return 0;

    case WM_PTY_DATA:
        m_dirty = true;
        return 0;

    case kWmHotkey:
        if ((int)wp == kQuakeHotKeyId) QuakeToggle();
        return 0;

    case WM_SUMMON:
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

    case WM_TRAY: {
        const UINT ev = (UINT)(lp & 0xFFFF);
        if (InQuake()) {
            // Tek tik goster/odakla; "odak kaybinda gizle" az once gizlediyse geri cagirma
            if (ev == kWmLButtonUp) {
                const bool justAutoHidden = m_lastAutoHide &&
                    (NowTicks() - m_lastAutoHide) < TickFreq() * 2 / 5;
                if (!justAutoHidden) {
                    if (m_quakeState == QuakeState::Visible) ForceForeground();
                    else QuakeShow();
                }
            } else if (ev == kWmRButtonUp) {
                ShowTrayMenu();
            }
            return 0;
        }
        if (ev == kWmLButtonDblClk || ev == kWmLButtonUp) RestoreFromTray();
        else if (ev == kWmRButtonUp) ShowTrayMenu();
        return 0;
    }

    case WM_COMMAND:
        switch (wp & 0xFFFF) {
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
        case TRAY_STARTUP:
            m_cfg.launchAtStartup = !m_cfg.launchAtStartup;
            SetLaunchAtStartup(m_cfg.launchAtStartup);
            m_cfg.Save(m_dataDir);
            Toast(Tr(m_cfg.launchAtStartup ? Msg::ToastStartupOn : Msg::ToastStartupOff));
            return 0;
        case TRAY_QUIT:  m_reallyQuit = true; PostMessageW(m_hwnd, WM_CLOSE, 0, 0); return 0;
        default: break;
        }
        m_dirty = true;   // arka plan isleri (SFTP yukleme) yeniden cizim icin de gonderir
        return 0;

    case WM_SIZE:
        OnResize();
        return 0;

    case WM_SETFOCUS:
        m_focused = true;
        ResetCursorBlink();
        return 0;

    case WM_KILLFOCUS:
        m_focused = false;
        m_dirty = true;
        return 0;

    case WM_ACTIVATE:
        m_focused = (LOWORD(wp) != 0);
        if (m_focused) ResetCursorBlink();
        m_dirty = true;
        return 0;

    case WM_ACTIVATEAPP:
        if (!wp && InQuake() && m_cfg.quakeHideOnLoseFocus && !m_quakeResizing && !m_lnxResizing && !m_captureHotkey &&
            !lnx::IsModalActive() && !lnx::WasModalRecentlyClosed() &&
            (m_quakeState == QuakeState::Visible || m_quakeState == QuakeState::DroppingDown)) {
            m_lastAutoHide = NowTicks();
            QuakeHide();
        }
        return 0;

    case WM_CHAR:
        OnChar((wchar_t)wp, false);
        return 0;

    case WM_SYSCHAR:
        if (wp == L' ') {
            if (m_swallowChar) { m_swallowChar = false; return 0; }
            LinuxSysCommand(lnx::ShowWindowMenu(m_hwnd));
            return 0;
        }
        OnChar((wchar_t)wp, true);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (msg == WM_SYSKEYDOWN && wp == VK_F4 && !m_captureHotkey &&
            !(GetKeyState(VK_CONTROL) & 0x8000)) {
            m_swallowChar = false;
            if (!(lp & (1 << 30))) PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        OnKeyDown(wp, msg == WM_SYSKEYDOWN);
        return 0;

    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        LinuxUpdateCursor(x, y);
        if (m_lnxResizing) {
            POINT cur{};
            GetCursorPos(&cur);
            const int dx = cur.x - m_lnxResizeStartMouse.x;
            const int dy = cur.y - m_lnxResizeStartMouse.y;
            const int minW = std::max(360, (int)std::round(480 * m_lay.scale));
            const int minH = std::max(240, (int)std::round(320 * m_lay.scale));

            int left   = m_lnxResizeStartRect.left;
            int top    = m_lnxResizeStartRect.top;
            int right  = m_lnxResizeStartRect.right;
            int bottom = m_lnxResizeStartRect.bottom;

            if (m_lnxResizeDir == 0 || m_lnxResizeDir == 6 || m_lnxResizeDir == 7)
                left = std::min<int>(right - minW, left + dx);
            if (m_lnxResizeDir == 2 || m_lnxResizeDir == 3 || m_lnxResizeDir == 4)
                right = std::max<int>(left + minW, right + dx);
            if (m_lnxResizeDir == 0 || m_lnxResizeDir == 1 || m_lnxResizeDir == 2)
                top = std::min<int>(bottom - minH, top + dy);
            if (m_lnxResizeDir == 4 || m_lnxResizeDir == 5 || m_lnxResizeDir == 6)
                bottom = std::max<int>(top + minH, bottom + dy);

            if (left != s_lastResizeRect.left || top != s_lastResizeRect.top ||
                right != s_lastResizeRect.right || bottom != s_lastResizeRect.bottom) {
                s_lastResizeRect = RECT{ left, top, right, bottom };
                const int newW = right - left;
                const int newH = bottom - top;
                SetWindowPos(m_hwnd, nullptr, left, top, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);
                if ((int)m_r.Width() != newW || (int)m_r.Height() != newH) {
                    m_r.Resize((UINT)newW, (UINT)newH);
                    OnResize();
                }
                if (m_r.Ready() && IsWindowVisible(m_hwnd)) Render();
            }
            m_dirty = true;
            return 0;
        }
        if (m_quakeResizing) {
            POINT sp{}; GetCursorPos(&sp);
            const int workH = m_quakeWork.bottom - m_quakeWork.top;
            if (workH > 0) {
                const int h = std::clamp((int)(sp.y - m_quakeRect.top), workH / 5, workH);
                if (h != m_quakeRect.bottom - m_quakeRect.top) {
                    m_quakeRect.bottom = m_quakeRect.top + h;
                    m_quakeVisH = h;
                    SetWindowPos(m_hwnd, nullptr, 0, 0, m_quakeRect.right - m_quakeRect.left, h,
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                    OnResize();
                    if (m_r.Ready() && IsWindowVisible(m_hwnd)) Render();
                }
                m_cfg.quakeHeightPercent = std::clamp((h * 100 + workH / 2) / workH, 20, 100);
                m_quakeFull = false;
            }
            m_dirty = true;
            return 0;
        }
        m_in.mx = (float)x; m_in.my = (float)y;

        if (DrawerUp()) {
            m_hoverTab = m_hoverBtn = m_hoverClose = -1;
            m_dirty = true;
            return 0;
        }

        if (m_mouseBtn >= 0 || MouseReporting((wp & MK_SHIFT) != 0)) {
            if (auto* t = Active()) {
                const int mode = t->screen().MouseMode();
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
        if (y < (int)m_lay.titleH) m_dirty = true;
        if (m_view != View::Terminal || HitRect(m_lay.side, x, y) || DrawerUp() || m_agentFleetOpen ||
            !m_error.empty() || m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt || m_sftpNewFilePrompt ||
            (Active() && (Active()->IsSftp() || Active()->IsDatabase() || Active()->ShowSshLoading()))) m_dirty = true;
        if (InQuake() && y >= (int)m_lay.status.top) m_dirty = true;

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
        if (m_in.clicked || m_in.dblClick) m_leavePending = true;
        else m_in.mx = m_in.my = -1.0f;
        m_dirty = true;
        return 0;

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        SetFocus(m_hwnd);
        ResetCursorBlink();
        m_swallowChar = false;
        m_in.mx = (float)x; m_in.my = (float)y;
        m_in.px = m_in.py = -1.0f;
        m_in.down = true;
        m_dirty = true;

        if (m_quakeState == QuakeState::Visible) {
            RECT rc{}; GetClientRect(m_hwnd, &rc);
            const int edgeOnly = std::max(3, (int)std::floor(4.0f * m_lay.scale));
            const bool overStatusBtns = (y < rc.bottom - edgeOnly) &&
                (x >= (int)(m_drawerBtn.left - std::floor(175.0f * m_lay.scale)));
            if (y >= rc.bottom - QuakeGrip() && !overStatusBtns) {
                m_quakeResizing = true;
                m_in.down = false;
                SetCapture(m_hwnd);
                return 0;
            }
        } else if (const int dir = LinuxHitResizeDir(x, y); dir >= 0) {
            Display* dpy = Dpy(m_hwnd);
            Window r = 0, c = 0;
            int rx = 0, ry = 0, wx = 0, wy = 0;
            unsigned int mask = 0;
            // Sentetik birim testi tiklamalarini (Button1Mask == 0) yakalama
            if (dpy && XQueryPointer(dpy, DefaultRootWindow(dpy), &r, &c, &rx, &ry, &wx, &wy, &mask) &&
                (mask & Button1Mask) != 0) {
                POINT origin{ 0, 0 };
                ClientToScreen(m_hwnd, &origin);
                RECT rc{};
                GetClientRect(m_hwnd, &rc);
                m_lnxResizeStartMouse = POINT{ rx, ry };
                m_lnxResizeStartRect = RECT{
                    origin.x,
                    origin.y,
                    origin.x + (rc.right - rc.left),
                    origin.y + (rc.bottom - rc.top)
                };
                s_lastResizeRect = m_lnxResizeStartRect;
                m_lnxResizing = true;
                m_lnxResizeDir = dir;
                m_in.down = false;
                SetCapture(m_hwnd);
                LinuxUpdateCursor(x, y);
                return 0;
            }
        }

        if (DrawerUp() || m_agentFleetOpen || m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt || m_sftpNewFilePrompt) {
            m_in.px = (float)x; m_in.py = (float)y;
            return 0;
        }
        if (!m_error.empty()) {
            if (HitRect(ErrorOverlayRect(), x, y)) {
                m_in.px = (float)x; m_in.py = (float)y;
                return 0;
            }
            m_error.clear();
        }

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
        if (const int c = HitTabClose(x, y); c >= 0) {
            m_pressClose = c;
            return 0;
        }

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

        if (y < (int)m_lay.titleH && !InQuake()) {
            lnx::StartMoveResize(m_hwnd, 8);
            return 0;
        }

        if (m_view == View::Terminal) {
            if (auto* tab = Active(); tab && (tab->IsSftp() || tab->IsDatabase() || tab->ShowSshLoading())) {
                m_in.px = static_cast<float>(x);
                m_in.py = static_cast<float>(y);
                return 0;
            }
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
                            Toast(std::wstring(TrText(L"Opening link: ", L"Bağlantı açılıyor: ")) + Trunc(Utf8ToWide(url), 35));
                            return 0;
                        }
                    }
                }
                if (MouseReporting((wp & MK_SHIFT) != 0)) {
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
        m_in.px = (float)x; m_in.py = (float)y;
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        m_in.mx = (float)x; m_in.my = (float)y;
        m_in.px = (float)x; m_in.py = (float)y;
        m_in.dblClick = true;
        m_in.clicked = true;
        m_dirty = true;

        if (DrawerUp() || m_agentFleetOpen || m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt || m_sftpNewFilePrompt ||
            (!m_error.empty() && HitRect(ErrorOverlayRect(), x, y))) {
            m_in.px = (float)x; m_in.py = (float)y;
            return 0;
        }

        if (y < (int)m_lay.titleH) {
            int hit = HitTab(x, y);
            if (hit >= 0 && (size_t)hit < m_tabs.size()) {
                StartRenameTab((size_t)hit);
                return 0;
            }
            if (!InQuake() && !HitRect(m_lay.btnClose, x, y) && !HitRect(m_lay.btnMin, x, y) &&
                !HitRect(m_lay.btnMax, x, y) && !HitRect(m_lay.btnQuake, x, y) &&
                !HitRect(m_lay.menuBtn, x, y) && !HitRect(m_lay.newTab, x, y) &&
                !HitRect(m_lay.overflow, x, y)) {
                ShowWindow(m_hwnd, IsZoomed(m_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
                return 0;
            }
        }

        int col, row;
        const bool inTerm = (m_view == View::Terminal) && Active() && !Active()->IsSftp() && !Active()->IsDatabase() &&
                            !HitAgentRibbon(x, y) && CellFromPoint(x, y, col, row);
        if (DrawerUp() || (y >= (int)m_lay.titleH && !inTerm)) {
            m_in.px = (float)x; m_in.py = (float)y;
        } else if (inTerm && MouseReporting((wp & MK_SHIFT) != 0)) {
            m_mouseBtn = 0;
            SendMouseReport(0, true, false, col, row);
            SetCapture(m_hwnd);
        } else if (inTerm) {
            m_in.clicked = m_in.dblClick = m_in.down = false;
            m_in.px = m_in.py = -1;
            m_selecting = false;
            m_ui.SetFocus(ID_NONE);
            auto& sc = Active()->screen();
            ClearSelection();
            if (sc.WordRangeAt(col, row, m_sel.x0, m_sel.y0, m_sel.x1, m_sel.y1)) {
                m_sel.active = true; m_sel.alt = sc.IsAltBuffer();
                CopySelection();
            }
            ResetCursorBlink();
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (m_lnxResizing) {
            m_lnxResizing = false;
            m_lnxResizeDir = -1;
            ReleaseCapture();
            LinuxUpdateCursor(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            m_dirty = true;
            return 0;
        }
        if (m_quakeResizing) {
            m_quakeResizing = false;
            ReleaseCapture();
            m_cfg.Save(m_dataDir);
            m_dirty = true;
            return 0;
        }
        m_in.down = false;
        m_in.clicked = true;
        m_in.mx = (float)GET_X_LPARAM(lp);
        m_in.my = (float)GET_Y_LPARAM(lp);
        m_dirty = true;
        if (m_mouseBtn == 0) {
            int col, row;
            CellFromPointClamped(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), col, row);
            m_mouseBtn = -1;
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
                Toast(TrText(L"Tab closed", L"Sekme kapatıldı"));
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
        if (DrawerUp() || m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt || m_sftpNewFilePrompt) return 0;
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
            return 0;
        }
        if (my < (int)m_lay.titleH) {
            if (m_lay.tabTier > 0) {
                const int mt = HitTab(mx, my);
                if (mt >= 0 && (size_t)mt < m_tabs.size()) {
                    CloseTab((size_t)mt);
                    Toast(TrText(L"Tab closed", L"Sekme kapatıldı"));
                }
            }
            return 0;
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
                if (Active() && Active()->IsDatabase()) return 0;
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
        return 0;
    }

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (delta == 0) return 0;
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(m_hwnd, &pt);
        if (DrawerUp()) { m_dirty = true; return 0; }
        const bool ctrl  = (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0;
        const bool shift = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0;

        if (m_agentFleetOpen) {
            const float step = (float)delta * 50.0f / (float)WHEEL_DELTA * m_lay.scale;
            m_fleetScroll = std::max(0.0f, m_fleetScroll - step);
            m_dirty = true;
            return 0;
        }
        if (m_view == View::Terminal && !HitRect(m_lay.side, pt.x, pt.y)) {
            if (auto* tab = Active(); tab && tab->IsDatabase()) {
                m_in.wheel += delta / WHEEL_DELTA; m_dirty = true;
            } else if (auto* tab = Active(); tab && tab->IsSftp()) {
                const float mid = std::floor((m_lay.term.left + m_lay.term.right) * 0.5f);
                const float step = (float)delta * 50.0f / (float)WHEEL_DELTA * m_lay.scale;
                if (pt.x < mid) m_sftpLocalScroll = std::max(0.0f, m_sftpLocalScroll - step);
                else            m_sftpRemoteScroll = std::max(0.0f, m_sftpRemoteScroll - step);
                m_dirty = true;
            } else {
                SendWheel(delta, shift, ctrl, pt);
            }
        } else if (m_view == View::Databases && !m_dbEditing && !HitRect(m_lay.side, pt.x, pt.y)) {
            m_in.wheel += delta / WHEEL_DELTA;
        } else if (m_view == View::KnownHosts && !HitRect(m_lay.side, pt.x, pt.y)) {
            const float step = (float)delta * 50.0f / (float)WHEEL_DELTA * m_lay.scale;
            m_knownHostsScroll = std::max(0.0f, m_knownHostsScroll - step);
        } else if (m_view == View::Sftp && !HitRect(m_lay.side, pt.x, pt.y)) {
            const float mid = std::floor((m_lay.main.left + m_lay.main.right) * 0.5f);
            const float step = (float)delta * 50.0f / (float)WHEEL_DELTA * m_lay.scale;
            if (pt.x < mid) m_sftpLocalScroll = std::max(0.0f, m_sftpLocalScroll - step);
            else            m_sftpRemoteScroll = std::max(0.0f, m_sftpRemoteScroll - step);
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

    case WM_DESTROY:
        if (m_httpJob) { CloseHandle(m_httpJob); m_httpJob = nullptr; }
        if (m_httpProcess) { CloseHandle(m_httpProcess); m_httpProcess = nullptr; }
        UnregisterQuakeHotkey();
        RemoveTrayIcon();
        m_tabLayouts.clear();
        m_tabs.clear();
        m_r.Shutdown();
        if (m_icon) { DestroyIcon(m_icon); m_icon = nullptr; }
        m_hwnd = nullptr;
        return 0;

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
        // Buradan sonrasi gercek cikis: tum oturumlar olur.
        if (m_cfg.confirmClose) {
            if (m_closePrompting) return 0;
            size_t live = 0;
            for (const auto& t : m_tabs) if (t && t->Alive()) ++live;
            if (live > 0) {
                if (InQuake()) {
                    if (m_quakeState != QuakeState::Visible) {
                        QuakeShow();
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
                    m_reallyQuit = false;
                    return 0;
                }
            }
        }
        LinuxDestroy();
        return 0;

    default:
        break;
    }
    return 0;
}

static WPARAM KeySymToVk(KeySym sym) {
    switch (sym) {
    case XK_BackSpace:   return VK_BACK;
    case XK_Tab:
    case XK_ISO_Left_Tab: return VK_TAB;
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
    case XK_Pause:       return VK_PAUSE;
    case XK_Scroll_Lock: return VK_SCROLL;
    case XK_Control_L:
    case XK_Control_R:   return VK_CONTROL;
    case XK_Shift_L:
    case XK_Shift_R:     return VK_SHIFT;
    case XK_Alt_L:
    case XK_Alt_R:
    case XK_Meta_L:
    case XK_Meta_R:      return VK_MENU;
    case XK_Super_L:
    case XK_Super_R:     return VK_LWIN;
    case XK_grave:
    case XK_asciitilde:
    case XK_quotedbl:    return VK_OEM_3; // US `~ ve TR Q "é (Tab ustu tus)
    case XK_semicolon:
    case XK_colon:       return 0xBA;     // VK_OEM_1
    case XK_equal:
    case XK_plus:        return 0xBB;     // VK_OEM_PLUS
    case XK_comma:
    case XK_less:        return 0xBC;     // VK_OEM_COMMA
    case XK_minus:
    case XK_underscore:  return 0xBD;     // VK_OEM_MINUS
    case XK_period:
    case XK_greater:     return 0xBE;     // VK_OEM_PERIOD
    case XK_slash:
    case XK_question:    return 0xBF;     // VK_OEM_2
    case XK_bracketleft:
    case XK_braceleft:   return 0xDB;     // VK_OEM_4
    case XK_backslash:
    case XK_bar:         return 0xDC;     // VK_OEM_5
    case XK_bracketright:
    case XK_braceright:  return 0xDD;     // VK_OEM_6
    case XK_apostrophe:  return 0xDE;     // VK_OEM_7
    default:
        if (sym >= XK_F1 && sym <= XK_F24) return (WPARAM)(VK_F1 + (sym - XK_F1));
        if (sym >= XK_a && sym <= XK_z) return (WPARAM)('A' + (sym - XK_a));
        if (sym >= XK_A && sym <= XK_Z) return (WPARAM)sym;
        if (sym >= XK_0 && sym <= XK_9) return (WPARAM)sym;
        if (sym >= XK_KP_0 && sym <= XK_KP_9) return (WPARAM)(VK_NUMPAD0 + (sym - XK_KP_0));
        break;
    }
    return 0;
}

void MainWindow::RunLinuxLoop() {
    if (!m_hwnd) return;
    Display* dpy = Dpy(m_hwnd);
    Window xwin = XWin(m_hwnd);
    if (!dpy) return;
    const Window root = DefaultRootWindow(dpy);
    const Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", 0);
    const Atom wmStateAtom = XInternAtom(dpy, "WM_STATE", 0);
    const Atom netWmState = XInternAtom(dpy, "_NET_WM_STATE", 0);
    const Atom netActiveWin = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", 0);
    const Atom ftWakeAtom = XInternAtom(dpy, "_FT_WAKE", 0);

    const int x11_fd = ConnectionNumber(dpy);
    auto lastBlink = std::chrono::steady_clock::now();
    int64_t lastBlinkReset = g_linuxTimerResetMs.load();
    bool focusVerifyPending = false;
    auto focusVerifyDeadline = std::chrono::steady_clock::now();
    Time lastClickTime = 0;
    int lastClickX = -100, lastClickY = -100;

    auto makeKeyState = [](unsigned int st) -> WPARAM {
        WPARAM mk = 0;
        if (st & ControlMask) mk |= MK_CONTROL;
        if (st & ShiftMask)   mk |= MK_SHIFT;
        if (st & Button1Mask) mk |= MK_LBUTTON;
        if (st & Button2Mask) mk |= MK_MBUTTON;
        if (st & Button3Mask) mk |= MK_RBUTTON;
        return mk;
    };

    while (m_hwnd != nullptr) {
        // Arka plan thread'lerinden gelen mesajlar (PostMessageW)
        LinuxMsg qmsg;
        while (m_hwnd && LinuxPopMessage(qmsg)) {
            WndProc(qmsg.msg, qmsg.wp, qmsg.lp);
        }
        if (!m_hwnd) break;
        lnx::PumpDeferred();

        while (m_hwnd && XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);

            // Tepsi simgesi, tepsi yoneticisi, global kisayol
            int hotkey = 0;
            if (lnx::HandleEvent(&ev, &hotkey)) {
                if (hotkey) WndProc(kWmHotkey, (WPARAM)hotkey, 0);
                continue;
            }
            if (ev.xany.window != xwin && ev.type != PropertyNotify) continue;

            // Modal kutu/menu acikken birikmis girdi kapaninca islenmesin (Win32 sahibi devre disi)
            if (ev.type == KeyPress || ev.type == KeyRelease || ev.type == ButtonPress || ev.type == ButtonRelease) {
                const unsigned long t = (ev.type == KeyPress || ev.type == KeyRelease) ? ev.xkey.time : ev.xbutton.time;
                const unsigned long fence = lnx::ModalInputFence();
                if (fence && t && t <= fence) continue;
            }

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
                if (ev.xexpose.count == 0) { m_dirty = true; Render(); }
                break;

            case ConfigureNotify:
                while (XCheckTypedWindowEvent(dpy, xwin, ConfigureNotify, &ev)) {}
                if (!lnx::HasWindowRgn() && !m_quakeResizing && !m_lnxResizing &&
                    m_quakeState != QuakeState::DroppingDown && m_quakeState != QuakeState::SlidingUp &&
                    (ev.xconfigure.width != (int)m_r.Width() || ev.xconfigure.height != (int)m_r.Height())) {
                    m_r.Resize(ev.xconfigure.width, ev.xconfigure.height);
                    lnx::NoteClientSize(ev.xconfigure.width, ev.xconfigure.height);
                    OnResize();
                    if (m_r.Ready() && IsWindowVisible(m_hwnd)) Render();
                    m_dirty = true;
                }
                break;

            case PropertyNotify:
                if (ev.xproperty.window == root) {
                    // Masaustu olcegi degisti (Win32 WM_DPICHANGED)
                    if (ev.xproperty.atom == XA_RESOURCE_MANAGER) {
                        LinuxOnDpiChanged(lnx::SystemDpi(dpy));
                    } else if (ev.xproperty.atom == netActiveWin &&
                               !lnx::IsModalActive() && !lnx::WasModalRecentlyClosed() &&
                               !m_quakeResizing && !m_lnxResizing) {
                        // Aktif pencere baska uygulamaya gectiginde Visor otomatik gizlemeyi tetikle
                        focusVerifyPending = true;
                        focusVerifyDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
                    }
                } else if (ev.xproperty.window == xwin &&
                           (ev.xproperty.atom == wmStateAtom || ev.xproperty.atom == netWmState)) {
                    const bool iconic = IsIconic(m_hwnd) != FALSE;
                    if (iconic && !m_lnxIconic) {
                        // Win32 WM_SIZE / SIZE_MINIMIZED
                        if (InQuake()) {
                            ShowWindow(m_hwnd, SW_HIDE);
                            m_quakeState = QuakeState::Hidden;
                            m_quakeFrac = 0.0f;
                        } else if (m_cfg.minimizeToTray) {
                            m_lnxIconic = true;
                            HideToTray();
                            break;
                        }
                    }
                    m_lnxIconic = iconic;
                    m_dirty = true;
                }
                break;

            case FocusIn:
                if (ev.xfocus.detail != NotifyPointer) {
                    focusVerifyPending = false;
                    lnx::ClearAttention();
                    WndProc(WM_SETFOCUS, 0, 0);
                    WndProc(WM_ACTIVATEAPP, TRUE, 0);
                }
                break;

            case FocusOut:
                if (ev.xfocus.detail != NotifyPointer && ev.xfocus.detail != NotifyInferior &&
                    !lnx::IsModalActive() && !lnx::WasModalRecentlyClosed() &&
                    !m_quakeResizing && !m_lnxResizing) {
                    if (ev.xfocus.mode != NotifyGrab && ev.xfocus.mode != NotifyUngrab) {
                        focusVerifyPending = false;
                        WndProc(WM_KILLFOCUS, 0, 0);
                        WndProc(WM_ACTIVATEAPP, FALSE, 0);
                    } else if (ev.xfocus.mode == NotifyGrab) {
                        // Mutter / KWin baska pencereye tiklandiginda once NotifyGrab ile FocusOut gonderir;
                        // 50ms sonra gercek X11 odagini kontrol ederek baska pencereye gecildiyse gizle.
                        focusVerifyPending = true;
                        focusVerifyDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                    }
                }
                break;

            case LeaveNotify:
                WndProc(WM_MOUSELEAVE, 0, 0);
                break;

            case KeyRelease: {
                const KeySym sym = XLookupKeysym(&ev.xkey, 0);
                if (sym == XK_F4) m_lnxF4Down = false;
                break;
            }

            case KeyPress: {
                char buf[64]{};
                KeySym sym = 0;
                int len = XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &sym, nullptr);
                if (len > 0) buf[len] = '\0';
                const bool alt = (ev.xkey.state & Mod1Mask) != 0;
                const bool ctrl = (ev.xkey.state & ControlMask) != 0;
                const bool shift = (ev.xkey.state & ShiftMask) != 0;
                const bool winMod = (ev.xkey.state & Mod4Mask) != 0;

                // Alt+F4 terminale ESC[1;3S olarak gitmesin; WM_CLOSE yolundan gecsin
                // (Guake'de gizler, confirmClose sorusu orada).
                if (alt && !ctrl && sym == XK_F4 && !m_captureHotkey) {
                    m_swallowChar = false;
                    if (!m_lnxF4Down) {   // tekrar degil
                        m_lnxF4Down = true;
                        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
                    }
                    break;
                }

                WPARAM vk = KeySymToVk(sym);
                if (vk == 0) {
                    const KeySym baseSym = XLookupKeysym(&ev.xkey, 0);
                    vk = KeySymToVk(baseSym);
                }
                if (vk == 0 && ev.xkey.keycode == 49) {
                    vk = VK_OEM_3; // Fiziksel Tab ustu tusu (" / `)
                }

                // Visor acikken klavye kancasi (m_hotkeyViaHook) ile yakalanan kisayol terminale sizmasin
                if (InQuake() && !m_captureHotkey && m_hotkeyViaHook && m_cfg.quakeHotkeyVk2 == 0 && vk != 0) {
                    int curMods = 0;
                    if (ctrl)   curMods |= MOD_CONTROL;
                    if (alt)    curMods |= MOD_ALT;
                    if (shift)  curMods |= MOD_SHIFT;
                    if (winMod) curMods |= MOD_WIN;
                    if ((int)vk == m_cfg.quakeHotkeyVk && curMods == (m_cfg.quakeHotkeyMods & 0x0F)) {
                        break;
                    }
                }

                if (vk != 0) {
                    OnKeyDown(vk, alt);
                }
                if (!m_hwnd) break;

                // Alt+Bosluk: pencere (sistem) menusu. Kisayol yakalama tusu aldiysa acilmaz.
                if (alt && !ctrl && sym == XK_space) {
                    if (m_swallowChar) { m_swallowChar = false; break; }
                    LinuxSysCommand(lnx::ShowWindowMenu(m_hwnd));
                    break;
                }

                // Metin girdisi (OnChar yutma bayragini kendisi tuketir)
                if (len > 0) {
                    wchar_t wch[16]{};
                    int wlen = (int)mbstowcs(wch, buf, 15);
                    for (int i = 0; i < wlen; ++i) {
                        OnChar(wch[i], alt);
                    }
                }
                break;
            }

            case ButtonPress: {
                const int mx = ev.xbutton.x;
                const int my = ev.xbutton.y;
                const WPARAM mk = makeKeyState(ev.xbutton.state);
                const LPARAM pt = MAKELPARAM(mx, my);

                if (ev.xbutton.button == 4 || ev.xbutton.button == 5) {
                    const int16_t delta = (ev.xbutton.button == 4) ? (int16_t)WHEEL_DELTA : (int16_t)-WHEEL_DELTA;
                    const WPARAM wpWheel = (mk & 0xFFFF) | ((WPARAM)(uint16_t)delta << 16);
                    WndProc(WM_MOUSEWHEEL, wpWheel, MAKELPARAM(ev.xbutton.x_root, ev.xbutton.y_root));
                } else if (ev.xbutton.button == 1) {
                    if (lastClickTime && (ev.xbutton.time - lastClickTime) <= 400 &&
                        std::abs(mx - lastClickX) <= 6 && std::abs(my - lastClickY) <= 6) {
                        lastClickTime = 0;
                        WndProc(WM_LBUTTONDBLCLK, mk | MK_LBUTTON, pt);
                    } else {
                        lastClickTime = ev.xbutton.time;
                        lastClickX = mx;
                        lastClickY = my;
                        WndProc(WM_LBUTTONDOWN, mk | MK_LBUTTON, pt);
                    }
                } else if (ev.xbutton.button == 2) {
                    WndProc(WM_MBUTTONDOWN, mk | MK_MBUTTON, pt);
                } else if (ev.xbutton.button == 3) {
                    WndProc(WM_RBUTTONDOWN, mk | MK_RBUTTON, pt);
                }
                break;
            }

            case ButtonRelease: {
                const int mx = ev.xbutton.x;
                const int my = ev.xbutton.y;
                const WPARAM mk = makeKeyState(ev.xbutton.state);
                const LPARAM pt = MAKELPARAM(mx, my);

                if (ev.xbutton.button == 1) {
                    WndProc(WM_LBUTTONUP, mk & ~(WPARAM)MK_LBUTTON, pt);
                } else if (ev.xbutton.button == 2) {
                    WndProc(WM_MBUTTONUP, mk & ~(WPARAM)MK_MBUTTON, pt);
                } else if (ev.xbutton.button == 3) {
                    WndProc(WM_RBUTTONUP, mk & ~(WPARAM)MK_RBUTTON, pt);
                }
                break;
            }

            case MotionNotify: {
                while (XCheckTypedWindowEvent(dpy, xwin, MotionNotify, &ev)) {}
                const WPARAM mk = makeKeyState(ev.xmotion.state);
                WndProc(WM_MOUSEMOVE, mk, MAKELPARAM(ev.xmotion.x, ev.xmotion.y));
                break;
            }

            case SelectionNotify:
                LinuxHandleXdnd(&ev);
                break;

            case SelectionRequest: {
                XSelectionRequestEvent* req = &ev.xselectionrequest;
                XSelectionEvent resp{};
                resp.type = SelectionNotify;
                resp.display = req->display;
                resp.requestor = req->requestor;
                resp.selection = req->selection;
                resp.target = req->target;
                resp.property = req->property;
                resp.time = req->time;

                Atom utf8Atom = XInternAtom(dpy, "UTF8_STRING", 0);
                Atom stringAtom = XA_STRING;
                Atom targetsAtom = XInternAtom(dpy, "TARGETS", 0);

                std::string clipUtf8 = WideToUtf8(ClipboardGetText(m_hwnd));

                if (req->target == targetsAtom) {
                    Atom supportedTargets[2] = { utf8Atom, stringAtom };
                    XChangeProperty(dpy, req->requestor, req->property, XA_ATOM, 32,
                                    PropModeReplace, (unsigned char*)supportedTargets, 2);
                } else if (req->target == utf8Atom || req->target == stringAtom) {
                    XChangeProperty(dpy, req->requestor, req->property, req->target, 8,
                                    PropModeReplace, (unsigned char*)clipUtf8.data(), clipUtf8.size());
                } else {
                    resp.property = 0;
                }
                XSendEvent(dpy, req->requestor, 0, 0, (XEvent*)&resp);
                XFlush(dpy);
                break;
            }

            case ClientMessage:
                if (ev.xclient.message_type == ftWakeAtom) break;
                if (LinuxHandleXdnd(&ev)) break;
                if (lnx::IsSummon(&ev)) { WndProc(WM_SUMMON, 0, 0); m_dirty = true; break; }
                if ((Atom)ev.xclient.data.l[0] == wmDelete) {
                    // Pencere yoneticisinin kapat dugmesi / Alt+F4: Win32 WM_CLOSE yolu
                    WndProc(WM_CLOSE, 0, 0);
                }
                break;

            default:
                break;
            }
        }
        if (!m_hwnd) break;

        // Ertelenmis odak kaybi dogrulamasi (Mutter tikla-odakla NotifyGrab + _NET_ACTIVE_WINDOW gecisi)
        auto now = std::chrono::steady_clock::now();
        if (focusVerifyPending && now >= focusVerifyDeadline) {
            focusVerifyPending = false;
            if (!m_quakeResizing && !m_lnxResizing && !m_selecting && m_mouseBtn < 0 &&
                !lnx::IsModalActive() && !lnx::WasModalRecentlyClosed()) {
                const bool justShownQuake = InQuake() &&
                    (NowTicks() - m_quakeAnimStart) < (TickFreq() / 4);
                if (!justShownQuake) {
                    Window focusWin = 0;
                    int rev = 0;
                    XGetInputFocus(dpy, &focusWin, &rev);
                    const HWND fg = GetForegroundWindow();
                    if (focusWin != xwin && fg != m_hwnd) {
                        WndProc(WM_KILLFOCUS, 0, 0);
                        WndProc(WM_ACTIVATEAPP, FALSE, 0);
                    }
                }
            }
        }

        // Caret blink timer simulation (SetTimer -> ResetCursorBlink fazi sifirlar)
        if (const int64_t reset = g_linuxTimerResetMs.load(); reset != lastBlinkReset) {
            lastBlinkReset = reset;
            lastBlink = now;
        }
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBlink).count() >= (int64_t)kBlinkMs) {
            lastBlink = now;
            m_cursorOn = m_cfg.cursorBlink ? !m_cursorOn : true;
            m_ui.SetCaretVisible(m_cursorOn);
            m_dirty = true;
        }

        bool drawn = Tick();
        if (!m_hwnd) break;

        struct pollfd pfd;
        pfd.fd = x11_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll(&pfd, 1, drawn ? 10 : 16);
    }

    LinuxDestroy();
}

} // namespace ft

#endif // !defined(_WIN32)
