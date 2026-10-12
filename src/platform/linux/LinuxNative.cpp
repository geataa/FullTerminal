//
// FullTerminal — Linux (X11) yerel platform katmani. Ayrinti: LinuxNative.h
//
#include "platform/linux/LinuxNative.h"

#if !defined(_WIN32)
#include "platform/linux/AppIconData.h"
#include "core/Utf8.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

#include <dlfcn.h>
#include <poll.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ft::lnx {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

// ================================================================ durum ===

struct MainState {
    HWND hwnd = nullptr;
    Display* dpy = nullptr;
    Window win = 0;
    Window root = 0;
    int screen = 0;
    int cw = 0, ch = 0;             // istemci boyutu onbellegi
    bool hiddenByUs = false;        // ShowWindow(SW_HIDE) ile geri cekildi
    bool hidePosValid = false;
    int hideX = 0, hideY = 0;       // geri gosterilince ayni yere
    bool attention = false;
};
MainState g;

std::atomic<bool> g_bellPending{false};
std::atomic<int> g_attnPending{0};            // 1: dikkat iste, 2: temizle
std::atomic<unsigned long> g_modalFence{0};
std::recursive_mutex g_modalMutex;            // ayni anda tek modal kutu/menu
uint32_t g_accent = 0x3B82F6;

Atom At(Display* d, const char* n) { return XInternAtom(d, n, False); }
bool IsMain(HWND h) { return h && h == g.hwnd && g.dpy && g.win; }

void NoteFence(unsigned long t) {
    unsigned long cur = g_modalFence.load();
    while (t > cur && !g_modalFence.compare_exchange_weak(cur, t)) {}
}

unsigned long EventTime(const XEvent& ev) {
    switch (ev.type) {
    case KeyPress: case KeyRelease: return ev.xkey.time;
    case ButtonPress: case ButtonRelease: return ev.xbutton.time;
    case MotionNotify: return ev.xmotion.time;
    default: return 0;
    }
}

// ----------------------------------------------------------- X hatalari --
int g_trapCode = 0;
int TrapHandler(Display*, XErrorEvent* e) { g_trapCode = e->error_code; return 0; }

class ErrorTrap {
public:
    explicit ErrorTrap(Display* d) : m_d(d) {
        XSync(m_d, False);
        g_trapCode = 0;
        m_old = XSetErrorHandler(TrapHandler);
    }
    int Finish() {
        if (m_done) return g_trapCode;
        XSync(m_d, False);
        XSetErrorHandler(m_old);
        m_done = true;
        return g_trapCode;
    }
    ~ErrorTrap() { Finish(); }
private:
    Display* m_d;
    XErrorHandler m_old = nullptr;
    bool m_done = false;
};

// ------------------------------------------------------- ozellik yardimci --
std::vector<unsigned long> GetProp32(Display* d, Window w, Atom prop, Atom type, long maxLen = 1024) {
    std::vector<unsigned long> out;
    Atom at = 0;
    int af = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, w, prop, 0, maxLen, False, type, &at, &af, &n, &after, &data) == Success && data) {
        if (af == 32) {
            auto* p = reinterpret_cast<unsigned long*>(data);
            out.assign(p, p + n);
        }
        XFree(data);
    }
    return out;
}

bool HasNetState(Display* d, Window w, Atom st) {
    for (unsigned long a : GetProp32(d, w, At(d, "_NET_WM_STATE"), XA_ATOM)) {
        if ((Atom)a == st) return true;
    }
    return false;
}

void SendRootMessage(Display* d, Window w, Atom type, long l0, long l1 = 0, long l2 = 0, long l3 = 0, long l4 = 0) {
    XEvent e{};
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = type;
    e.xclient.format = 32;
    e.xclient.data.l[0] = l0;
    e.xclient.data.l[1] = l1;
    e.xclient.data.l[2] = l2;
    e.xclient.data.l[3] = l3;
    e.xclient.data.l[4] = l4;
    XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
}

void SetNetState(Display* d, Window w, bool add, Atom a1, Atom a2 = 0) {
    SendRootMessage(d, w, At(d, "_NET_WM_STATE"), add ? 1 : 0, (long)a1, (long)a2, 1);
}

long WmState(Display* d, Window w) {
    const Atom wmState = At(d, "WM_STATE");
    auto v = GetProp32(d, w, wmState, wmState, 2);
    return v.empty() ? -1 : (long)v[0];
}

bool HaveEwmhWm(Display* d) {
    return !GetProp32(d, DefaultRootWindow(d), At(d, "_NET_SUPPORTING_WM_CHECK"), XA_WINDOW, 1).empty();
}

void SetUtf8Name(Display* d, Window w, const std::string& name) {
    const Atom utf8 = At(d, "UTF8_STRING");
    XChangeProperty(d, w, At(d, "_NET_WM_NAME"), utf8, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(name.data()), (int)name.size());
    XStoreName(d, w, name.c_str());
}

void SetClassHint(Display* d, Window w) {
    XClassHint ch{};
    ch.res_name = const_cast<char*>("fullterminal");
    ch.res_class = const_cast<char*>("FullTerminal");
    XSetClassHint(d, w, &ch);
}

// ------------------------------------------------------------ monitorler --
std::vector<RECT> Monitors(Display* d) {
    std::vector<RECT> out;
    struct XinScreen { int screen_number; short x_org, y_org, width, height; };
    using IsActiveFn = int (*)(Display*);
    using QueryFn = XinScreen* (*)(Display*, int*);
    static void* lib = dlopen("libXinerama.so.1", RTLD_LAZY | RTLD_LOCAL);
    static IsActiveFn isActive = lib ? reinterpret_cast<IsActiveFn>(dlsym(lib, "XineramaIsActive")) : nullptr;
    static QueryFn query = lib ? reinterpret_cast<QueryFn>(dlsym(lib, "XineramaQueryScreens")) : nullptr;
    if (d && isActive && query && isActive(d)) {
        int n = 0;
        if (XinScreen* s = query(d, &n)) {
            for (int i = 0; i < n; ++i) {
                out.push_back(RECT{ s[i].x_org, s[i].y_org, s[i].x_org + s[i].width, s[i].y_org + s[i].height });
            }
            XFree(s);
        }
    }
    if (out.empty() && d) {
        const int sc = DefaultScreen(d);
        out.push_back(RECT{ 0, 0, DisplayWidth(d, sc), DisplayHeight(d, sc) });
    }
    return out;
}

int MonitorIndexFor(const std::vector<RECT>& ms, POINT p, DWORD flags) {
    for (size_t i = 0; i < ms.size(); ++i) {
        const RECT& r = ms[i];
        if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) return (int)i;
    }
    if (ms.empty() || flags == MONITOR_DEFAULTTONULL) return -1;
    if (flags == MONITOR_DEFAULTTOPRIMARY) return 0;
    long best = LONG_MAX;
    int bi = 0;
    for (size_t i = 0; i < ms.size(); ++i) {
        const RECT& r = ms[i];
        const long dx = p.x < r.left ? r.left - p.x : (p.x >= r.right ? p.x - r.right + 1 : 0);
        const long dy = p.y < r.top ? r.top - p.y : (p.y >= r.bottom ? p.y - r.bottom + 1 : 0);
        const long dd = dx * dx + dy * dy;
        if (dd < best) { best = dd; bi = (int)i; }
    }
    return bi;
}

RECT WorkAreaFor(Display* d, const RECT& mon) {
    const Window root = DefaultRootWindow(d);
    auto wa = GetProp32(d, root, At(d, "_NET_WORKAREA"), XA_CARDINAL, 4 * 64);
    long desk = 0;
    auto cur = GetProp32(d, root, At(d, "_NET_CURRENT_DESKTOP"), XA_CARDINAL, 1);
    if (!cur.empty()) desk = (long)cur[0];
    if (wa.size() >= (size_t)(desk + 1) * 4) {
        const long x = (long)wa[desk * 4], y = (long)wa[desk * 4 + 1];
        const long w = (long)wa[desk * 4 + 2], h = (long)wa[desk * 4 + 3];
        RECT r{ (int)std::max<long>(x, mon.left), (int)std::max<long>(y, mon.top),
                (int)std::min<long>(x + w, mon.right), (int)std::min<long>(y + h, mon.bottom) };
        if (r.right > r.left && r.bottom > r.top) return r;
    }
    return mon;
}

POINT QueryPointerPos(Display* d) {
    Window r = 0, c = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned int m = 0;
    POINT p{};
    if (XQueryPointer(d, DefaultRootWindow(d), &r, &c, &rx, &ry, &wx, &wy, &m)) { p.x = rx; p.y = ry; }
    return p;
}

// ============================================================ pikseller ===

struct Surface {
    int w = 0, h = 0;
    std::vector<uint32_t> px;   // 0x00RRGGBB, opak

    void Resize(int W, int H) {
        w = std::max(1, W);
        h = std::max(1, H);
        px.assign((size_t)w * h, 0);
    }
    void Fill(int x, int y, int ww, int hh, uint32_t rgb) {
        const int x0 = std::max(0, x), y0 = std::max(0, y);
        const int x1 = std::min(w, x + ww), y1 = std::min(h, y + hh);
        for (int yy = y0; yy < y1; ++yy) {
            uint32_t* row = &px[(size_t)yy * w];
            for (int xx = x0; xx < x1; ++xx) row[xx] = rgb;
        }
    }
    void Blend(int x, int y, uint32_t rgb, int a) {
        if (a <= 0 || x < 0 || y < 0 || x >= w || y >= h) return;
        uint32_t& d = px[(size_t)y * w + x];
        if (a >= 255) { d = rgb & 0xFFFFFF; return; }
        const int ia = 255 - a;
        const uint32_t r = (((rgb >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * ia) / 255;
        const uint32_t gg = (((rgb >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * ia) / 255;
        const uint32_t b = ((rgb & 0xFF) * a + (d & 0xFF) * ia) / 255;
        d = (r << 16) | (gg << 8) | b;
    }
    // on carpimli ARGB
    void BlendPremul(int x, int y, uint32_t argb) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const uint32_t a = argb >> 24;
        if (!a) return;
        uint32_t& d = px[(size_t)y * w + x];
        const uint32_t ia = 255 - a;
        const uint32_t r = std::min<uint32_t>(255, ((argb >> 16) & 0xFF) + ((d >> 16) & 0xFF) * ia / 255);
        const uint32_t gg = std::min<uint32_t>(255, ((argb >> 8) & 0xFF) + ((d >> 8) & 0xFF) * ia / 255);
        const uint32_t b = std::min<uint32_t>(255, (argb & 0xFF) + (d & 0xFF) * ia / 255);
        d = (r << 16) | (gg << 8) | b;
    }
    void RoundRect(int x, int y, int ww, int hh, float rad, uint32_t rgb) {
        rad = std::min(rad, std::min(ww, hh) / 2.0f);
        for (int yy = std::max(0, y); yy < std::min(h, y + hh); ++yy) {
            for (int xx = std::max(0, x); xx < std::min(w, x + ww); ++xx) {
                const float cx = xx + 0.5f, cy = yy + 0.5f;
                float dx = 0.0f, dy = 0.0f;
                if (cx < x + rad) dx = x + rad - cx; else if (cx > x + ww - rad) dx = cx - (x + ww - rad);
                if (cy < y + rad) dy = y + rad - cy; else if (cy > y + hh - rad) dy = cy - (y + hh - rad);
                if (dx > 0.0f && dy > 0.0f) {
                    const float cov = std::clamp(rad + 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
                    Blend(xx, yy, rgb, (int)(cov * 255.0f));
                } else {
                    px[(size_t)yy * w + xx] = rgb;
                }
            }
        }
    }
    void Circle(float cx, float cy, float rad, uint32_t rgb) {
        for (int yy = (int)(cy - rad - 1); yy <= (int)(cy + rad + 1); ++yy) {
            for (int xx = (int)(cx - rad - 1); xx <= (int)(cx + rad + 1); ++xx) {
                const float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy;
                const float cov = std::clamp(rad + 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
                Blend(xx, yy, rgb, (int)(cov * 255.0f));
            }
        }
    }
    // 4x4 ust ornekleme ile duzgun ucgen
    void Triangle(float ax, float ay, float bx, float by, float cx, float cy, uint32_t rgb) {
        auto edge = [](float x0, float y0, float x1, float y1, float x, float y) {
            return (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
        };
        const int minx = (int)std::floor(std::min({ ax, bx, cx })), maxx = (int)std::ceil(std::max({ ax, bx, cx }));
        const int miny = (int)std::floor(std::min({ ay, by, cy })), maxy = (int)std::ceil(std::max({ ay, by, cy }));
        const float area = edge(ax, ay, bx, by, cx, cy);
        if (std::fabs(area) < 0.01f) return;
        for (int yy = miny; yy <= maxy; ++yy) {
            for (int xx = minx; xx <= maxx; ++xx) {
                int hit = 0;
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        const float px_ = xx + (sx + 0.5f) / 4.0f, py_ = yy + (sy + 0.5f) / 4.0f;
                        const float w0 = edge(bx, by, cx, cy, px_, py_) / area;
                        const float w1 = edge(cx, cy, ax, ay, px_, py_) / area;
                        const float w2 = edge(ax, ay, bx, by, px_, py_) / area;
                        if (w0 >= 0 && w1 >= 0 && w2 >= 0) ++hit;
                    }
                }
                if (hit) Blend(xx, yy, rgb, hit * 255 / 16);
            }
        }
    }
};

unsigned long PackPixel(const Visual* v, uint32_t rgb) {
    auto chan = [](unsigned long mask, uint32_t c8) -> unsigned long {
        if (!mask) return 0;
        const int shift = __builtin_ctzl(mask);
        const int bits = __builtin_popcountl(mask);
        unsigned long c = c8;
        if (bits < 8) c >>= (8 - bits); else if (bits > 8) c <<= (bits - 8);
        return (c << shift) & mask;
    };
    return chan(v->red_mask, (rgb >> 16) & 0xFF) | chan(v->green_mask, (rgb >> 8) & 0xFF) | chan(v->blue_mask, rgb & 0xFF);
}

void PresentRgb(Display* d, Drawable dst, GC gc, Visual* vis, int depth,
                const uint32_t* px, int w, int h, int dx, int dy) {
    if (w <= 0 || h <= 0) return;
    const bool std888 = vis->red_mask == 0xFF0000 && vis->green_mask == 0xFF00 && vis->blue_mask == 0xFF &&
                        (depth == 24 || depth == 32);
    if (std888) {
        std::vector<uint32_t> buf(px, px + (size_t)w * h);
        if (depth == 32) for (auto& p : buf) p |= 0xFF000000u;
        XImage* img = XCreateImage(d, vis, depth, ZPixmap, 0, reinterpret_cast<char*>(buf.data()), w, h, 32, 0);
        if (!img) return;
        XPutImage(d, dst, gc, img, 0, 0, dx, dy, w, h);
        img->data = nullptr;
        XDestroyImage(img);
        return;
    }
    XImage* img = XCreateImage(d, vis, depth, ZPixmap, 0, nullptr, w, h, 32, 0);
    if (!img) return;
    img->data = static_cast<char*>(malloc((size_t)img->bytes_per_line * h));
    if (!img->data) { XDestroyImage(img); return; }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) XPutPixel(img, x, y, PackPixel(vis, px[(size_t)y * w + x]));
    XPutImage(d, dst, gc, img, 0, 0, dx, dy, w, h);
    XDestroyImage(img);
}

void Present(Display* d, Window w, GC gc, const Surface& s) {
    const int sc = DefaultScreen(d);
    PresentRgb(d, w, gc, DefaultVisual(d, sc), DefaultDepth(d, sc), s.px.data(), s.w, s.h, 0, 0);
}

// On carpimli ARGB dikdortgen olcekleme (alan ortalamasi / en yakin komsu)
std::vector<uint32_t> ScalePremul(const std::vector<uint32_t>& src, int sw, int sh, int dw, int dh) {
    std::vector<uint32_t> out((size_t)dw * dh, 0);
    if (sw <= 0 || sh <= 0) return out;
    for (int y = 0; y < dh; ++y) {
        int sy0 = (int)((long)y * sh / dh), sy1 = (int)((long)(y + 1) * sh / dh);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; ++x) {
            int sx0 = (int)((long)x * sw / dw), sx1 = (int)((long)(x + 1) * sw / dw);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            uint64_t a = 0, r = 0, gg = 0, b = 0, n = 0;
            for (int yy = sy0; yy < sy1 && yy < sh; ++yy) {
                for (int xx = sx0; xx < sx1 && xx < sw; ++xx) {
                    const uint32_t p = src[(size_t)yy * sw + xx];
                    a += p >> 24; r += (p >> 16) & 0xFF; gg += (p >> 8) & 0xFF; b += p & 0xFF; ++n;
                }
            }
            if (!n) continue;
            out[(size_t)y * dw + x] = (uint32_t)((a / n) << 24 | (r / n) << 16 | (gg / n) << 8 | (b / n));
        }
    }
    return out;
}

std::vector<uint32_t> Premultiply(const uint32_t* src, size_t n) {
    std::vector<uint32_t> out(n);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t p = src[i], a = p >> 24;
        out[i] = (a << 24) | ((((p >> 16) & 0xFF) * a / 255) << 16) | ((((p >> 8) & 0xFF) * a / 255) << 8) | ((p & 0xFF) * a / 255);
    }
    return out;
}

std::vector<uint32_t> Unpremultiply(const std::vector<uint32_t>& src) {
    std::vector<uint32_t> out(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        const uint32_t p = src[i], a = p >> 24;
        if (!a) { out[i] = 0; continue; }
        out[i] = (a << 24) | (std::min<uint32_t>(255, ((p >> 16) & 0xFF) * 255 / a) << 16) |
                 (std::min<uint32_t>(255, ((p >> 8) & 0xFF) * 255 / a) << 8) | std::min<uint32_t>(255, (p & 0xFF) * 255 / a);
    }
    return out;
}

// ============================================================== metin ===
//
// Fontconfig "sans-serif" siralamasi + karakter bazli yedek font. Renkli
// bitmap emoji fontlari (Noto Color Emoji) FT_LOAD_COLOR ile olceklenir.

struct Glyph {
    int w = 0, h = 0, left = 0, top = 0, adv = 0;
    bool color = false;
    std::vector<uint8_t> a;        // gri kaplama
    std::vector<uint32_t> argb;    // renkli, on carpimli
};

class TextRaster {
public:
    TextRaster() = default;
    TextRaster(const TextRaster&) = delete;
    TextRaster& operator=(const TextRaster&) = delete;
    ~TextRaster() {
        for (auto& e : m_fonts) {
            if (e.face) FT_Done_Face(e.face);
            if (e.cs) FcCharSetDestroy(e.cs);
        }
        if (m_lib) FT_Done_FreeType(m_lib);
    }

    bool Init(int px) {
        m_px = std::max(6, px);
        if (FT_Init_FreeType(&m_lib) != 0) { m_lib = nullptr; return false; }
        if (!FcInit()) return false;
        FcPattern* pat = FcNameParse(reinterpret_cast<const FcChar8*>("sans-serif"));
        if (!pat) return false;
        FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double)m_px);
        FcConfigSubstitute(nullptr, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult res = FcResultNoMatch;
        if (FcFontSet* set = FcFontSort(nullptr, pat, FcTrue, nullptr, &res)) {
            for (int i = 0; i < set->nfont && m_fonts.size() < 96; ++i) {
                FcChar8* file = nullptr;
                int idx = 0;
                FcCharSet* cs = nullptr;
                if (FcPatternGetString(set->fonts[i], FC_FILE, 0, &file) != FcResultMatch || !file) continue;
                FcPatternGetInteger(set->fonts[i], FC_INDEX, 0, &idx);
                FontEntry e;
                e.file = reinterpret_cast<const char*>(file);
                e.index = idx;
                if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &cs) == FcResultMatch && cs) e.cs = FcCharSetCopy(cs);
                m_fonts.push_back(e);
            }
            FcFontSetDestroy(set);
        }
        FcPatternDestroy(pat);
        for (auto& e : m_fonts) {
            if (Load(e) && !e.color && FT_IS_SCALABLE(e.face)) {
                m_asc = (int)std::ceil(e.face->size->metrics.ascender / 64.0);
                m_desc = (int)std::ceil(-e.face->size->metrics.descender / 64.0);
                break;
            }
        }
        return !m_fonts.empty();
    }

    int Ascent() const { return m_asc; }
    int Height() const { return m_asc + m_desc; }

    int Measure(const std::wstring& s) {
        int w = 0;
        for (wchar_t c : s) w += Get((char32_t)c).adv;
        return w;
    }

    void Draw(Surface& surf, int x, int baseline, const std::wstring& s, uint32_t rgb, int clipR = INT_MAX) {
        int pen = x;
        for (wchar_t c : s) {
            const Glyph& gl = Get((char32_t)c);
            if (pen + gl.adv > clipR) break;
            const int ox = pen + gl.left, oy = baseline - gl.top;
            if (gl.color) {
                for (int yy = 0; yy < gl.h; ++yy)
                    for (int xx = 0; xx < gl.w; ++xx) surf.BlendPremul(ox + xx, oy + yy, gl.argb[(size_t)yy * gl.w + xx]);
            } else {
                for (int yy = 0; yy < gl.h; ++yy)
                    for (int xx = 0; xx < gl.w; ++xx) surf.Blend(ox + xx, oy + yy, rgb, gl.a[(size_t)yy * gl.w + xx]);
            }
            pen += gl.adv;
        }
    }

private:
    struct FontEntry {
        std::string file;
        int index = 0;
        FcCharSet* cs = nullptr;
        FT_Face face = nullptr;
        bool tried = false;
        bool color = false;
        float scale = 1.0f;
    };

    bool Load(FontEntry& e) {
        if (e.tried) return e.face != nullptr;
        e.tried = true;
        if (FT_New_Face(m_lib, e.file.c_str(), e.index, &e.face) != 0) { e.face = nullptr; return false; }
        e.color = FT_HAS_COLOR(e.face);
        if (FT_IS_SCALABLE(e.face)) {
            FT_Set_Pixel_Sizes(e.face, 0, (FT_UInt)m_px);
            e.scale = 1.0f;
            return true;
        }
        if (e.face->num_fixed_sizes > 0) {
            int best = 0;
            long bestPpem = 0;
            for (int i = 0; i < e.face->num_fixed_sizes; ++i) {
                const long ppem = e.face->available_sizes[i].y_ppem >> 6;
                const bool better = bestPpem == 0 ||
                    (ppem >= m_px && (bestPpem < m_px || ppem < bestPpem)) ||
                    (bestPpem < m_px && ppem > bestPpem);
                if (better) { best = i; bestPpem = ppem; }
            }
            if (FT_Select_Size(e.face, best) == 0 && bestPpem > 0) {
                e.scale = (float)m_px / (float)bestPpem;
                return true;
            }
        }
        FT_Done_Face(e.face);
        e.face = nullptr;
        return false;
    }

    FontEntry* FaceFor(char32_t cp) {
        for (auto& e : m_fonts) {
            if (e.cs && !FcCharSetHasChar(e.cs, cp)) continue;
            if (!Load(e)) continue;
            if (FT_Get_Char_Index(e.face, cp)) return &e;
        }
        return nullptr;
    }

    const Glyph& Get(char32_t cp) {
        auto it = m_cache.find(cp);
        if (it != m_cache.end()) return it->second;
        Glyph gl;
        char32_t use = cp;
        FontEntry* fe = FaceFor(cp);
        if (!fe && cp >= 0x20) { use = 0xFFFD; fe = FaceFor(use); }
        if (!fe && cp >= 0x20) { use = U'?'; fe = FaceFor(use); }
        if (fe) {
            FT_Int32 flags = FT_LOAD_DEFAULT | FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT;
            if (fe->color) flags |= FT_LOAD_COLOR;
            if (FT_Load_Glyph(fe->face, FT_Get_Char_Index(fe->face, use), flags) == 0) {
                FT_GlyphSlot s = fe->face->glyph;
                const FT_Bitmap& b = s->bitmap;
                const float sc = fe->scale;
                if (b.pixel_mode == FT_PIXEL_MODE_BGRA) {
                    std::vector<uint32_t> src((size_t)b.width * b.rows);
                    for (unsigned y = 0; y < b.rows; ++y) {
                        const uint8_t* row = b.buffer + (long)y * b.pitch;
                        for (unsigned x = 0; x < b.width; ++x) {
                            const uint8_t* p = row + x * 4;
                            src[(size_t)y * b.width + x] = ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
                        }
                    }
                    const int dw = std::max(1, (int)std::lround(b.width * sc));
                    const int dh = std::max(1, (int)std::lround(b.rows * sc));
                    gl.argb = (sc == 1.0f) ? src : ScalePremul(src, (int)b.width, (int)b.rows, dw, dh);
                    gl.w = (sc == 1.0f) ? (int)b.width : dw;
                    gl.h = (sc == 1.0f) ? (int)b.rows : dh;
                    gl.color = true;
                } else {
                    std::vector<uint8_t> a((size_t)b.width * b.rows, 0);
                    for (unsigned y = 0; y < b.rows; ++y) {
                        const uint8_t* row = b.buffer + (long)y * b.pitch;
                        for (unsigned x = 0; x < b.width; ++x) {
                            if (b.pixel_mode == FT_PIXEL_MODE_MONO) a[(size_t)y * b.width + x] = (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
                            else a[(size_t)y * b.width + x] = row[x];
                        }
                    }
                    if (sc != 1.0f && b.width && b.rows) {
                        std::vector<uint32_t> tmp(a.size());
                        for (size_t i = 0; i < a.size(); ++i) tmp[i] = (uint32_t)a[i] << 24;
                        const int dw = std::max(1, (int)std::lround(b.width * sc));
                        const int dh = std::max(1, (int)std::lround(b.rows * sc));
                        auto sc2 = ScalePremul(tmp, (int)b.width, (int)b.rows, dw, dh);
                        gl.a.resize(sc2.size());
                        for (size_t i = 0; i < sc2.size(); ++i) gl.a[i] = (uint8_t)(sc2[i] >> 24);
                        gl.w = dw; gl.h = dh;
                    } else {
                        gl.a = std::move(a);
                        gl.w = (int)b.width;
                        gl.h = (int)b.rows;
                    }
                }
                gl.left = (int)std::lround(s->bitmap_left * sc);
                gl.top = (int)std::lround(s->bitmap_top * sc);
                gl.adv = (int)std::lround(s->advance.x / 64.0 * sc);
                // Renkli emoji ust cizgiyi asmasin: govdeyi metin yuksekligine ortala
                if (gl.color) {
                    const int body = m_asc + m_desc;
                    gl.top = m_asc - std::max(0, (body - gl.h) / 2);
                }
            }
        }
        if (gl.adv == 0 && cp >= 0x20 && cp != 0x200B && cp != 0xFE0F) gl.adv = m_px / 2;
        return m_cache.emplace(cp, std::move(gl)).first->second;
    }

    FT_Library m_lib = nullptr;
    std::vector<FontEntry> m_fonts;
    std::unordered_map<char32_t, Glyph> m_cache;
    int m_px = 13, m_asc = 10, m_desc = 3;
};

// ============================================================ etiketler ===

enum Lbl { L_OK, L_CANCEL, L_YES, L_NO, L_ABORT, L_RETRY, L_IGNORE,
           L_RESTORE, L_MOVE, L_SIZE, L_MINIMIZE, L_MAXIMIZE, L_CLOSE, L_COUNT };

// Win32 dugme metinleri isletim sistemi dilini izler; burada da LANG/LC_*.
const wchar_t* Label(Lbl l) {
    static const int lang = [] {
        const char* v = getenv("LC_ALL");
        if (!v || !*v) v = getenv("LC_MESSAGES");
        if (!v || !*v) v = getenv("LANG");
        const std::string s = v ? v : "";
        if (s.rfind("tr", 0) == 0) return 1;
        if (s.rfind("de", 0) == 0) return 2;
        if (s.rfind("fr", 0) == 0) return 3;
        if (s.rfind("es", 0) == 0) return 4;
        if (s.rfind("ru", 0) == 0 || s.rfind("uk", 0) == 0) return 5;
        return 0;
    }();
    static const wchar_t* T[6][L_COUNT] = {
        { L"OK", L"Cancel", L"Yes", L"No", L"Abort", L"Retry", L"Ignore",
          L"Restore", L"Move", L"Size", L"Minimize", L"Maximize", L"Close" },
        { L"Tamam", L"\u0130ptal", L"Evet", L"Hay\u0131r", L"Durdur", L"Yeniden Dene", L"Yoksay",
          L"Geri Y\u00fckle", L"Ta\u015f\u0131", L"Boyut", L"Simge Durumuna K\u00fc\u00e7\u00fclt", L"Ekran\u0131 Kapla", L"Kapat" },
        { L"OK", L"Abbrechen", L"Ja", L"Nein", L"Abbrechen", L"Wiederholen", L"Ignorieren",
          L"Wiederherstellen", L"Verschieben", L"Gr\u00f6\u00dfe \u00e4ndern", L"Minimieren", L"Maximieren", L"Schlie\u00dfen" },
        { L"OK", L"Annuler", L"Oui", L"Non", L"Abandonner", L"R\u00e9essayer", L"Ignorer",
          L"Restaurer", L"D\u00e9placer", L"Taille", L"R\u00e9duire", L"Agrandir", L"Fermer" },
        { L"Aceptar", L"Cancelar", L"S\u00ed", L"No", L"Anular", L"Reintentar", L"Omitir",
          L"Restaurar", L"Mover", L"Tama\u00f1o", L"Minimizar", L"Maximizar", L"Cerrar" },
        { L"\u041e\u041a", L"\u041e\u0442\u043c\u0435\u043d\u0430", L"\u0414\u0430", L"\u041d\u0435\u0442",
          L"\u041f\u0440\u0435\u0440\u0432\u0430\u0442\u044c", L"\u041f\u043e\u0432\u0442\u043e\u0440",
          L"\u041f\u0440\u043e\u043f\u0443\u0441\u0442\u0438\u0442\u044c",
          L"\u0412\u043e\u0441\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c",
          L"\u041f\u0435\u0440\u0435\u043c\u0435\u0441\u0442\u0438\u0442\u044c", L"\u0420\u0430\u0437\u043c\u0435\u0440",
          L"\u0421\u0432\u0435\u0440\u043d\u0443\u0442\u044c", L"\u0420\u0430\u0437\u0432\u0435\u0440\u043d\u0443\u0442\u044c",
          L"\u0417\u0430\u043a\u0440\u044b\u0442\u044c" },
    };
    return T[lang][l];
}

// "&Kopyala" -> "Kopyala", "&&" -> "&"
std::wstring StripAmp(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'&') {
            if (i + 1 < s.size() && s[i + 1] == L'&') { out += L'&'; ++i; }
            continue;
        }
        out += s[i];
    }
    return out;
}

float ScaleFor(Display* d) { return std::max(1.0f, SystemDpi(d) / 96.0f); }

void SetDialogIcon(Display* d, Window w) {
    std::vector<unsigned long> data;
    for (int sz : { 16, 32, 48 }) {
        auto px = ScaleArgb(kAppIconArgb, kAppIconSize, sz);
        data.push_back((unsigned long)sz);
        data.push_back((unsigned long)sz);
        for (uint32_t p : px) data.push_back(p);
    }
    XChangeProperty(d, w, At(d, "_NET_WM_ICON"), XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(data.data()), (int)data.size());
}

bool GrabWithRetry(Display* d, Window w, bool keyboard) {
    const auto until = Clock::now() + 2s;
    for (;;) {
        int r;
        if (keyboard) r = XGrabKeyboard(d, w, False, GrabModeAsync, GrabModeAsync, CurrentTime);
        else r = XGrabPointer(d, w, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                              GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        if (r == GrabSuccess) return true;
        if (Clock::now() > until) return false;
        usleep(10000);
    }
}

// =========================================================== acilir menu ===

class PopupSession {
public:
    PopupSession(Display* d) : m_d(d) {
        m_scr = DefaultScreen(d);
        m_root = RootWindow(d, m_scr);
        m_s = ScaleFor(d);
        m_font.Init((int)std::lround(13.0f * m_s));
        m_gc = XCreateGC(d, m_root, 0, nullptr);
        m_rowH = (int)std::lround(28 * m_s);
        m_sepH = (int)std::lround(9 * m_s);
        m_pad = (int)std::lround(5 * m_s);
        m_checkW = (int)std::lround(30 * m_s);
        m_arrowW = (int)std::lround(22 * m_s);
        m_gap = (int)std::lround(36 * m_s);
    }
    ~PopupSession() {
        CloseFrom(0);
        if (m_gc) XFreeGC(m_d, m_gc);
    }

    UINT Run(LinuxMenu* menu, int x, int y, UINT flags) {
        if (!menu || menu->items.empty()) return 0;
        OpenRoot(menu, x, y, flags);
        if (m_stack.empty()) return 0;
        XSync(m_d, False);
        const Window gw = m_stack[0]->win;
        const bool ptr = GrabWithRetry(m_d, gw, false);
        GrabWithRetry(m_d, gw, true);
        (void)ptr;

        const auto opened = Clock::now();
        bool pressedInside = false;
        UINT result = 0;
        bool done = false;
        int pendLvl = -1, pendRow = -1;
        Clock::time_point pendAt{};

        while (!done) {
            while (!done && XPending(m_d) > 0) {
                XEvent ev;
                XNextEvent(m_d, &ev);
                if (unsigned long t = EventTime(ev)) m_lastTime = t;
                switch (ev.type) {
                case Expose:
                    if (ev.xexpose.count == 0) {
                        for (auto& mw : m_stack) if (mw->win == ev.xexpose.window) Draw(*mw);
                    }
                    break;
                case MotionNotify: {
                    const int lvl = HitLevel(ev.xmotion.x_root, ev.xmotion.y_root);
                    if (lvl >= 0) {
                        MenuWin& mw = *m_stack[lvl];
                        const int row = RowAt(mw, ev.xmotion.y_root - mw.y);
                        if (row != mw.hover) {
                            mw.hover = row;
                            if (mw.openSub >= 0 && row != mw.openSub && row >= 0) {
                                CloseFrom((size_t)lvl + 1);
                                mw.openSub = -1;
                            }
                            Draw(mw);
                            if (row >= 0 && mw.rows[row].sub && mw.rows[row].enabled && mw.openSub != row) {
                                pendLvl = lvl; pendRow = row; pendAt = Clock::now();
                            } else {
                                pendLvl = -1;
                            }
                        }
                    } else {
                        MenuWin& top = *m_stack.back();
                        if (top.hover >= 0 && top.openSub < 0) { top.hover = -1; Draw(top); }
                        pendLvl = -1;
                    }
                    break;
                }
                case ButtonPress: {
                    if (ev.xbutton.button > 3) break;
                    const int lvl = HitLevel(ev.xbutton.x_root, ev.xbutton.y_root);
                    if (lvl < 0) { done = true; break; }
                    pressedInside = true;
                    MenuWin& mw = *m_stack[lvl];
                    const int row = RowAt(mw, ev.xbutton.y_root - mw.y);
                    if (row >= 0 && mw.rows[row].sub && mw.rows[row].enabled) OpenSub((size_t)lvl, row, false);
                    break;
                }
                case ButtonRelease: {
                    if (ev.xbutton.button > 3) break;
                    const int lvl = HitLevel(ev.xbutton.x_root, ev.xbutton.y_root);
                    if (lvl < 0) break;
                    // Menuyu acan tikin birakilmasi hemen bir oge secmesin
                    if (!pressedInside && Clock::now() - opened < 350ms) break;
                    MenuWin& mw = *m_stack[lvl];
                    const int row = RowAt(mw, ev.xbutton.y_root - mw.y);
                    if (row >= 0) {
                        const MenuRow& r = mw.rows[row];
                        if (!r.sep && r.enabled && !r.sub) { result = (UINT)r.id; done = true; }
                    }
                    break;
                }
                case KeyPress: {
                    const KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    const size_t lvl = m_stack.size() - 1;
                    MenuWin& top = *m_stack.back();
                    switch (ks) {
                    case XK_Escape:
                        if (m_stack.size() > 1) { CloseFrom(lvl); m_stack.back()->openSub = -1; Draw(*m_stack.back()); }
                        else done = true;
                        break;
                    case XK_Up: case XK_KP_Up: MoveHover(top, -1); break;
                    case XK_Down: case XK_KP_Down: MoveHover(top, +1); break;
                    case XK_Home: top.hover = -1; MoveHover(top, +1); break;
                    case XK_End: top.hover = (int)top.rows.size(); MoveHover(top, -1); break;
                    case XK_Right: case XK_KP_Right:
                        if (top.hover >= 0 && top.rows[top.hover].sub && top.rows[top.hover].enabled) OpenSub(lvl, top.hover, true);
                        break;
                    case XK_Left: case XK_KP_Left:
                        if (m_stack.size() > 1) { CloseFrom(lvl); m_stack.back()->openSub = -1; Draw(*m_stack.back()); }
                        break;
                    case XK_Return: case XK_KP_Enter: case XK_space:
                        if (top.hover >= 0) {
                            const MenuRow& r = top.rows[top.hover];
                            if (r.sub && r.enabled) OpenSub(lvl, top.hover, true);
                            else if (!r.sep && r.enabled) { result = (UINT)r.id; done = true; }
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                }
                default:
                    break;
                }
            }
            if (done) break;
            if (pendLvl >= 0 && Clock::now() - pendAt >= 220ms) {
                if (pendLvl < (int)m_stack.size() && m_stack[pendLvl]->hover == pendRow) OpenSub((size_t)pendLvl, pendRow, false);
                pendLvl = -1;
            }
            pollfd p{ ConnectionNumber(m_d), POLLIN, 0 };
            poll(&p, 1, 30);
        }

        XUngrabPointer(m_d, CurrentTime);
        XUngrabKeyboard(m_d, CurrentTime);
        CloseFrom(0);
        XSync(m_d, False);
        NoteFence(m_lastTime);
        return result;
    }

private:
    struct MenuRow {
        int y = 0, h = 0;
        bool sep = false, enabled = true, checked = false, sub = false;
        UINT_PTR id = 0;
        std::wstring label, accel;
    };
    struct MenuWin {
        LinuxMenu* menu = nullptr;
        Window win = 0;
        int x = 0, y = 0, w = 0, h = 0;
        std::vector<MenuRow> rows;
        int hover = -1;
        int openSub = -1;
        Surface surf;
    };

    void Layout(MenuWin& mw) {
        int labelW = 0, accelW = 0, y = m_pad;
        bool anySub = false;
        for (const auto& it : mw.menu->items) {
            MenuRow r;
            r.sep = (it.flags & MF_SEPARATOR) != 0;
            r.enabled = (it.flags & MF_GRAYED) == 0;
            r.checked = (it.flags & MF_CHECKED) != 0;
            r.sub = (it.flags & MF_POPUP) != 0;
            r.id = it.id;
            if (!r.sep) {
                const std::wstring t = StripAmp(it.text);
                const size_t tab = t.find(L'\t');
                r.label = t.substr(0, tab);
                if (tab != std::wstring::npos) r.accel = t.substr(tab + 1);
                labelW = std::max(labelW, m_font.Measure(r.label));
                if (!r.accel.empty()) accelW = std::max(accelW, m_font.Measure(r.accel));
                anySub = anySub || r.sub;
            }
            r.y = y;
            r.h = r.sep ? m_sepH : m_rowH;
            y += r.h;
            mw.rows.push_back(std::move(r));
        }
        mw.h = y + m_pad;
        mw.w = m_checkW + labelW + (accelW ? m_gap + accelW : 0) + m_arrowW + (anySub ? 0 : (int)(4 * m_s));
        mw.w = std::max(mw.w, (int)std::lround(150 * m_s));
        m_accelW = accelW;
    }

    void Create(MenuWin& mw) {
        XSetWindowAttributes a{};
        a.override_redirect = True;
        a.save_under = True;
        a.background_pixel = PackPixel(DefaultVisual(m_d, m_scr), 0x2B2B2B);
        a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask;
        mw.win = XCreateWindow(m_d, m_root, mw.x, mw.y, (unsigned)mw.w, (unsigned)mw.h, 0, CopyFromParent,
                               InputOutput, CopyFromParent, CWOverrideRedirect | CWSaveUnder | CWBackPixel | CWEventMask, &a);
        const Atom type = At(m_d, "_NET_WM_WINDOW_TYPE");
        Atom popup = At(m_d, "_NET_WM_WINDOW_TYPE_POPUP_MENU");
        XChangeProperty(m_d, mw.win, type, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&popup), 1);
        SetClassHint(m_d, mw.win);
        XMapRaised(m_d, mw.win);
    }

    void OpenRoot(LinuxMenu* menu, int x, int y, UINT flags) {
        auto mw = std::make_unique<MenuWin>();
        mw->menu = menu;
        Layout(*mw);
        if (flags & 0x0008L) x -= mw->w;            // TPM_RIGHTALIGN
        else if (flags & 0x0004L) x -= mw->w / 2;   // TPM_CENTERALIGN
        if (flags & 0x0020L) y -= mw->h;            // TPM_BOTTOMALIGN
        else if (flags & 0x0010L) y -= mw->h / 2;   // TPM_VCENTERALIGN
        const auto mons = Monitors(m_d);
        const int mi = MonitorIndexFor(mons, POINT{ x, y }, MONITOR_DEFAULTTONEAREST);
        const RECT m = mi >= 0 ? mons[mi] : RECT{ 0, 0, 1920, 1080 };
        if (x + mw->w > m.right) x = std::max<int>(m.left, (flags & 0x0008L) ? m.right - mw->w : x - mw->w);
        if (y + mw->h > m.bottom) y = std::max<int>(m.top, y - mw->h);
        x = std::max<int>(m.left, x);
        y = std::max<int>(m.top, y);
        mw->x = x;
        mw->y = y;
        Create(*mw);
        m_stack.push_back(std::move(mw));
    }

    void OpenSub(size_t lvl, int row, bool selectFirst) {
        MenuWin& parent = *m_stack[lvl];
        if (parent.openSub == row && m_stack.size() > lvl + 1) return;
        CloseFrom(lvl + 1);
        auto* sub = reinterpret_cast<LinuxMenu*>(parent.rows[row].id);
        if (!sub || sub->items.empty()) return;
        auto mw = std::make_unique<MenuWin>();
        mw->menu = sub;
        Layout(*mw);
        const auto mons = Monitors(m_d);
        const int mi = MonitorIndexFor(mons, POINT{ parent.x, parent.y }, MONITOR_DEFAULTTONEAREST);
        const RECT m = mi >= 0 ? mons[mi] : RECT{ 0, 0, 1920, 1080 };
        int x = parent.x + parent.w - (int)(3 * m_s);
        int y = parent.y + parent.rows[row].y - m_pad;
        if (x + mw->w > m.right) x = std::max<int>(m.left, parent.x - mw->w + (int)(3 * m_s));
        if (y + mw->h > m.bottom) y = std::max<int>(m.top, m.bottom - mw->h);
        mw->x = x;
        mw->y = y;
        parent.openSub = row;
        parent.hover = row;
        Draw(parent);
        Create(*mw);
        if (selectFirst) MoveHover(*mw, +1, false);
        m_stack.push_back(std::move(mw));
    }

    void CloseFrom(size_t lvl) {
        while (m_stack.size() > lvl) {
            if (m_stack.back()->win) XDestroyWindow(m_d, m_stack.back()->win);
            m_stack.pop_back();
        }
        if (!m_stack.empty() && lvl > 0 && lvl <= m_stack.size()) m_stack[lvl - 1]->openSub = -1;
    }

    int HitLevel(int rx, int ry) const {
        for (int i = (int)m_stack.size() - 1; i >= 0; --i) {
            const MenuWin& mw = *m_stack[i];
            if (rx >= mw.x && rx < mw.x + mw.w && ry >= mw.y && ry < mw.y + mw.h) return i;
        }
        return -1;
    }

    int RowAt(const MenuWin& mw, int ly) const {
        for (size_t i = 0; i < mw.rows.size(); ++i) {
            const MenuRow& r = mw.rows[i];
            if (ly >= r.y && ly < r.y + r.h) return r.sep ? -1 : (int)i;
        }
        return -1;
    }

    void MoveHover(MenuWin& mw, int dir, bool redraw = true) {
        const int n = (int)mw.rows.size();
        if (!n) return;
        int i = mw.hover;
        for (int k = 0; k < n; ++k) {
            i += dir;
            if (i < 0) i = n - 1;
            if (i >= n) i = 0;
            if (!mw.rows[i].sep && mw.rows[i].enabled) { mw.hover = i; break; }
        }
        if (redraw) Draw(mw);
    }

    void Draw(MenuWin& mw) {
        Surface& s = mw.surf;
        s.Resize(mw.w, mw.h);
        s.Fill(0, 0, mw.w, mw.h, 0x4A4A4A);
        s.Fill(1, 1, mw.w - 2, mw.h - 2, 0x2B2B2B);
        int accelW = 0;
        for (const auto& r : mw.rows) if (!r.accel.empty()) accelW = std::max(accelW, m_font.Measure(r.accel));
        for (size_t i = 0; i < mw.rows.size(); ++i) {
            const MenuRow& r = mw.rows[i];
            if (r.sep) {
                s.Fill(m_pad + (int)(6 * m_s), r.y + r.h / 2, mw.w - 2 * m_pad - (int)(12 * m_s), std::max(1, (int)m_s), 0x484848);
                continue;
            }
            if ((int)i == mw.hover && r.enabled) {
                s.RoundRect(m_pad, r.y + 1, mw.w - 2 * m_pad, r.h - 2, 4.0f * m_s, 0x3E3E42);
            }
            const uint32_t fg = r.enabled ? 0xF0F0F0 : 0x707070;
            const int base = r.y + (r.h - m_font.Height()) / 2 + m_font.Ascent();
            if (r.checked) m_font.Draw(s, (int)(10 * m_s), base, L"\u2713", fg);
            const int textR = mw.w - m_arrowW - (accelW ? accelW + m_gap / 2 : 0);
            m_font.Draw(s, m_checkW, base, r.label, fg, textR);
            if (!r.accel.empty()) {
                const int aw = m_font.Measure(r.accel);
                m_font.Draw(s, mw.w - m_arrowW - aw, base, r.accel, r.enabled ? 0xA6A6A6 : 0x5C5C5C);
            }
            if (r.sub) m_font.Draw(s, mw.w - m_arrowW + (int)(6 * m_s), base, L"\u203A", fg);
        }
        Present(m_d, mw.win, m_gc, s);
        XFlush(m_d);
    }

    Display* m_d;
    int m_scr = 0;
    Window m_root = 0;
    float m_s = 1.0f;
    TextRaster m_font;
    GC m_gc = nullptr;
    int m_rowH = 28, m_sepH = 9, m_pad = 5, m_checkW = 30, m_arrowW = 22, m_gap = 36, m_accelW = 0;
    unsigned long m_lastTime = 0;
    std::vector<std::unique_ptr<MenuWin>> m_stack;
};

UINT H_TrackPopup(HMENU menu, UINT flags, int x, int y, HWND owner) {
    std::lock_guard<std::recursive_mutex> lk(g_modalMutex);
    Display* d = XOpenDisplay(nullptr);
    if (!d) return 0;
    UINT r = 0;
    {
        PopupSession ps(d);
        r = ps.Run(static_cast<LinuxMenu*>(menu), x, y, flags);
    }
    XCloseDisplay(d);
    if (r && !(flags & TPM_RETURNCMD) && owner) {
        PostMessageW(owner, WM_COMMAND, (WPARAM)r, 0);
        return TRUE;
    }
    return r;
}

// ============================================================ mesaj kutusu ===

class MessageBoxSession {
public:
    MessageBoxSession(Display* d, HWND owner, std::wstring text, std::wstring caption, UINT type)
        : m_d(d), m_owner(owner), m_text(std::move(text)), m_caption(std::move(caption)), m_type(type) {
        m_scr = DefaultScreen(d);
        m_root = RootWindow(d, m_scr);
        m_s = ScaleFor(d);
        m_font.Init((int)std::lround(14.0f * m_s));
    }

    int Run() {
        BuildButtons();
        LayoutAll();
        CreateWindow();
        int result = 0;
        bool done = false;
        while (!done) {
            while (!done && XPending(m_d) > 0) {
                XEvent ev;
                XNextEvent(m_d, &ev);
                if (unsigned long t = EventTime(ev)) m_lastTime = t;
                switch (ev.type) {
                case MapNotify:
                    Activate();
                    break;
                case Expose:
                    if (ev.xexpose.count == 0) Draw();
                    break;
                case MotionNotify: {
                    const int b = ButtonAt(ev.xmotion.x, ev.xmotion.y);
                    if (b != m_hover) { m_hover = b; Draw(); }
                    break;
                }
                case LeaveNotify:
                    if (m_hover >= 0) { m_hover = -1; Draw(); }
                    break;
                case ButtonPress:
                    if (ev.xbutton.button == 1) {
                        m_pressed = ButtonAt(ev.xbutton.x, ev.xbutton.y);
                        if (m_pressed >= 0) { m_focus = m_pressed; Draw(); }
                    }
                    break;
                case ButtonRelease:
                    if (ev.xbutton.button == 1) {
                        const int b = ButtonAt(ev.xbutton.x, ev.xbutton.y);
                        if (b >= 0 && b == m_pressed) { result = m_btns[b].id; done = true; }
                        m_pressed = -1;
                        Draw();
                    }
                    break;
                case KeyPress: {
                    const KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    const bool shift = (ev.xkey.state & ShiftMask) != 0;
                    if (ks == XK_Escape) { if (m_escId) { result = m_escId; done = true; } }
                    else if (ks == XK_Return || ks == XK_KP_Enter || ks == XK_space) { result = m_btns[m_focus].id; done = true; }
                    else if (ks == XK_Tab || ks == XK_ISO_Left_Tab || ks == XK_Right || ks == XK_Left) {
                        const int n = (int)m_btns.size();
                        const bool back = ks == XK_Left || ks == XK_ISO_Left_Tab || (ks == XK_Tab && shift);
                        m_focus = (m_focus + (back ? n - 1 : 1)) % n;
                        Draw();
                    } else {
                        // Dugme etiketinin ilk harfi (Win32 &Evet/&Hayir kisayollari)
                        char buf[16]{};
                        KeySym sym = 0;
                        const int len = XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &sym, nullptr);
                        wchar_t ch = 0;
                        if (len == 1) ch = (wchar_t)(unsigned char)buf[0];
                        if (ch && (ev.xkey.state & ControlMask) == 0) {
                            int match = -1, count = 0;
                            for (size_t i = 0; i < m_btns.size(); ++i) {
                                if (!m_btns[i].label.empty() && towlower(m_btns[i].label[0]) == towlower(ch)) { match = (int)i; ++count; }
                            }
                            if (count == 1) { result = m_btns[match].id; done = true; }
                        }
                    }
                    break;
                }
                case ClientMessage:
                    if ((Atom)ev.xclient.data.l[0] == At(m_d, "WM_DELETE_WINDOW") && m_escId) { result = m_escId; done = true; }
                    break;
                default:
                    break;
                }
            }
            if (done) break;
            pollfd p{ ConnectionNumber(m_d), POLLIN, 0 };
            poll(&p, 1, 50);
        }
        XDestroyWindow(m_d, m_win);
        XSync(m_d, False);
        if (m_gc) XFreeGC(m_d, m_gc);
        NoteFence(m_lastTime);
        return result;
    }

private:
    struct Btn { int id; std::wstring label; int x = 0, y = 0, w = 0, h = 0; };

    void BuildButtons() {
        auto add = [&](int id, Lbl l) { m_btns.push_back(Btn{ id, Label(l) }); };
        switch (m_type & 0x0F) {
        case MB_OKCANCEL: add(IDOK, L_OK); add(IDCANCEL, L_CANCEL); m_escId = IDCANCEL; break;
        case 0x02: add(3, L_ABORT); add(IDRETRY, L_RETRY); add(5, L_IGNORE); m_escId = 0; break;
        case MB_YESNOCANCEL: add(IDYES, L_YES); add(IDNO, L_NO); add(IDCANCEL, L_CANCEL); m_escId = IDCANCEL; break;
        // Win32'de MB_YESNO'da Esc/kapat yoktur; burada guvenli taraf: Hayir.
        case MB_YESNO: add(IDYES, L_YES); add(IDNO, L_NO); m_escId = IDNO; break;
        case MB_RETRYCANCEL: add(IDRETRY, L_RETRY); add(IDCANCEL, L_CANCEL); m_escId = IDCANCEL; break;
        default: add(IDOK, L_OK); m_escId = IDOK; break;
        }
        const int def = (int)((m_type & 0xF00) >> 8);
        m_focus = std::clamp(def, 0, (int)m_btns.size() - 1);
    }

    void Wrap(int maxW) {
        m_lines.clear();
        size_t start = 0;
        while (start <= m_text.size()) {
            size_t nl = m_text.find(L'\n', start);
            std::wstring para = m_text.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
            if (!para.empty() && para.back() == L'\r') para.pop_back();
            if (para.empty()) m_lines.emplace_back();
            while (!para.empty()) {
                if (m_font.Measure(para) <= maxW) { m_lines.push_back(para); break; }
                size_t cut = 0, lastSpace = std::wstring::npos;
                int w = 0;
                for (; cut < para.size(); ++cut) {
                    const int cw = m_font.Measure(std::wstring(1, para[cut]));
                    if (w + cw > maxW) break;
                    w += cw;
                    if (para[cut] == L' ') lastSpace = cut;
                }
                size_t take = (lastSpace != std::wstring::npos && lastSpace > 0) ? lastSpace : std::max<size_t>(1, cut);
                m_lines.push_back(para.substr(0, take));
                para.erase(0, take);
                while (!para.empty() && para[0] == L' ') para.erase(0, 1);
            }
            if (nl == std::wstring::npos) break;
            start = nl + 1;
        }
        while (!m_lines.empty() && m_lines.back().empty()) m_lines.pop_back();
    }

    void LayoutAll() {
        const int pad = (int)std::lround(22 * m_s);
        const int iconType = (int)(m_type & 0xF0);
        m_icon = iconType;
        m_iconSz = iconType ? (int)std::lround(34 * m_s) : 0;
        const int maxW = (int)std::lround((m_text.size() > 900 ? 720 : 460) * m_s);
        Wrap(maxW);

        const POINT ptr = QueryPointerPos(m_d);
        const auto mons = Monitors(m_d);
        const int mi = MonitorIndexFor(mons, ptr, MONITOR_DEFAULTTONEAREST);
        m_mon = mi >= 0 ? WorkAreaFor(m_d, mons[mi]) : RECT{ 0, 0, 1280, 800 };

        m_lineH = m_font.Height() + (int)std::lround(4 * m_s);
        const int bandH = (int)std::lround(60 * m_s);
        const int maxTextH = (int)((m_mon.bottom - m_mon.top) * 0.8) - bandH - 2 * pad;
        const size_t maxLines = (size_t)std::max(3, maxTextH / std::max(1, m_lineH));
        if (m_lines.size() > maxLines) {
            m_lines.resize(maxLines);
            m_lines.back() += L" \u2026";
        }
        int textW = 0;
        for (const auto& l : m_lines) textW = std::max(textW, m_font.Measure(l));
        const int textH = (int)m_lines.size() * m_lineH;

        const int bh = (int)std::lround(32 * m_s), bgap = (int)std::lround(8 * m_s);
        int rowW = 0;
        for (auto& b : m_btns) {
            b.w = std::max((int)std::lround(92 * m_s), m_font.Measure(b.label) + (int)std::lround(32 * m_s));
            b.h = bh;
            rowW += b.w;
        }
        rowW += bgap * ((int)m_btns.size() - 1);

        const int contentW = (m_iconSz ? m_iconSz + (int)std::lround(16 * m_s) : 0) + textW;
        m_w = std::max({ contentW + 2 * pad, rowW + 2 * pad, (int)std::lround(320 * m_s) });
        m_bodyH = pad + std::max(textH, m_iconSz) + pad;
        m_h = m_bodyH + bandH;
        m_textX = pad + (m_iconSz ? m_iconSz + (int)std::lround(16 * m_s) : 0);
        m_textY = pad + std::max(0, (m_iconSz - textH) / 2);
        m_iconX = pad;
        m_iconY = pad + std::max(0, (textH - m_iconSz) / 2);
        int bx = m_w - pad - rowW;
        for (auto& b : m_btns) {
            b.x = bx;
            b.y = m_bodyH + (bandH - bh) / 2;
            bx += b.w + bgap;
        }
    }

    void CreateWindow() {
        // Sahibin ortasina, yoksa farenin bulundugu ekranin ortasina
        int x = m_mon.left + ((m_mon.right - m_mon.left) - m_w) / 2;
        int y = m_mon.top + ((m_mon.bottom - m_mon.top) - m_h) / 2;
        Window ownerWin = 0;
        if (IsMain(m_owner)) {
            XWindowAttributes wa{};
            if (XGetWindowAttributes(m_d, g.win, &wa) && wa.map_state == IsViewable) {
                Window child = 0;
                int ox = 0, oy = 0;
                if (XTranslateCoordinates(m_d, g.win, m_root, 0, 0, &ox, &oy, &child)) {
                    x = ox + (wa.width - m_w) / 2;
                    y = oy + (wa.height - m_h) / 2;
                    ownerWin = g.win;
                }
            }
        }
        x = std::clamp(x, (int)m_mon.left, std::max((int)m_mon.left, (int)m_mon.right - m_w));
        y = std::clamp(y, (int)m_mon.top, std::max((int)m_mon.top, (int)m_mon.bottom - m_h));

        XSetWindowAttributes a{};
        a.background_pixel = PackPixel(DefaultVisual(m_d, m_scr), 0x202020);
        a.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                       LeaveWindowMask | StructureNotifyMask | FocusChangeMask;
        m_win = XCreateWindow(m_d, m_root, x, y, (unsigned)m_w, (unsigned)m_h, 0, CopyFromParent, InputOutput,
                              CopyFromParent, CWBackPixel | CWEventMask, &a);
        m_gc = XCreateGC(m_d, m_win, 0, nullptr);

        SetUtf8Name(m_d, m_win, WideToUtf8(m_caption.empty() ? L"FullTerminal" : m_caption));
        SetClassHint(m_d, m_win);
        SetDialogIcon(m_d, m_win);

        XSizeHints* sh = XAllocSizeHints();
        sh->flags = PPosition | USPosition | PMinSize | PMaxSize;
        sh->x = x; sh->y = y;
        sh->min_width = sh->max_width = m_w;
        sh->min_height = sh->max_height = m_h;
        XSetWMNormalHints(m_d, m_win, sh);
        XFree(sh);

        XWMHints* wh = XAllocWMHints();
        wh->flags = InputHint;
        wh->input = True;
        XSetWMHints(m_d, m_win, wh);
        XFree(wh);

        Atom wmDelete = At(m_d, "WM_DELETE_WINDOW");
        XSetWMProtocols(m_d, m_win, &wmDelete, 1);

        Atom dlg = At(m_d, "_NET_WM_WINDOW_TYPE_DIALOG");
        XChangeProperty(m_d, m_win, At(m_d, "_NET_WM_WINDOW_TYPE"), XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(&dlg), 1);
        std::vector<Atom> states{ At(m_d, "_NET_WM_STATE_MODAL") };
        if (!ownerWin || (m_type & MB_TOPMOST)) states.push_back(At(m_d, "_NET_WM_STATE_ABOVE"));
        XChangeProperty(m_d, m_win, At(m_d, "_NET_WM_STATE"), XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(states.data()), (int)states.size());
        if (ownerWin) XSetTransientForHint(m_d, m_win, ownerWin);

        XMapRaised(m_d, m_win);
        XFlush(m_d);
    }

    void Activate() {
        SendRootMessage(m_d, m_win, At(m_d, "_NET_ACTIVE_WINDOW"), 2, CurrentTime, 0);
        if (!HaveEwmhWm(m_d)) {
            ErrorTrap t(m_d);
            XSetInputFocus(m_d, m_win, RevertToParent, CurrentTime);
            t.Finish();
        }
        XFlush(m_d);
    }

    int ButtonAt(int x, int y) const {
        for (size_t i = 0; i < m_btns.size(); ++i) {
            const Btn& b = m_btns[i];
            if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return (int)i;
        }
        return -1;
    }

    void DrawIcon(Surface& s) {
        if (!m_icon) return;
        const float r = m_iconSz / 2.0f;
        const float cx = m_iconX + r, cy = m_iconY + r;
        const int glyphPx = m_font.Height();
        auto center = [&](const wchar_t* t, uint32_t col, float ycenter) {
            const int w = m_font.Measure(t);
            const int base = (int)std::lround(ycenter - glyphPx / 2.0f) + m_font.Ascent();
            m_font.Draw(s, (int)std::lround(cx - w / 2.0f), base, t, col);
            m_font.Draw(s, (int)std::lround(cx - w / 2.0f) + 1, base, t, col);   // kalin gorunum
        };
        switch (m_icon) {
        case MB_ICONERROR:
            s.Circle(cx, cy, r, 0xE5484D);
            center(L"\u2715", 0xFFFFFF, cy);
            break;
        case MB_ICONWARNING:
            s.Triangle(cx, (float)m_iconY + 1.0f, (float)m_iconX, m_iconY + m_iconSz - 1.0f,
                       (float)(m_iconX + m_iconSz), m_iconY + m_iconSz - 1.0f, 0xF5A524);
            center(L"!", 0x1A1A1A, cy + r * 0.22f);
            break;
        case MB_ICONQUESTION:
            s.Circle(cx, cy, r, g_accent);
            center(L"?", 0xFFFFFF, cy);
            break;
        default:
            s.Circle(cx, cy, r, g_accent);
            center(L"i", 0xFFFFFF, cy);
            break;
        }
    }

    void Draw() {
        Surface s;
        s.Resize(m_w, m_h);
        s.Fill(0, 0, m_w, m_bodyH, 0x202020);
        s.Fill(0, m_bodyH, m_w, m_h - m_bodyH, 0x2A2A2A);
        s.Fill(0, m_bodyH, m_w, std::max(1, (int)m_s), 0x383838);
        DrawIcon(s);
        int y = m_textY;
        for (const auto& l : m_lines) {
            m_font.Draw(s, m_textX, y + m_font.Ascent(), l, 0xEDEDED);
            y += m_lineH;
        }
        const float rad = 5.0f * m_s;
        for (size_t i = 0; i < m_btns.size(); ++i) {
            const Btn& b = m_btns[i];
            const bool focus = (int)i == m_focus;
            const bool hover = (int)i == m_hover;
            const bool pressed = (int)i == m_pressed && hover;
            uint32_t fill = focus ? g_accent : 0x3A3A3A;
            if (hover && !focus) fill = 0x454545;
            if (pressed) fill = focus ? 0x2A5DB0 : 0x303030;
            s.RoundRect(b.x - 1, b.y - 1, b.w + 2, b.h + 2, rad + 1, focus ? 0x9CC2FF : 0x4C4C4C);
            s.RoundRect(b.x, b.y, b.w, b.h, rad, fill);
            const int tw = m_font.Measure(b.label);
            m_font.Draw(s, b.x + (b.w - tw) / 2, b.y + (b.h - m_font.Height()) / 2 + m_font.Ascent(), b.label, 0xFFFFFF);
        }
        Present(m_d, m_win, m_gc, s);
        XFlush(m_d);
    }

    Display* m_d;
    HWND m_owner;
    std::wstring m_text, m_caption;
    UINT m_type;
    int m_scr = 0;
    Window m_root = 0, m_win = 0;
    GC m_gc = nullptr;
    float m_s = 1.0f;
    TextRaster m_font;
    std::vector<Btn> m_btns;
    std::vector<std::wstring> m_lines;
    RECT m_mon{};
    int m_escId = IDCANCEL, m_focus = 0, m_hover = -1, m_pressed = -1;
    int m_w = 0, m_h = 0, m_bodyH = 0, m_lineH = 0, m_textX = 0, m_textY = 0;
    int m_icon = 0, m_iconSz = 0, m_iconX = 0, m_iconY = 0;
    unsigned long m_lastTime = 0;
};

int SafeDefault(UINT type) {
    switch (type & 0x0F) {
    case MB_OKCANCEL: case MB_RETRYCANCEL: case MB_YESNOCANCEL: return IDCANCEL;
    case MB_YESNO: return IDNO;
    default: return IDOK;
    }
}

int H_MessageBox(HWND owner, const wchar_t* text, const wchar_t* caption, UINT type) {
    std::lock_guard<std::recursive_mutex> lk(g_modalMutex);
    Display* d = XOpenDisplay(nullptr);
    if (!d) {
        // Ekran yok (ssh, headless): soru sorulamaz, metin kayboldu sanilmasin.
        fprintf(stderr, "[%s] %s\n", caption ? WideToUtf8(caption).c_str() : "FullTerminal",
                text ? WideToUtf8(text).c_str() : "");
        return SafeDefault(type);
    }
    int r = 0;
    {
        MessageBoxSession mb(d, owner, text ? text : L"", caption ? caption : L"", type);
        r = mb.Run();
    }
    XCloseDisplay(d);
    return r ? r : SafeDefault(type);
}

// ============================================================== tepsi ===
//
// freedesktop System Tray Protocol (XEmbed). KDE (xembedsniproxy), XFCE,
// MATE, Cinnamon, LXQt ve cogu panel destekler. Yonetici yoksa NIM_ADD FALSE
// doner ve cagiran pencereyi gizlemek yerine simge durumuna kucultur.

struct TrayState {
    bool active = false;
    bool docked = false;
    Window win = 0;
    Window manager = 0;
    Atom sel = 0;
    HWND hwnd = nullptr;
    UINT cbMsg = 0;
    std::wstring tip;
    std::vector<uint32_t> icon;
    int iconSize = 0;
    Visual* vis = nullptr;
    int depth = 0;
    Colormap cmap = 0;
    bool argb = false;
    Time lastClick = 0;
};
TrayState g_tray;

void TrayDestroyWindow() {
    if (!g.dpy) return;
    if (g_tray.win) { XDestroyWindow(g.dpy, g_tray.win); g_tray.win = 0; }
    if (g_tray.cmap) { XFreeColormap(g.dpy, g_tray.cmap); g_tray.cmap = 0; }
    g_tray.docked = false;
    XFlush(g.dpy);
}

void TrayApplyTip() {
    if (!g_tray.win) return;
    SetUtf8Name(g.dpy, g_tray.win, WideToUtf8(g_tray.tip.empty() ? L"FullTerminal" : g_tray.tip));
}

void TrayDraw() {
    if (!g_tray.win || g_tray.icon.empty() || !g.dpy) return;
    Display* d = g.dpy;
    XWindowAttributes wa{};
    if (!XGetWindowAttributes(d, g_tray.win, &wa) || wa.width <= 0 || wa.height <= 0) return;
    const int W = wa.width, H = wa.height;
    const int sz = std::max(1, (int)std::lround(std::min(W, H) * 0.86));
    const int ox = (W - sz) / 2, oy = (H - sz) / 2;
    const auto px = ScaleArgb(g_tray.icon.data(), g_tray.iconSize, sz);
    GC gc = XCreateGC(d, g_tray.win, 0, nullptr);
    if (g_tray.argb) {
        std::vector<uint32_t> buf((size_t)W * H, 0);
        const auto pm = Premultiply(px.data(), px.size());
        for (int y = 0; y < sz; ++y)
            for (int x = 0; x < sz; ++x) buf[(size_t)(y + oy) * W + (x + ox)] = pm[(size_t)y * sz + x];
        XImage* img = XCreateImage(d, g_tray.vis, 32, ZPixmap, 0, reinterpret_cast<char*>(buf.data()), W, H, 32, 0);
        if (img) {
            XPutImage(d, g_tray.win, gc, img, 0, 0, 0, 0, W, H);
            img->data = nullptr;
            XDestroyImage(img);
        }
    } else {
        // Saydam gorsel yok: panel arka plani (ParentRelative) + 1-bit maske
        XClearWindow(d, g_tray.win);
        const int stride = (sz + 7) / 8;
        std::vector<char> bits((size_t)stride * sz, 0);
        std::vector<uint32_t> rgb((size_t)sz * sz);
        for (int y = 0; y < sz; ++y) {
            for (int x = 0; x < sz; ++x) {
                const uint32_t p = px[(size_t)y * sz + x];
                const uint32_t a = p >> 24;
                if (a >= 110) bits[(size_t)y * stride + x / 8] |= (char)(1 << (x & 7));
                const uint32_t bg = 0x303030;
                auto mix = [&](int sh) { return ((((p >> sh) & 0xFF) * a + ((bg >> sh) & 0xFF) * (255 - a)) / 255) << sh; };
                rgb[(size_t)y * sz + x] = mix(16) | mix(8) | mix(0);
            }
        }
        Pixmap mask = XCreateBitmapFromData(d, g_tray.win, bits.data(), (unsigned)sz, (unsigned)sz);
        XSetClipMask(d, gc, mask);
        XSetClipOrigin(d, gc, ox, oy);
        PresentRgb(d, g_tray.win, gc, g_tray.vis, g_tray.depth, rgb.data(), sz, sz, ox, oy);
        XSetClipMask(d, gc, None);
        XFreePixmap(d, mask);
    }
    XFreeGC(d, gc);
    XFlush(d);
}

bool TrayDock() {
    Display* d = g.dpy;
    if (!d) return false;
    if (!g_tray.sel) {
        char nm[64];
        snprintf(nm, sizeof(nm), "_NET_SYSTEM_TRAY_S%d", g.screen);
        g_tray.sel = At(d, nm);
    }
    const Window mgr = XGetSelectionOwner(d, g_tray.sel);
    if (!mgr) return false;
    TrayDestroyWindow();
    g_tray.manager = mgr;
    XSelectInput(d, mgr, StructureNotifyMask);

    Visual* vis = DefaultVisual(d, g.screen);
    int depth = DefaultDepth(d, g.screen);
    bool argb = false;
    auto vv = GetProp32(d, mgr, At(d, "_NET_SYSTEM_TRAY_VISUAL"), XA_VISUALID, 1);
    if (!vv.empty()) {
        XVisualInfo tmpl{};
        tmpl.visualid = (VisualID)vv[0];
        int n = 0;
        if (XVisualInfo* vi = XGetVisualInfo(d, VisualIDMask, &tmpl, &n)) {
            if (n > 0 && vi[0].depth == 32) { vis = vi[0].visual; depth = 32; argb = true; }
            XFree(vi);
        }
    }
    XSetWindowAttributes a{};
    unsigned long mask = CWEventMask;
    a.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask;
    if (argb) {
        g_tray.cmap = XCreateColormap(d, g.root, vis, AllocNone);
        a.colormap = g_tray.cmap;
        a.background_pixel = 0;
        a.border_pixel = 0;
        mask |= CWColormap | CWBackPixel | CWBorderPixel;
    } else {
        a.background_pixmap = ParentRelative;
        mask |= CWBackPixmap;
    }
    g_tray.win = XCreateWindow(d, g.root, 0, 0, 24, 24, 0, depth, InputOutput, vis, mask, &a);
    g_tray.vis = vis;
    g_tray.depth = depth;
    g_tray.argb = argb;

    unsigned long info[2] = { 0, 1 };   // XEmbed surum 0, XEMBED_MAPPED
    const Atom xinfo = At(d, "_XEMBED_INFO");
    XChangeProperty(d, g_tray.win, xinfo, xinfo, 32, PropModeReplace, reinterpret_cast<unsigned char*>(info), 2);
    SetClassHint(d, g_tray.win);
    TrayApplyTip();

    XEvent e{};
    e.xclient.type = ClientMessage;
    e.xclient.window = mgr;
    e.xclient.message_type = At(d, "_NET_SYSTEM_TRAY_OPCODE");
    e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime;
    e.xclient.data.l[1] = 0;            // SYSTEM_TRAY_REQUEST_DOCK
    e.xclient.data.l[2] = (long)g_tray.win;
    XSendEvent(d, mgr, False, NoEventMask, &e);
    XFlush(d);
    g_tray.docked = true;
    return true;
}

BOOL H_NotifyIcon(DWORD msg, NOTIFYICONDATAW* nid) {
    if (!g.dpy || !nid) return FALSE;
    auto take = [&] {
        g_tray.hwnd = nid->hWnd;
        if (nid->uFlags & NIF_MESSAGE) g_tray.cbMsg = nid->uCallbackMessage;
        if (nid->uFlags & NIF_TIP) g_tray.tip = nid->szTip;
        if ((nid->uFlags & NIF_ICON) && nid->hIcon) {
            auto* li = static_cast<LinuxIcon*>(nid->hIcon);
            if (li->size > 0 && li->argb.size() >= (size_t)li->size * li->size) {
                g_tray.icon = li->argb;
                g_tray.iconSize = li->size;
            }
        }
        if (g_tray.icon.empty()) {
            g_tray.icon.assign(kAppIconArgb, kAppIconArgb + kAppIconSize * kAppIconSize);
            g_tray.iconSize = kAppIconSize;
        }
    };
    switch (msg) {
    case NIM_ADD:
        take();
        g_tray.active = true;
        if (!TrayDock()) { g_tray.active = false; return FALSE; }
        return TRUE;
    case NIM_MODIFY:
        take();
        if (!g_tray.docked) return FALSE;
        TrayApplyTip();
        TrayDraw();
        return TRUE;
    case NIM_DELETE:
        g_tray.active = false;
        TrayDestroyWindow();
        return TRUE;
    default:
        return FALSE;
    }
}

void TrayPost(UINT mouseMsg) {
    if (g_tray.hwnd && g_tray.cbMsg) PostMessageW(g_tray.hwnd, g_tray.cbMsg, 1, (LPARAM)mouseMsg);
}

// ======================================================== global kisayol ===

struct Hotkey { int id; KeyCode code; unsigned mods; bool down; };
std::vector<Hotkey> g_hotkeys;
const unsigned kLockVariants[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };

KeySym VkToKeySym(UINT vk) {
    if (vk >= VK_F1 && vk <= VK_F24) return XK_F1 + (vk - VK_F1);
    if (vk >= 'A' && vk <= 'Z') return XK_a + (vk - 'A');
    if (vk >= '0' && vk <= '9') return XK_0 + (vk - '0');
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return XK_KP_0 + (vk - VK_NUMPAD0);
    switch (vk) {
    case VK_SPACE: return XK_space;
    case VK_PAUSE: return XK_Pause;
    case VK_SCROLL: return XK_Scroll_Lock;
    case VK_INSERT: return XK_Insert;
    case VK_DELETE: return XK_Delete;
    case VK_HOME: return XK_Home;
    case VK_END: return XK_End;
    case VK_PRIOR: return XK_Prior;
    case VK_NEXT: return XK_Next;
    case VK_TAB: return XK_Tab;
    case VK_RETURN: return XK_Return;
    case VK_ESCAPE: return XK_Escape;
    case VK_BACK: return XK_BackSpace;
    case VK_UP: return XK_Up;
    case VK_DOWN: return XK_Down;
    case VK_LEFT: return XK_Left;
    case VK_RIGHT: return XK_Right;
    case 0xC0: return XK_grave;          // VK_OEM_3
    case 0xBA: return XK_semicolon;      // VK_OEM_1
    case 0xBB: return XK_equal;          // VK_OEM_PLUS
    case 0xBC: return XK_comma;          // VK_OEM_COMMA
    case 0xBD: return XK_minus;          // VK_OEM_MINUS
    case 0xBE: return XK_period;         // VK_OEM_PERIOD
    case 0xBF: return XK_slash;          // VK_OEM_2
    case 0xDB: return XK_bracketleft;    // VK_OEM_4
    case 0xDC: return XK_backslash;      // VK_OEM_5
    case 0xDD: return XK_bracketright;   // VK_OEM_6
    case 0xDE: return XK_apostrophe;     // VK_OEM_7
    default: return 0;
    }
}

BOOL H_UnregisterHotKey(HWND, int id) {
    if (!g.dpy) return FALSE;
    bool any = false;
    for (auto it = g_hotkeys.begin(); it != g_hotkeys.end();) {
        if (it->id == id) {
            for (unsigned v : kLockVariants) XUngrabKey(g.dpy, it->code, it->mods | v, g.root);
            it = g_hotkeys.erase(it);
            any = true;
        } else {
            ++it;
        }
    }
    XFlush(g.dpy);
    return any ? TRUE : FALSE;
}

BOOL H_RegisterHotKey(HWND h, int id, UINT mods, UINT vk) {
    if (!IsMain(h)) return FALSE;
    H_UnregisterHotKey(h, id);
    const KeySym ks = VkToKeySym(vk);
    if (!ks) return FALSE;
    const KeyCode kc = XKeysymToKeycode(g.dpy, ks);
    if (!kc) return FALSE;
    unsigned xm = 0;
    if (mods & MOD_ALT) xm |= Mod1Mask;
    if (mods & MOD_CONTROL) xm |= ControlMask;
    if (mods & MOD_SHIFT) xm |= ShiftMask;
    if (mods & MOD_WIN) xm |= Mod4Mask;
    ErrorTrap t(g.dpy);
    for (unsigned v : kLockVariants) XGrabKey(g.dpy, kc, xm | v, g.root, False, GrabModeAsync, GrabModeAsync);
    if (t.Finish() != 0) {
        // Baska bir uygulama (ya da masaustu) bu tusu tutuyor
        for (unsigned v : kLockVariants) XUngrabKey(g.dpy, kc, xm | v, g.root);
        XFlush(g.dpy);
        return FALSE;
    }
    g_hotkeys.push_back(Hotkey{ id, kc, xm, false });
    return TRUE;
}

// ========================================================= pencere kancalari ===

bool IsZoomedImpl() {
    return HasNetState(g.dpy, g.win, At(g.dpy, "_NET_WM_STATE_MAXIMIZED_VERT")) &&
           HasNetState(g.dpy, g.win, At(g.dpy, "_NET_WM_STATE_MAXIMIZED_HORZ"));
}

void SetMaximized(bool on) {
    SetNetState(g.dpy, g.win, on, At(g.dpy, "_NET_WM_STATE_MAXIMIZED_VERT"), At(g.dpy, "_NET_WM_STATE_MAXIMIZED_HORZ"));
}

void SetStaticPosHints(int x, int y, int w, int h) {
    XSizeHints sh{};
    long supplied = 0;
    XGetWMNormalHints(g.dpy, g.win, &sh, &supplied);
    sh.flags |= USPosition | PWinGravity;
    sh.x = x;
    sh.y = y;
    sh.win_gravity = StaticGravity;
    if (w > 0 && h > 0) { sh.flags |= USSize; sh.width = w; sh.height = h; }
    XSetWMNormalHints(g.dpy, g.win, &sh);
}

void MapBack() {
    if (g.hiddenByUs && g.hidePosValid) {
        SetStaticPosHints(g.hideX, g.hideY, 0, 0);
        XMoveWindow(g.dpy, g.win, g.hideX, g.hideY);
    }
    g.hiddenByUs = false;
    XMapRaised(g.dpy, g.win);
}

BOOL H_IsZoomed(HWND h) { return IsMain(h) && IsZoomedImpl() ? TRUE : FALSE; }
BOOL H_IsIconic(HWND h) { return IsMain(h) && WmState(g.dpy, g.win) == IconicState ? TRUE : FALSE; }

BOOL H_IsWindowVisible(HWND h) {
    if (!IsMain(h)) return FALSE;
    if (g.hiddenByUs) return FALSE;
    XWindowAttributes wa{};
    if (XGetWindowAttributes(g.dpy, g.win, &wa) && wa.map_state == IsViewable) return TRUE;
    return WmState(g.dpy, g.win) == IconicState ? TRUE : FALSE;
}

BOOL H_ShowWindow(HWND h, int cmd) {
    if (!IsMain(h)) return FALSE;
    Display* d = g.dpy;
    const BOOL wasVisible = H_IsWindowVisible(h);
    switch (cmd) {
    case SW_HIDE: {
        if (!g.hiddenByUs) {
            Window child = 0;
            int x = 0, y = 0;
            XWindowAttributes wa{};
            if (XGetWindowAttributes(d, g.win, &wa) && wa.map_state == IsViewable &&
                XTranslateCoordinates(d, g.win, g.root, 0, 0, &x, &y, &child)) {
                g.hideX = x; g.hideY = y; g.hidePosValid = true;
            }
        }
        // Withdraw: gorev cubugundan da kalkar (Win32 SW_HIDE gibi)
        XWithdrawWindow(d, g.win, g.screen);
        g.hiddenByUs = true;
        break;
    }
    case SW_MINIMIZE:
    case SW_SHOWMINIMIZED:
        if (g.hiddenByUs) MapBack();
        XIconifyWindow(d, g.win, g.screen);
        break;
    case SW_SHOWMAXIMIZED:
        MapBack();
        SetMaximized(true);
        break;
    case SW_RESTORE:
        if (g.hiddenByUs || WmState(d, g.win) == IconicState) MapBack();
        else if (IsZoomedImpl()) SetMaximized(false);
        else MapBack();
        break;
    default:   // SW_SHOW, SW_SHOWNORMAL, ...
        MapBack();
        break;
    }
    XFlush(d);
    return wasVisible;
}

BOOL H_GetPlacement(HWND h, WINDOWPLACEMENT* wp) {
    if (!wp || !IsMain(h)) return FALSE;
    Display* d = g.dpy;
    XWindowAttributes wa{};
    if (!XGetWindowAttributes(d, g.win, &wa)) return FALSE;
    int x = 0, y = 0;
    if (g.hiddenByUs && g.hidePosValid) {
        x = g.hideX; y = g.hideY;
    } else {
        Window child = 0;
        XTranslateCoordinates(d, g.win, g.root, 0, 0, &x, &y, &child);
    }
    wp->flags = 0;
    wp->rcNormalPosition = RECT{ x, y, x + wa.width, y + wa.height };
    const long st = WmState(d, g.win);
    wp->showCmd = st == IconicState ? SW_SHOWMINIMIZED : (IsZoomedImpl() ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    return TRUE;
}

BOOL H_SetPlacement(HWND h, const WINDOWPLACEMENT* wp) {
    if (!wp || !IsMain(h)) return FALSE;
    Display* d = g.dpy;
    const RECT& r = wp->rcNormalPosition;
    const int w = r.right - r.left, hh = r.bottom - r.top;
    if (wp->showCmd != SW_SHOWMAXIMIZED && IsZoomedImpl()) SetMaximized(false);
    if (w > 0 && hh > 0) {
        SetStaticPosHints(r.left, r.top, w, hh);
        XMoveResizeWindow(d, g.win, r.left, r.top, (unsigned)w, (unsigned)hh);
        g.hideX = r.left; g.hideY = r.top; g.hidePosValid = true;
    }
    if (wp->showCmd == SW_SHOWMAXIMIZED) SetMaximized(true);
    else if (wp->showCmd == SW_SHOWMINIMIZED) XIconifyWindow(d, g.win, g.screen);
    XFlush(d);
    return TRUE;
}

BOOL H_SetForeground(HWND h) {
    if (!IsMain(h) || g.hiddenByUs) return FALSE;
    Display* d = g.dpy;
    // Kaynak 2 (pager): kullanicinin acik istegi (kisayol/tepsi); odak calma engeline takilmasin
    SendRootMessage(d, g.win, At(d, "_NET_ACTIVE_WINDOW"), 2, CurrentTime, 0);
    XRaiseWindow(d, g.win);
    if (!HaveEwmhWm(d)) {
        XWindowAttributes wa{};
        if (XGetWindowAttributes(d, g.win, &wa) && wa.map_state == IsViewable) {
            ErrorTrap t(d);
            XSetInputFocus(d, g.win, RevertToParent, CurrentTime);
            t.Finish();
        }
    }
    XFlush(d);
    return TRUE;
}

HWND H_GetForeground() {
    if (!g.dpy) return nullptr;
    Window act = 0;
    auto v = GetProp32(g.dpy, g.root, At(g.dpy, "_NET_ACTIVE_WINDOW"), XA_WINDOW, 1);
    if (!v.empty()) {
        act = (Window)v[0];
    } else {
        int rev = 0;
        XGetInputFocus(g.dpy, &act, &rev);
    }
    if (act == g.win) return g.hwnd;
    return act > 1 ? reinterpret_cast<HWND>(static_cast<intptr_t>(2)) : nullptr;   // baska bir pencere
}

BOOL H_GetCursorPos(POINT* p) {
    if (!p) return FALSE;
    p->x = 0; p->y = 0;
    if (!g.dpy) return FALSE;
    *p = QueryPointerPos(g.dpy);
    return TRUE;
}

BOOL H_ClientToScreen(HWND h, POINT* p) {
    if (!p || !IsMain(h)) return FALSE;
    Window child = 0;
    int x = 0, y = 0;
    if (!XTranslateCoordinates(g.dpy, g.win, g.root, p->x, p->y, &x, &y, &child)) return FALSE;
    p->x = x; p->y = y;
    return TRUE;
}

BOOL H_GetClientRect(HWND h, RECT* rc) {
    if (!rc || !IsMain(h)) return FALSE;
    if (g.cw > 0 && g.ch > 0) { *rc = RECT{ 0, 0, g.cw, g.ch }; return TRUE; }
    XWindowAttributes wa{};
    if (!XGetWindowAttributes(g.dpy, g.win, &wa)) return FALSE;
    *rc = RECT{ 0, 0, wa.width, wa.height };
    return TRUE;
}

HMONITOR H_MonitorFromPoint(POINT p, DWORD flags) {
    if (!g.dpy) return (HMONITOR)(intptr_t)1;
    const int i = MonitorIndexFor(Monitors(g.dpy), p, flags);
    return i < 0 ? nullptr : (HMONITOR)(intptr_t)(i + 1);
}

HMONITOR H_MonitorFromWindow(HWND h, DWORD flags) {
    if (!IsMain(h)) return H_MonitorFromPoint(POINT{ 0, 0 }, flags);
    RECT rc{};
    H_GetClientRect(h, &rc);
    POINT c{ rc.right / 2, rc.bottom / 2 };
    H_ClientToScreen(h, &c);
    return H_MonitorFromPoint(c, flags);
}

BOOL H_GetMonitorInfo(HMONITOR mon, MONITORINFO* mi) {
    if (!mi || !g.dpy) return FALSE;
    const auto ms = Monitors(g.dpy);
    const int i = (int)(intptr_t)mon - 1;
    if (i < 0 || i >= (int)ms.size()) return FALSE;
    mi->rcMonitor = ms[i];
    mi->rcWork = WorkAreaFor(g.dpy, ms[i]);
    mi->dwFlags = (i == 0) ? 1 /* MONITORINFOF_PRIMARY */ : 0;
    return TRUE;
}

BOOL H_GetWorkArea(RECT* r) {
    if (!r || !g.dpy) return FALSE;
    const auto ms = Monitors(g.dpy);
    if (ms.empty()) return FALSE;
    *r = WorkAreaFor(g.dpy, ms[0]);
    return TRUE;
}

void H_MessageBeep(UINT) { g_bellPending = true; }

BOOL H_FlashWindow(HWND h, BOOL invert) {
    if (h != g.hwnd) return FALSE;
    g_attnPending = invert ? 1 : 2;
    return TRUE;
}

void SetAttention(bool on) {
    if (!g.dpy || g.attention == on) return;
    XWMHints* wh = XGetWMHints(g.dpy, g.win);
    if (!wh) wh = XAllocWMHints();
    if (wh) {
        if (on) wh->flags |= XUrgencyHint; else wh->flags &= ~XUrgencyHint;
        XSetWMHints(g.dpy, g.win, wh);
        XFree(wh);
    }
    SetNetState(g.dpy, g.win, on, At(g.dpy, "_NET_WM_STATE_DEMANDS_ATTENTION"));
    g.attention = on;
    XFlush(g.dpy);
}

LRESULT H_SendMessage(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SETICON && wp == ICON_BIG) SetWindowIcon(h, reinterpret_cast<HICON>(lp));
    return 0;
}

HICON H_LoadAppIcon() {
    static LinuxIcon icon = [] {
        LinuxIcon li;
        li.size = kAppIconSize;
        li.argb.assign(kAppIconArgb, kAppIconArgb + kAppIconSize * kAppIconSize);
        li.owned = false;
        return li;
    }();
    return &icon;
}

} // namespace

// ============================================================ disa acik ===

std::vector<uint32_t> ScaleArgb(const uint32_t* src, int srcSize, int dstSize) {
    if (!src || srcSize <= 0 || dstSize <= 0) return {};
    if (srcSize == dstSize) return std::vector<uint32_t>(src, src + (size_t)srcSize * srcSize);
    const auto pm = Premultiply(src, (size_t)srcSize * srcSize);
    return Unpremultiply(ScalePremul(pm, srcSize, srcSize, dstSize, dstSize));
}

void InstallHooks() {
    LinuxPlatformHooks& hk = g_linuxHooks;
    hk.showWindow = H_ShowWindow;
    hk.isIconic = H_IsIconic;
    hk.isWindowVisible = H_IsWindowVisible;
    hk.isZoomed = H_IsZoomed;
    hk.getWindowPlacement = H_GetPlacement;
    hk.setWindowPlacement = H_SetPlacement;
    hk.setForegroundWindow = H_SetForeground;
    hk.getForegroundWindow = H_GetForeground;
    hk.getCursorPos = H_GetCursorPos;
    hk.clientToScreen = H_ClientToScreen;
    hk.getClientRect = H_GetClientRect;
    hk.monitorFromPoint = H_MonitorFromPoint;
    hk.monitorFromWindow = H_MonitorFromWindow;
    hk.getMonitorInfo = H_GetMonitorInfo;
    hk.getWorkArea = H_GetWorkArea;
    hk.messageBeep = H_MessageBeep;
    hk.flashWindow = H_FlashWindow;
    hk.messageBox = H_MessageBox;
    hk.trackPopupMenu = H_TrackPopup;
    hk.notifyIcon = H_NotifyIcon;
    hk.sendMessage = H_SendMessage;
    hk.loadAppIcon = H_LoadAppIcon;
    hk.registerHotKey = H_RegisterHotKey;
    hk.unregisterHotKey = H_UnregisterHotKey;
}

void SetMainWindow(HWND hwnd) {
    if (!hwnd) {
        if (g.dpy) {
            g_tray.active = false;
            TrayDestroyWindow();
            for (auto& hk : g_hotkeys)
                for (unsigned v : kLockVariants) XUngrabKey(g.dpy, hk.code, hk.mods | v, g.root);
            g_hotkeys.clear();
            XFlush(g.dpy);
        }
        g = MainState{};
        return;
    }
    auto* lh = reinterpret_cast<LinuxWindowHandle*>(hwnd);
    g.hwnd = hwnd;
    g.dpy = static_cast<Display*>(lh->display);
    g.win = (Window)lh->window;
    g.screen = lh->screen;
    g.root = RootWindow(g.dpy, g.screen);
    // Kok pencere: tepsi yoneticisi (MANAGER), DPI (RESOURCE_MANAGER), global kisayol
    XWindowAttributes ra{};
    long mask = StructureNotifyMask | PropertyChangeMask;
    if (XGetWindowAttributes(g.dpy, g.root, &ra)) mask |= ra.your_event_mask;
    XSelectInput(g.dpy, g.root, mask);
    Bool supported = False;
    XkbSetDetectableAutoRepeat(g.dpy, True, &supported);
}

bool HandleEvent(union _XEvent* evp, int* hotkeyId) {
    if (hotkeyId) *hotkeyId = 0;
    if (!evp || !g.dpy) return false;
    XEvent& ev = *evp;

    if (g_tray.win && ev.xany.window == g_tray.win) {
        switch (ev.type) {
        case Expose:
            if (ev.xexpose.count == 0) TrayDraw();
            break;
        case ConfigureNotify:
            TrayDraw();
            break;
        case ButtonPress:
            if (ev.xbutton.button == 1) {
                if (g_tray.lastClick && ev.xbutton.time - g_tray.lastClick < 400) {
                    TrayPost(0x0203 /* WM_LBUTTONDBLCLK */);
                    g_tray.lastClick = 0;
                } else {
                    g_tray.lastClick = ev.xbutton.time;
                }
            }
            break;
        case ButtonRelease:
            if (ev.xbutton.button == 1) TrayPost(0x0202 /* WM_LBUTTONUP */);
            else if (ev.xbutton.button == 3) TrayPost(0x0205 /* WM_RBUTTONUP */);
            break;
        case DestroyNotify:
            if (ev.xdestroywindow.window == g_tray.win) { g_tray.win = 0; g_tray.docked = false; }
            break;
        default:
            break;
        }
        return true;
    }

    // Tepsi yoneticisi oldu (panel yeniden basladi): Win32 TaskbarCreated gibi bekle
    if (ev.type == DestroyNotify && g_tray.manager && ev.xdestroywindow.window == g_tray.manager) {
        g_tray.manager = 0;
        if (g_tray.win) { XUnmapWindow(g.dpy, g_tray.win); TrayDestroyWindow(); }
        g_tray.docked = false;
        return true;
    }
    if (ev.type == ClientMessage && ev.xclient.window == g.root &&
        ev.xclient.message_type == At(g.dpy, "MANAGER") && g_tray.sel &&
        (Atom)ev.xclient.data.l[1] == g_tray.sel) {
        if (g_tray.active && !g_tray.docked) {
            if (TrayDock()) TrayDraw();
        }
        return true;
    }

    if ((ev.type == KeyPress || ev.type == KeyRelease) && ev.xkey.window == g.root) {
        const unsigned m = ev.xkey.state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
        for (auto& hk : g_hotkeys) {
            if (hk.code != ev.xkey.keycode || hk.mods != m) continue;
            if (ev.type == KeyPress) {
                if (!hk.down && hotkeyId) *hotkeyId = hk.id;   // MOD_NOREPEAT
                hk.down = true;
            } else {
                hk.down = false;
            }
            return true;
        }
        return true;
    }
    return false;
}

void PumpDeferred() {
    if (!g.dpy) return;
    if (g_bellPending.exchange(false)) {
        static Clock::time_point last{};
        const auto now = Clock::now();
        if (now - last > 120ms) {
            last = now;
            if (!XkbBell(g.dpy, g.win, 0, None)) XBell(g.dpy, 0);
            XFlush(g.dpy);
        }
    }
    const int a = g_attnPending.exchange(0);
    if (a == 1) {
        if (H_GetForeground() != g.hwnd) SetAttention(true);
    } else if (a == 2) {
        SetAttention(false);
    }
}

void NoteClientSize(int w, int h) { g.cw = w; g.ch = h; }

void ClearAttention() { SetAttention(false); }

unsigned long ModalInputFence() { return g_modalFence.load(); }

UINT SystemDpi(void* display) {
    if (const char* e = getenv("FULLTERMINAL_DPI")) {
        const int v = atoi(e);
        if (v >= 48 && v <= 480) return (UINT)v;
    }
    if (auto* d = static_cast<Display*>(display)) {
        // RESOURCE_MANAGER her seferinde taze okunur: masaustu olcegi calisirken degisebilir
        Atom at = 0;
        int af = 0;
        unsigned long n = 0, after = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(d, RootWindow(d, DefaultScreen(d)), XA_RESOURCE_MANAGER, 0, 1 << 16, False,
                               XA_STRING, &at, &af, &n, &after, &data) == Success && data) {
            const std::string s(reinterpret_cast<char*>(data), n);
            XFree(data);
            size_t p = s.find("Xft.dpi:");
            while (p != std::string::npos && p > 0 && s[p - 1] != '\n') p = s.find("Xft.dpi:", p + 1);
            if (p != std::string::npos) {
                const double v = atof(s.c_str() + p + 8);
                if (v >= 48.0 && v <= 480.0) return (UINT)std::lround(v);
            }
        }
    }
    if (const char* gs = getenv("GDK_SCALE")) {
        const int v = atoi(gs);
        if (v >= 1 && v <= 4) return (UINT)(96 * v);
    }
    if (const char* q = getenv("QT_SCALE_FACTOR")) {
        const double v = atof(q);
        if (v >= 0.5 && v <= 5.0) return (UINT)std::lround(96.0 * v);
    }
    return 96;
}

UINT ShowWindowMenu(HWND hwnd) {
    if (!IsMain(hwnd)) return 0;
    const bool zoomed = IsZoomedImpl();
    LinuxMenu m;
    auto add = [&](UINT id, Lbl l, bool enabled, const wchar_t* accel = nullptr) {
        LinuxMenuItem it;
        it.flags = MF_STRING | (enabled ? 0 : MF_GRAYED);
        it.id = id;
        it.text = Label(l);
        if (accel) it.text += std::wstring(L"\t") + accel;
        m.items.push_back(std::move(it));
    };
    add(kScRestore, L_RESTORE, zoomed);
    add(kScMove, L_MOVE, !zoomed);
    add(kScSize, L_SIZE, !zoomed);
    add(kScMinimize, L_MINIMIZE, true);
    add(kScMaximize, L_MAXIMIZE, !zoomed);
    m.items.push_back(LinuxMenuItem{ MF_SEPARATOR, 0, L"" });
    add(kScClose, L_CLOSE, true, L"Alt+F4");
    POINT p{ 0, 0 };
    H_ClientToScreen(hwnd, &p);
    return H_TrackPopup(&m, TPM_RETURNCMD, p.x, p.y, hwnd);
}

void ApplyWindowCommand(HWND hwnd, UINT sc) {
    if (!IsMain(hwnd)) return;
    Display* d = g.dpy;
    switch (sc) {
    case kScRestore: H_ShowWindow(hwnd, SW_RESTORE); break;
    case kScMinimize: XIconifyWindow(d, g.win, g.screen); break;
    case kScMaximize: SetMaximized(true); break;
    case kScMove:
    case kScSize: {
        const POINT p = QueryPointerPos(d);
        // _NET_WM_MOVERESIZE_MOVE_KEYBOARD (10) / _SIZE_KEYBOARD (9): ok tuslari ile
        SendRootMessage(d, g.win, At(d, "_NET_WM_MOVERESIZE"), p.x, p.y, sc == kScMove ? 10 : 9, 0, 1);
        break;
    }
    case kScClose: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
    default: break;
    }
    XFlush(d);
}

void SetWindowIcon(HWND hwnd, HICON icon) {
    if (!IsMain(hwnd) || !icon) return;
    auto* li = static_cast<LinuxIcon*>(icon);
    if (li->size <= 0 || li->argb.size() < (size_t)li->size * li->size) return;
    std::vector<unsigned long> data;
    for (int sz : { 16, 24, 32, 48, 64, 128 }) {
        const auto px = ScaleArgb(li->argb.data(), li->size, sz);
        data.push_back((unsigned long)sz);
        data.push_back((unsigned long)sz);
        for (uint32_t p : px) data.push_back(p);
    }
    XChangeProperty(g.dpy, g.win, At(g.dpy, "_NET_WM_ICON"), XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(data.data()), (int)data.size());
    XFlush(g.dpy);
}

} // namespace ft::lnx

#endif // !defined(_WIN32)
