#include "render/Renderer.h"
#include "render/PlainTextHighlights.h"

#if !defined(_WIN32)
#include "ui/Theme.h"
#include "core/Utf8.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>
#include <iostream>

namespace ft {

namespace {

inline void ResolveCell(const Cell& c, bool selected, uint32_t& fg, uint32_t& bg) {
    if (c.flags & CF_FgDefault)      fg = theme::TermFg;
    else if (c.flags & CF_FgIndexed) fg = theme::Xterm256((int)c.fg);
    else                             fg = c.fg;

    if (c.flags & CF_BgDefault)      bg = theme::TermBg;
    else if (c.flags & CF_BgIndexed) bg = theme::Xterm256((int)c.bg);
    else                             bg = c.bg;

    if (c.flags & CF_Dim)     fg = theme::Mix(bg, fg, 0.55f);
    if (c.flags & CF_Inverse) std::swap(fg, bg);
    if (c.flags & CF_Hidden)  fg = bg;
    if (selected) {
        bg = theme::TermSel;
        if (fg == theme::TermSel) fg = theme::TextHi;
    }
}

inline void ColorToRgba(uint32_t c, float alpha, float& r, float& g, float& b, float& a) {
    r = ((c >> 16) & 0xFF) / 255.0f;
    g = ((c >> 8)  & 0xFF) / 255.0f;
    b = (c         & 0xFF) / 255.0f;
    a = alpha;
}

std::string FindFontPath(const std::string& family, bool bold, bool italic) {
    if (!FcInit()) return "";

    FcPattern* pat = FcPatternCreate();
    if (!pat) return "";

    FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family.c_str());
    FcPatternAddInteger(pat, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddBool(pat, FC_SCALABLE, FcTrue);

    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern* match = FcFontMatch(nullptr, pat, &result);
    std::string path;
    if (match) {
        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            path = (const char*)file;
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);
    return path;
}

} // namespace

bool Renderer::Init(HWND hwnd, UINT dpi, std::wstring* err) {
    m_hwnd = hwnd;
    m_dpi = dpi ? dpi : 96;

    if (!InitOpenGL(hwnd, err)) return false;
    if (!InitFreeType(err)) return false;

    ComputeMetrics();
    m_ready = true;
    return true;
}

void Renderer::Shutdown() {
    m_ready = false;
    if (m_atlasTex && m_glContext && m_xdisplay) {
        glDeleteTextures(1, &m_atlasTex);
        m_atlasTex = 0;
    }
    for (int i = 3; i >= 0; --i) {
        if (m_ftFaces[i]) {
            if (i == 0 || m_ftFaces[i] != m_ftFaces[0]) {
                FT_Done_Face(m_ftFaces[i]);
            }
            m_ftFaces[i] = nullptr;
        }
    }
    if (m_ftLib) {
        FT_Done_FreeType(m_ftLib);
        m_ftLib = nullptr;
    }
    if (m_glContext && m_xdisplay) {
        glXMakeCurrent(m_xdisplay, 0, nullptr);
        glXDestroyContext(m_xdisplay, m_glContext);
        m_glContext = nullptr;
        if (m_hwnd) {
            reinterpret_cast<LinuxWindowHandle*>(m_hwnd)->glContext = nullptr;
        }
    }
    m_xdisplay = nullptr;
    m_xwindow = 0;
}

bool Renderer::InitOpenGL(HWND hwnd, std::wstring* err) {
    auto fail = [&](const wchar_t* msg) {
        if (err) *err = msg;
        return false;
    };

    auto* winH = reinterpret_cast<LinuxWindowHandle*>(hwnd);
    if (!winH || !winH->display) {
        return fail(L"Gecersiz LinuxWindowHandle");
    }

    m_xdisplay = reinterpret_cast<Display*>(winH->display);
    m_xwindow = static_cast<Window>(winH->window);

    if (winH->glContext) {
        m_glContext = reinterpret_cast<GLXContext>(winH->glContext);
    } else {
        int glxAttribs[] = {
            GLX_RGBA,
            GLX_DOUBLEBUFFER,
            GLX_RED_SIZE, 8,
            GLX_GREEN_SIZE, 8,
            GLX_BLUE_SIZE, 8,
            GLX_ALPHA_SIZE, 8,
            0
        };
        XVisualInfo* vi = glXChooseVisual(m_xdisplay, winH->screen, glxAttribs);
        if (!vi) return fail(L"glXChooseVisual basarisiz");

        m_glContext = glXCreateContext(m_xdisplay, vi, nullptr, GL_TRUE);
        XFree(vi);
        if (!m_glContext) return fail(L"glXCreateContext basarisiz");
        winH->glContext = m_glContext;
    }

    glXMakeCurrent(m_xdisplay, m_xwindow, m_glContext);

    using PFNGLXSWAPINTERVALEXTPROC = void (*)(Display*, GLXDrawable, int);
    using PFNGLXSWAPINTERVALMESAPROC = int (*)(unsigned int);
    if (auto fn = reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>(
            glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXSwapIntervalEXT")))) {
        fn(m_xdisplay, m_xwindow, 0);
    } else if (auto fnMesa = reinterpret_cast<PFNGLXSWAPINTERVALMESAPROC>(
            glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXSwapIntervalMESA")))) {
        fnMesa(0);
    }

    // Doku atlasi olustur (2048x2048 RGBA)
    glGenTextures(1, &m_atlasTex);
    glBindTexture(GL_TEXTURE_2D, m_atlasTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    std::vector<uint32_t> blank(m_atlasW * m_atlasH, 0);
    // (0,0) pikseli duz dolgular icin beyaz yapilir
    blank[0] = 0xFFFFFFFF;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_atlasW, m_atlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, blank.data());

    m_atlasNextX = 2;
    m_atlasNextY = 2;
    m_atlasRowH = 0;

    return true;
}

bool Renderer::InitFreeType(std::wstring* err) {
    auto fail = [&](const wchar_t* msg) {
        if (err) *err = msg;
        return false;
    };

    if (FT_Init_FreeType(&m_ftLib)) {
        return fail(L"FreeType kutuphanesi baslatilamadi");
    }

    std::string family = m_preferredFamily.empty() ? "monospace" : WideToUtf8(m_preferredFamily);

    std::string paths[4];
    paths[0] = FindFontPath(family, false, false);
    paths[1] = FindFontPath(family, true, false);
    paths[2] = FindFontPath(family, false, true);
    paths[3] = FindFontPath(family, true, true);

    // Eger monospace bulunamadiysa DejaVu Sans Mono veya sistem fontlarini dene
    if (paths[0].empty()) {
        const char* fallbacks[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"
        };
        for (const char* fb : fallbacks) {
            std::ifstream test(fb);
            if (test.good()) { paths[0] = fb; break; }
        }
    }

    if (paths[0].empty()) {
        return fail(L"Sistemde eslesen monospace font bulunamadi");
    }

    for (int i = 0; i < 4; ++i) {
        std::string p = !paths[i].empty() ? paths[i] : paths[0];
        if (FT_New_Face(m_ftLib, p.c_str(), 0, &m_ftFaces[i])) {
            if (i > 0 && m_ftFaces[0]) {
                m_ftFaces[i] = m_ftFaces[0];
            } else {
                return fail(L"Font yuzu yuklenemedi");
            }
        }
    }

    m_fm.family = Utf8ToWide(family);
    return true;
}

void Renderer::ComputeMetrics() {
    if (!m_ftFaces[0]) return;

    const float sizePx = m_fontPt * (float)m_dpi / 72.0f;
    m_fm.sizePx = sizePx;

    const FT_UInt px = (FT_UInt)std::max(1.0f, std::round(sizePx));
    for (int i = 0; i < 4; ++i) {
        if (m_ftFaces[i]) {
            FT_Set_Pixel_Sizes(m_ftFaces[i], 0, px);
        }
    }

    if (FT_Load_Char(m_ftFaces[0], 'M', FT_LOAD_DEFAULT) == 0) {
        m_fm.cellW = std::max(1.0f, (float)(m_ftFaces[0]->glyph->advance.x >> 6));
    } else {
        m_fm.cellW = std::max(1.0f, std::round(sizePx * 0.6f));
    }

    if (m_ftFaces[0]->size) {
        m_fm.cellH = std::max(1.0f, (float)(m_ftFaces[0]->size->metrics.height >> 6));
        m_fm.baseline = (float)(m_ftFaces[0]->size->metrics.ascender >> 6);
    } else {
        m_fm.cellH = std::max(1.0f, std::round(sizePx * 1.3f));
        m_fm.baseline = std::round(sizePx * 0.9f);
    }

    m_fm.underlineThickness = std::max(1.0f, std::round(sizePx / 14.0f));
    m_fm.underlineY = std::min(m_fm.cellH - m_fm.underlineThickness, m_fm.baseline + 2.0f);

    // Atlasi sifirla
    m_glyphAtlas.clear();
    m_atlasNextX = 2;
    m_atlasNextY = 2;
    m_atlasRowH = 0;
}

void Renderer::Resize(UINT pxW, UINT pxH) {
    m_width = std::max<UINT>(1, pxW);
    m_height = std::max<UINT>(1, pxH);
    if (m_drawing) {
        glViewport(0, 0, m_width, m_height);
    }
}

void Renderer::SetDpi(UINT dpi) {
    if (dpi == m_dpi || dpi == 0) return;
    m_dpi = dpi;
    ComputeMetrics();
}

void Renderer::SetFontSizePt(float pt) {
    if (std::abs(pt - m_fontPt) < 0.1f || pt < 4.0f) return;
    m_fontPt = pt;
    ComputeMetrics();
}

bool Renderer::SetFontFamily(const std::wstring& family) {
    m_preferredFamily = family;
    std::wstring err;
    if (!InitFreeType(&err)) return false;
    ComputeMetrics();
    return true;
}

int Renderer::FaceIndex(uint16_t flags) const {
    return ((flags & CF_Bold) ? 1 : 0) | ((flags & CF_Italic) ? 2 : 0);
}

const Renderer::GlyphInfo* Renderer::GetOrLoadGlyph(int faceIdx, char32_t cp) {
    const uint64_t key = ((uint64_t)faceIdx << 32) | (uint32_t)cp;
    auto it = m_glyphAtlas.find(key);
    if (it != m_glyphAtlas.end()) return &it->second;

    FT_Face face = m_ftFaces[std::clamp(faceIdx, 0, 3)];
    if (!face) face = m_ftFaces[0];
    if (!face) return nullptr;

    FT_UInt gidx = FT_Get_Char_Index(face, cp);
    if (gidx == 0 && faceIdx != 0) {
        face = m_ftFaces[0];
        gidx = FT_Get_Char_Index(face, cp);
    }
    if (gidx == 0) {
        // Bulunamayan glif: yerine soru isareti ya da yer tutucu dene
        gidx = FT_Get_Char_Index(face, U'?');
    }

    if (FT_Load_Glyph(face, gidx, FT_LOAD_RENDER)) return nullptr;

    FT_Bitmap& bmp = face->glyph->bitmap;
    const int bw = bmp.width;
    const int bh = bmp.rows;

    // Raf bin-packing
    if (m_atlasNextX + bw + 1 >= m_atlasW) {
        m_atlasNextX = 2;
        m_atlasNextY += m_atlasRowH + 1;
        m_atlasRowH = 0;
    }
    if (m_atlasNextY + bh + 1 >= m_atlasH) {
        // Atlas doldu: temizle ve bastan basla
        m_glyphAtlas.clear();
        m_atlasNextX = 2;
        m_atlasNextY = 2;
        m_atlasRowH = 0;
    }

    const int gx = m_atlasNextX;
    const int gy = m_atlasNextY;

    if (bw > 0 && bh > 0) {
        std::vector<uint32_t> rgba(bw * bh);
        for (int r = 0; r < bh; ++r) {
            for (int c = 0; c < bw; ++c) {
                uint8_t a = bmp.buffer[r * bmp.pitch + c];
                rgba[r * bw + c] = ((uint32_t)a << 24) | 0x00FFFFFF;
            }
        }

        glBindTexture(GL_TEXTURE_2D, m_atlasTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, gx, gy, bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    }

    m_atlasNextX += bw + 1;
    if (bh > m_atlasRowH) m_atlasRowH = bh;

    GlyphInfo info;
    info.u0 = (float)gx / (float)m_atlasW;
    info.v0 = (float)gy / (float)m_atlasH;
    info.u1 = (float)(gx + bw) / (float)m_atlasW;
    info.v1 = (float)(gy + bh) / (float)m_atlasH;
    info.width = bw;
    info.height = bh;
    info.bearingX = face->glyph->bitmap_left;
    info.bearingY = face->glyph->bitmap_top;
    info.advance = (float)(face->glyph->advance.x >> 6);

    auto res = m_glyphAtlas.emplace(key, info);
    return &res.first->second;
}

bool Renderer::Begin() {
    if (!m_ready || !m_xdisplay || !m_glContext) return false;

    if ((m_width == 0 || m_height == 0) && m_hwnd) {
        RECT rc{};
        if (GetClientRect(m_hwnd, &rc) && rc.right > rc.left && rc.bottom > rc.top) {
            m_width = (UINT)(rc.right - rc.left);
            m_height = (UINT)(rc.bottom - rc.top);
        }
    }

    glXMakeCurrent(m_xdisplay, m_xwindow, m_glContext);
    glDrawBuffer(GL_BACK);
    glViewport(0, 0, m_width, m_height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, m_width, m_height, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_drawing = true;
    m_clipStack.clear();
    glDisable(GL_SCISSOR_TEST);

    return true;
}

void Renderer::End() {
    if (!m_drawing) return;
    m_drawing = false;
    glFlush();
    glXSwapBuffers(m_xdisplay, m_xwindow);
    glReadBuffer(GL_FRONT);
}

void Renderer::DrawSolidQuad(float x0, float y0, float x1, float y1, uint32_t color, float alpha) {
    float r, g, b, a;
    ColorToRgba(color, alpha, r, g, b, a);

    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

void Renderer::DrawTexturedQuad(float x0, float y0, float x1, float y1,
                               float u0, float v0, float u1, float v1,
                               uint32_t color, float alpha) {
    float r, g, b, a;
    ColorToRgba(color, alpha, r, g, b, a);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_atlasTex);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x0, y0);
    glTexCoord2f(u1, v0); glVertex2f(x1, y0);
    glTexCoord2f(u1, v1); glVertex2f(x1, y1);
    glTexCoord2f(u0, v1); glVertex2f(x0, y1);
    glEnd();
}

void Renderer::Fill(const D2D1_RECT_F& r, uint32_t color, float alpha) {
    DrawSolidQuad(r.left, r.top, r.right, r.bottom, color, alpha);
}

void Renderer::FillRound(const D2D1_RECT_F& r, float radius, uint32_t color, float alpha) {
    const float rad = std::min({ radius, (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f });
    if (rad <= 0.5f) {
        Fill(r, color, alpha);
        return;
    }

    float red, grn, blu, a;
    ColorToRgba(color, alpha, red, grn, blu, a);

    glDisable(GL_TEXTURE_2D);
    glColor4f(red, grn, blu, a);

    // Merkez ve yan dikdortgenler
    glBegin(GL_QUADS);
    glVertex2f(r.left + rad, r.top);
    glVertex2f(r.right - rad, r.top);
    glVertex2f(r.right - rad, r.bottom);
    glVertex2f(r.left + rad, r.bottom);

    glVertex2f(r.left, r.top + rad);
    glVertex2f(r.left + rad, r.top + rad);
    glVertex2f(r.left + rad, r.bottom - rad);
    glVertex2f(r.left, r.bottom - rad);

    glVertex2f(r.right - rad, r.top + rad);
    glVertex2f(r.right, r.top + rad);
    glVertex2f(r.right, r.bottom - rad);
    glVertex2f(r.right - rad, r.bottom - rad);
    glEnd();

    // 4 Kose yaylari
    const int segments = 8;
    const auto drawCorner = [&](float cx, float cy, float startAng) {
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= segments; ++i) {
            float ang = startAng + (3.14159265f * 0.5f * i) / segments;
            glVertex2f(cx + std::cos(ang) * rad, cy + std::sin(ang) * rad);
        }
        glEnd();
    };

    drawCorner(r.left + rad,  r.top + rad,    3.14159265f);        // Sol ust
    drawCorner(r.right - rad, r.top + rad,    3.14159265f * 1.5f); // Sag ust
    drawCorner(r.right - rad, r.bottom - rad, 0.0f);               // Sag alt
    drawCorner(r.left + rad,  r.bottom - rad, 3.14159265f * 0.5f); // Sol alt
}

void Renderer::Stroke(const D2D1_RECT_F& r, uint32_t color, float width, float alpha) {
    float red, grn, blu, a;
    ColorToRgba(color, alpha, red, grn, blu, a);

    glDisable(GL_TEXTURE_2D);
    glColor4f(red, grn, blu, a);
    glLineWidth(width);
    glBegin(GL_LINE_LOOP);
    glVertex2f(r.left + width * 0.5f, r.top + width * 0.5f);
    glVertex2f(r.right - width * 0.5f, r.top + width * 0.5f);
    glVertex2f(r.right - width * 0.5f, r.bottom - width * 0.5f);
    glVertex2f(r.left + width * 0.5f, r.bottom - width * 0.5f);
    glEnd();
}

void Renderer::StrokeRound(const D2D1_RECT_F& r, float radius, uint32_t color, float width, float alpha) {
    const float rad = std::min({ radius, (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f });
    if (rad <= 0.5f) {
        Stroke(r, color, width, alpha);
        return;
    }

    float red, grn, blu, a;
    ColorToRgba(color, alpha, red, grn, blu, a);

    glDisable(GL_TEXTURE_2D);
    glColor4f(red, grn, blu, a);
    glLineWidth(width);

    const int segments = 6;
    glBegin(GL_LINE_LOOP);
    // Ust kenar
    glVertex2f(r.left + rad, r.top + width * 0.5f);
    glVertex2f(r.right - rad, r.top + width * 0.5f);
    // Sag ust kose
    for (int i = 0; i <= segments; ++i) {
        float ang = 3.14159265f * 1.5f + (3.14159265f * 0.5f * i) / segments;
        glVertex2f(r.right - rad + std::cos(ang) * (rad - width * 0.5f),
                   r.top + rad + std::sin(ang) * (rad - width * 0.5f));
    }
    // Sag kenar
    glVertex2f(r.right - width * 0.5f, r.bottom - rad);
    // Sag alt kose
    for (int i = 0; i <= segments; ++i) {
        float ang = 0.0f + (3.14159265f * 0.5f * i) / segments;
        glVertex2f(r.right - rad + std::cos(ang) * (rad - width * 0.5f),
                   r.bottom - rad + std::sin(ang) * (rad - width * 0.5f));
    }
    // Alt kenar
    glVertex2f(r.left + rad, r.bottom - width * 0.5f);
    // Sol alt kose
    for (int i = 0; i <= segments; ++i) {
        float ang = 3.14159265f * 0.5f + (3.14159265f * 0.5f * i) / segments;
        glVertex2f(r.left + rad + std::cos(ang) * (rad - width * 0.5f),
                   r.bottom - rad + std::sin(ang) * (rad - width * 0.5f));
    }
    // Sol kenar
    glVertex2f(r.left + width * 0.5f, r.top + rad);
    // Sol ust kose
    for (int i = 0; i <= segments; ++i) {
        float ang = 3.14159265f + (3.14159265f * 0.5f * i) / segments;
        glVertex2f(r.left + rad + std::cos(ang) * (rad - width * 0.5f),
                   r.top + rad + std::sin(ang) * (rad - width * 0.5f));
    }
    glEnd();
}

void Renderer::Line(float x0, float y0, float x1, float y1, uint32_t color, float width) {
    float red, grn, blu, a;
    ColorToRgba(color, 1.0f, red, grn, blu, a);

    glDisable(GL_TEXTURE_2D);
    glColor4f(red, grn, blu, a);
    glLineWidth(width);
    glBegin(GL_LINES);
    glVertex2f(x0, y0);
    glVertex2f(x1, y1);
    glEnd();
}

void Renderer::Disc(float cx, float cy, float r, uint32_t color, float alpha) {
    float red, grn, blu, a;
    ColorToRgba(color, alpha, red, grn, blu, a);

    glDisable(GL_TEXTURE_2D);
    glColor4f(red, grn, blu, a);

    const int segments = 24;
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segments; ++i) {
        float ang = (3.14159265f * 2.0f * i) / segments;
        glVertex2f(cx + std::cos(ang) * r, cy + std::sin(ang) * r);
    }
    glEnd();
}

void Renderer::Ring(float cx, float cy, float r, uint32_t color, float width, float alpha) {
    float red, grn, blu, a;
    ColorToRgba(color, alpha, red, grn, blu, a);

    glDisable(GL_TEXTURE_2D);
    glColor4f(red, grn, blu, a);
    glLineWidth(width);

    const int segments = 24;
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; ++i) {
        float ang = (3.14159265f * 2.0f * i) / segments;
        glVertex2f(cx + std::cos(ang) * r, cy + std::sin(ang) * r);
    }
    glEnd();
}

void Renderer::PushClip(const D2D1_RECT_F& r) {
    // Kirpma GL penceresi koordinatinda: etkin olcek/oteleme (DrawRecording) uygulanir.
    ClipRect cr{ m_xfX + r.left * m_xfScale, m_xfY + r.top * m_xfScale,
                 m_xfX + r.right * m_xfScale, m_xfY + r.bottom * m_xfScale };
    if (!m_clipStack.empty()) {
        const auto& prev = m_clipStack.back();
        cr.left = std::max(cr.left, prev.left);
        cr.top = std::max(cr.top, prev.top);
        cr.right = std::min(cr.right, prev.right);
        cr.bottom = std::min(cr.bottom, prev.bottom);
    }
    m_clipStack.push_back(cr);
    ApplyScissor();
}

void Renderer::PopClip() {
    if (!m_clipStack.empty()) {
        m_clipStack.pop_back();
    }
    ApplyScissor();
}

void Renderer::ApplyScissor() {
    if (m_clipStack.empty()) {
        glDisable(GL_SCISSOR_TEST);
    } else {
        const auto& c = m_clipStack.back();
        const int sx = (int)std::floor(c.left);
        const int sy = (int)std::floor((float)m_height - c.bottom);
        const int sw = std::max(0, (int)std::ceil(c.right - c.left));
        const int sh = std::max(0, (int)std::ceil(c.bottom - c.top));
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx, sy, sw, sh);
    }
}

float Renderer::MeasureText(const std::wstring& s, float sizePx, bool semibold, bool mono) {
    if (s.empty()) return 0.0f;
    const int faceIdx = semibold ? 1 : 0;
    float w = 0.0f;
    for (wchar_t ch : s) {
        const auto* g = GetOrLoadGlyph(faceIdx, (char32_t)ch);
        w += (g && g->advance > 0.0f) ? g->advance : m_fm.cellW;
    }
    return w;
}

void Renderer::Text(const std::wstring& s, const D2D1_RECT_F& r, uint32_t color,
                    float sizePx, Align align, bool semibold, bool mono, float alpha,
                    bool wrap) {
    if (s.empty()) return;
    const int faceIdx = semibold ? 1 : 0;
    const float totalW = MeasureText(s, sizePx, semibold, mono);

    float curX = r.left;
    if (align == Align::Center) {
        curX = r.left + (r.right - r.left - totalW) * 0.5f;
    } else if (align == Align::Right) {
        curX = r.right - totalW;
    }

    const float boxH = r.bottom - r.top;
    const float curY = wrap ? (r.top + m_fm.baseline) : (r.top + (boxH - m_fm.cellH) * 0.5f + m_fm.baseline);

    PushClip(r);

    float lineX = curX;
    float lineY = curY;

    for (wchar_t ch : s) {
        if (ch == L'\n') {
            lineX = r.left;
            lineY += m_fm.cellH;
            continue;
        }

        const auto* g = GetOrLoadGlyph(faceIdx, (char32_t)ch);
        if (g) {
            if (wrap && (lineX + g->advance > r.right) && lineX > r.left) {
                lineX = r.left;
                lineY += m_fm.cellH;
            }

            const float gx = lineX + g->bearingX;
            const float gy = lineY - g->bearingY;
            DrawTexturedQuad(gx, gy, gx + g->width, gy + g->height,
                             g->u0, g->v0, g->u1, g->v1, color, alpha);
            lineX += (g->advance > 0.0f) ? g->advance : m_fm.cellW;
        } else {
            lineX += m_fm.cellW;
        }
    }

    PopClip();
}

// Kayit oynatici onizlemesi: terminali alana sigacak sekilde olcekleyip cizer (Windows'taki
// D2D donusum matrisinin karsiligi: GL modelview + kirpma dikdortgeni donusumu).
void Renderer::DrawRecording(const Screen& screen, const D2D1_RECT_F& area) {
    if (!m_drawing || area.right <= area.left || area.bottom <= area.top) return;
    const float w = screen.Cols() * m_fm.cellW, h = screen.Rows() * m_fm.cellH;
    if (w <= 0 || h <= 0) return;
    const float scale = std::min(1.f, std::min((area.right - area.left) / w, (area.bottom - area.top) / h));
    Fill(area, theme::TermBg);
    const float oldScale = m_xfScale, oldX = m_xfX, oldY = m_xfY;
    glPushMatrix();
    glTranslatef(area.left, area.top, 0.0f);
    glScalef(scale, scale, 1.0f);
    m_xfScale = oldScale * scale;
    m_xfX = oldX + area.left * oldScale;
    m_xfY = oldY + area.top * oldScale;
    DrawTerminal(screen, D2D1::RectF(0, 0, w, h), false, false, 0, Selection{}, 1.f);
    glPopMatrix();
    m_xfScale = oldScale; m_xfX = oldX; m_xfY = oldY;
}

void Renderer::DrawTerminal(const Screen& screen, const D2D1_RECT_F& area,
                            bool focused, bool cursorOn, int cursorStyle,
                            const Selection& sel, float bgAlpha, bool highlightPlainText) {
    if (!m_drawing) return;
    bgAlpha = std::clamp(bgAlpha, 0.10f, 1.0f);

    const int cols = screen.Cols();
    const int rows = screen.Rows();
    if (cols <= 0 || rows <= 0) return;

    const float cw = m_fm.cellW;
    const float ch = m_fm.cellH;

    PushClip(area);

    // Terminal alani arka plani: D2D1_PRIMITIVE_BLEND_COPY karsiligi (on carpimli RGB ve tam bgAlpha)
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ZERO, GL_ONE, GL_ZERO);
    DrawSolidQuad(area.left, area.top, area.right, area.bottom, theme::TermBg, bgAlpha);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_row.resize((size_t)cols);

    int sy0 = sel.y0, sy1 = sel.y1, sx0 = sel.x0, sx1 = sel.x1;
    if (sel.active && (sy1 < sy0 || (sy1 == sy0 && sx1 < sx0))) {
        std::swap(sy0, sy1);
        std::swap(sx0, sx1);
    }

    std::vector<uint8_t> highlights;
    const bool highlight = highlightPlainText && !screen.IsAltBuffer();
    for (int y = 0; y < rows; ++y) {
        screen.CopyViewRow(y, m_row.data());
        if (highlight) PlainTextHighlights(m_row.data(), cols, highlights);

        int selFrom = -1, selTo = -1;
        if (sel.active && y >= sy0 && y <= sy1) {
            selFrom = (y == sy0) ? std::max(0, sx0) : 0;
            selTo   = (y == sy1) ? std::min(cols, sx1 + 1) : cols;
        }

        for (int x = 0; x < cols; ++x) {
            const Cell& cell = m_row[(size_t)x];

            // Secim kontrolu
            bool isSelected = (selFrom >= 0 && x >= selFrom && x < selTo);

            uint32_t fg = 0, bg = 0;
            ResolveCell(cell, isSelected, fg, bg);
            if (highlight && !isSelected && highlights[(size_t)x]) fg = theme::Ansi[highlights[(size_t)x] - 1];

            const float cx0 = area.left + x * cw;
            const float cy0 = area.top + y * ch;
            const float cx1 = cx0 + cw;
            const float cy1 = cy0 + ch;

            // Arka plan cizimi (D2D1_PRIMITIVE_BLEND_COPY karsiligi: bgAlpha korunur)
            if (bg != theme::TermBg || isSelected) {
                const float cellAlpha = isSelected ? 1.0f : bgAlpha;
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ZERO, GL_ONE, GL_ZERO);
                DrawSolidQuad(cx0, cy0, cx1, cy1, bg, cellAlpha);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }

            // Metin glifi cizimi
            if (cell.ch > U' ' && !(cell.flags & CF_WideTail) && !(cell.flags & CF_Hidden)) {
                const int fi = FaceIndex(cell.flags);
                const auto* g = GetOrLoadGlyph(fi, cell.ch);
                if (g) {
                    const float gx = cx0 + g->bearingX;
                    const float gy = cy0 + m_fm.baseline - g->bearingY;
                    DrawTexturedQuad(gx, gy, gx + g->width, gy + g->height,
                                     g->u0, g->v0, g->u1, g->v1, fg, 1.0f);
                }
            }

            // Alt cizgi / baglanti / ustunu cizme
            if ((cell.flags & CF_Underline) || cell.linkId != 0) {
                DrawSolidQuad(cx0, cy0 + m_fm.underlineY, cx1,
                              cy0 + m_fm.underlineY + m_fm.underlineThickness, fg, 1.0f);
            }
            if (cell.flags & CF_Strike) {
                const float strikeY = cy0 + ch * 0.5f;
                DrawSolidQuad(cx0, strikeY, cx1, strikeY + m_fm.underlineThickness, fg, 1.0f);
            }
        }
    }

    // Imlec cizimi
    int curX = 0, curY = 0;
    if (screen.CursorVisible() && screen.CursorInView(curX, curY) && curX >= 0 && curX < cols && curY >= 0 && curY < rows) {
        screen.CopyViewRow(curY, m_row.data());
        if (m_row[(size_t)curX].flags & CF_WideTail && curX > 0) --curX;
        const Cell& c = m_row[(size_t)curX];
        const bool wideLead = (curX + 1 < cols) && (m_row[(size_t)curX + 1].flags & CF_WideTail);
        const float kx0 = area.left + curX * cw;
        const float ky0 = area.top + curY * ch;
        const float kx1 = kx0 + (wideLead ? cw * 2.0f : cw);
        const float ky1 = ky0 + ch;
        const uint32_t curCol = theme::TermCursor;
        const bool steady = (cursorStyle == 2 || cursorStyle == 4 || cursorStyle == 6);

        if (!focused) {
            // Odaksizken ici bos cerceve
            Stroke(D2D1::RectF(kx0 + 0.5f, ky0 + 0.5f, kx1 - 0.5f, ky1 - 0.5f), curCol, 1.0f);
        } else if (cursorOn || steady) {
            if (cursorStyle <= 2) {
                // Blok imlec
                DrawSolidQuad(kx0, ky0, kx1, ky1, curCol, 1.0f);
                if (c.ch > U' ' && !(c.flags & CF_WideTail) && !(c.flags & CF_Hidden)) {
                    const auto* g = GetOrLoadGlyph(FaceIndex(c.flags), c.ch);
                    if (g) {
                        const float gx = kx0 + g->bearingX;
                        const float gy = ky0 + m_fm.baseline - g->bearingY;
                        DrawTexturedQuad(gx, gy, gx + g->width, gy + g->height,
                                         g->u0, g->v0, g->u1, g->v1, theme::TermBg, 1.0f);
                    }
                }
            } else if (cursorStyle == 3 || cursorStyle == 4) {
                // Alt cizgi imleci
                const float bh = std::max(2.0f, std::round(ch * 0.14f));
                DrawSolidQuad(kx0, ky1 - bh, kx1, ky1, curCol, 1.0f);
            } else {
                // Dikey cubuk imleci
                const float bw = std::max(2.0f, std::round(cw * 0.22f));
                DrawSolidQuad(kx0, ky0, kx0 + bw, ky1, curCol, 1.0f);
            }
        }
    }

    PopClip();
}

bool Renderer::SaveScreenshot(const wchar_t* filename) {
    if (!filename || m_width == 0 || m_height == 0) return false;

    std::string path = WideToUtf8(filename);
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    std::vector<uint8_t> pixels(m_width * m_height * 4);
    glReadPixels(0, 0, m_width, m_height, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());

    // 24-bit veya 32-bit BMP dosyasi yaz
    const uint32_t fileHeaderSize = 14;
    const uint32_t infoHeaderSize = 40;
    const uint32_t imageSize = m_width * m_height * 4;
    const uint32_t fileSize = fileHeaderSize + infoHeaderSize + imageSize;

    uint8_t header[14] = {
        'B', 'M',
        (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
        0, 0, 0, 0,
        (uint8_t)(fileHeaderSize + infoHeaderSize), 0, 0, 0
    };

    uint8_t info[40] = {
        (uint8_t)(infoHeaderSize), 0, 0, 0,
        (uint8_t)(m_width), (uint8_t)(m_width >> 8), (uint8_t)(m_width >> 16), (uint8_t)(m_width >> 24),
        (uint8_t)(m_height), (uint8_t)(m_height >> 8), (uint8_t)(m_height >> 16), (uint8_t)(m_height >> 24),
        1, 0,
        32, 0,
        0, 0, 0, 0,
        (uint8_t)(imageSize), (uint8_t)(imageSize >> 8), (uint8_t)(imageSize >> 16), (uint8_t)(imageSize >> 24),
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };

    out.write(reinterpret_cast<const char*>(header), 14);
    out.write(reinterpret_cast<const char*>(info), 40);
    out.write(reinterpret_cast<const char*>(pixels.data()), imageSize);
    return out.good();
}

} // namespace ft

#endif // !defined(_WIN32)
