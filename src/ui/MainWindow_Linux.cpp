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

Cursor s_cursors[3] = { 0, 0, 0 };

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
                     StructureNotifyMask | FocusChangeMask | PropertyChangeMask;

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
                                CWColormap | CWEventMask, &swa);
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
    lnx::NoteClientSize(initW, initH);

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
    if (m_cfg.quakeMode) {
        m_cfg.quakeMode = false;
        EnterQuakeMode();
    }
    return true;
}

void MainWindow::Show(int /*nCmdShow*/) {
    if (InQuake() || !m_hwnd) return;
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

void MainWindow::LinuxDestroy() {
    if (!m_hwnd) return;
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
        if (dpy && lh->window) XDestroyWindow(dpy, static_cast<Window>(lh->window));
        if (dpy) XCloseDisplay(dpy);
        delete lh;
    }
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
    // Guake: alt kenar yukseklik tutamagi
    if (m_quakeState == QuakeState::Visible && (m_quakeResizing || y >= (int)m_r.Height() - QuakeGrip())) {
        want = 2;
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
    static const unsigned kShapes[3] = { XC_left_ptr, XC_hand2, XC_sb_v_double_arrow };
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
            const Time when = s_dnd.version >= 1 ? (Time)ev.xclient.data.l[2] : CurrentTime;
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
        case TRAY_QUIT:  m_reallyQuit = true; PostMessageW(m_hwnd, WM_CLOSE, 0, 0); return 0;
        default: break;
        }
        m_dirty = true;   // arka plan isleri (SFTP yukleme) yeniden cizim icin de gonderir
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
    if (!m_hwnd) return;
    Display* dpy = Dpy(m_hwnd);
    Window xwin = XWin(m_hwnd);
    if (!dpy) return;
    const Window root = DefaultRootWindow(dpy);
    const Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", 0);
    const Atom wmStateAtom = XInternAtom(dpy, "WM_STATE", 0);
    const Atom netWmState = XInternAtom(dpy, "_NET_WM_STATE", 0);

    const int x11_fd = ConnectionNumber(dpy);
    auto lastBlink = std::chrono::steady_clock::now();

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
                if (ev.xconfigure.width != (int)m_r.Width() || ev.xconfigure.height != (int)m_r.Height()) {
                    m_r.Resize(ev.xconfigure.width, ev.xconfigure.height);
                    lnx::NoteClientSize(ev.xconfigure.width, ev.xconfigure.height);
                    OnResize();
                    m_dirty = true;
                }
                break;

            case PropertyNotify:
                if (ev.xproperty.window == root) {
                    // Masaustu olcegi degisti (Win32 WM_DPICHANGED)
                    if (ev.xproperty.atom == XA_RESOURCE_MANAGER) LinuxOnDpiChanged(lnx::SystemDpi(dpy));
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
                m_focused = true;
                lnx::ClearAttention();
                m_dirty = true;
                break;

            case FocusOut:
                m_focused = false;
                m_dirty = true;
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
                } else if (ev.xbutton.button == 2 || ev.xbutton.button == 3) {
                    // Win32 WM_MBUTTONDOWN / WM_RBUTTONDOWN
                    if (DrawerUp() || m_renamingTab || m_sftpRenamePrompt || m_sftpNewFolderPrompt) break;
                    const int btn = (ev.xbutton.button == 2) ? 1 : 2;
                    const bool shift = (ev.xbutton.state & ShiftMask) != 0;
                    if (m_view == View::Terminal && m_mouseBtn < 0 && MouseReporting(shift)) {
                        int col, row;
                        if (CellFromPoint(mx, my, col, row)) {
                            m_mouseBtn = btn;
                            SendMouseReport(btn, true, false, col, row);
                            break;
                        }
                    }
                    if (ev.xbutton.button == 3) {
                        // Sag tik: tiklanan bolmeye odaklan (menu birakmada acilir)
                        if (m_view == View::Terminal && HitRect(m_lay.term, mx, my)) {
                            if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
                                auto panes = m_tabLayouts[m_active]->ComputeLayout(m_lay.term);
                                uint32_t hitId = m_tabLayouts[m_active]->HitTestPane(panes, (float)mx, (float)my);
                                if (hitId != 0 && hitId != m_tabLayouts[m_active]->GetFocusedPaneId()) {
                                    m_tabLayouts[m_active]->SetFocusedPane(hitId);
                                    m_tabs[m_active] = m_tabLayouts[m_active]->GetFocusedTab();
                                }
                            }
                        }
                    } else if (my < (int)m_lay.titleH) {
                        // Orta tik baslikta sekmeyi kapatir (MIKRO kademede degil)
                        if (m_lay.tabTier > 0) {
                            const int mt = HitTab(mx, my);
                            if (mt >= 0 && (size_t)mt < m_tabs.size()) {
                                CloseTab((size_t)mt);
                                Toast(L"Sekme kapatildi");
                            }
                        }
                    } else if (m_view == View::Terminal) {
                        PasteClipboard();
                    }
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
                } else if (ev.xbutton.button == 2 || ev.xbutton.button == 3) {
                    // Win32 WM_MBUTTONUP / WM_RBUTTONUP
                    const int btn = (ev.xbutton.button == 2) ? 1 : 2;
                    if (m_mouseBtn == btn) {
                        int col, row;
                        CellFromPointClamped(mx, my, col, row);
                        m_mouseBtn = -1;
                        SendMouseReport(btn, false, false, col, row);
                        m_dirty = true;
                        break;
                    }
                    if (ev.xbutton.button == 3 && !DrawerUp()) {
                        POINT pt{ mx, my };
                        ClientToScreen(m_hwnd, &pt);
                        if (my < (int)m_lay.titleH) {
                            const int hit = HitTab(mx, my);
                            if (hit >= 0 && (size_t)hit < m_tabs.size()) {
                                ShowTabContextMenu((size_t)hit, pt);
                                m_dirty = true;
                                break;
                            }
                        }
                        if (m_view == View::Terminal && HitRect(m_lay.term, mx, my)) {
                            if (auto* tab = Active(); tab && tab->IsSftp()) ShowSftpContextMenu(pt, mx, my);
                            else ShowTerminalContextMenu(pt);
                        } else if (m_view == View::Sftp && HitRect(m_lay.main, mx, my)) {
                            ShowSftpContextMenu(pt, mx, my);
                        }
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
                LinuxUpdateCursor(mx, my);

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
                if (LinuxHandleXdnd(&ev)) break;
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

        // Caret blink timer simulation
        auto now = std::chrono::steady_clock::now();
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
