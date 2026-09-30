#include "render/Renderer.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <wincodec.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace ft {
namespace {

inline uint64_t BrushKey(uint32_t color, float alpha) {
    return ((uint64_t)color << 32) | (uint32_t)(alpha * 1000.0f);
}
inline uint64_t FormatKey(float sizePx, bool semibold, bool mono) {
    return ((uint64_t)(sizePx * 100.0f) << 8) | (semibold ? 2u : 0u) | (mono ? 1u : 0u);
}

// Hucrenin son ekran renklerini cozer: dim, inverse, hidden ve secim dahil.
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

inline bool PlainSpace(const Cell& c) {
    if ((c.flags & (CF_Underline | CF_Strike)) || c.linkId != 0) return false;
    return c.ch == U' ' || c.ch == 0 || (c.flags & CF_WideTail) != 0;
}

// Yuz indisi (FaceIndex) sirasi: 0 duz, 1 kalin, 2 italik, 3 kalin italik
const DWRITE_FONT_WEIGHT kFaceWeights[4] = {
    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_BOLD,
    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_BOLD };
const DWRITE_FONT_STYLE kFaceStyles[4] = {
    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STYLE_NORMAL,
    DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STYLE_ITALIC };

inline bool DeviceLostHr(HRESULT hr) {
    return hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED ||
           hr == DXGI_ERROR_DEVICE_RESET;
}

} // namespace

// ------------------------------------------------------------------ init ---

bool Renderer::Init(HWND hwnd, UINT dpi, std::wstring* err) {
    m_hwnd = hwnd;
    m_dpi = dpi ? dpi : 96;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    m_width = std::max<UINT>(1, rc.right - rc.left);
    m_height = std::max<UINT>(1, rc.bottom - rc.top);

    if (!CreateDeviceResources(err)) return false;
    if (!CreateFonts(err)) return false;
    ComputeMetrics();
    return true;
}

bool Renderer::CreateDeviceResources(std::wstring* err, bool allowWarp) {
    auto fail = [&](const wchar_t* m) { if (err) *err = m; return false; };

    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
    };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   levels, (UINT)std::size(levels), D3D11_SDK_VERSION,
                                   &m_d3d, nullptr, &m_d3dCtx);
    if (FAILED(hr) && allowWarp) {
        // Donanim yoksa WARP ile devam et; yavas ama calisir.
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                               levels, (UINT)std::size(levels), D3D11_SDK_VERSION,
                               &m_d3d, nullptr, &m_d3dCtx);
    }
    if (FAILED(hr)) return fail(L"D3D11 cihazi olusturulamadi.");

    ComPtr<IDXGIDevice1> dxgiDevice;
    if (FAILED(m_d3d.As(&dxgiDevice))) return fail(L"IDXGIDevice alinamadi.");
    dxgiDevice->SetMaximumFrameLatency(1);

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return fail(L"DXGI adaptoru alinamadi.");

    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return fail(L"IDXGIFactory2 alinamadi.");

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = m_width;
    sd.Height = m_height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    if (FAILED(factory->CreateSwapChainForComposition(m_d3d.Get(), &sd, nullptr, &m_swap)))
        return fail(L"Swapchain olusturulamadi.");

    // Fabrikalar cihazdan bagimsiz; cihaz yeniden kurulurken korunuyor.
    if (!m_d2dFactory) {
        D2D1_FACTORY_OPTIONS opts{};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                                     &opts, reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()))))
            return fail(L"Direct2D fabrikasi olusturulamadi.");
    }

    if (FAILED(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice)))
        return fail(L"Direct2D cihazi olusturulamadi.");
    if (FAILED(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_ctx)))
        return fail(L"Direct2D baglami olusturulamadi.");

    if (!m_dwrite) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(m_dwrite.GetAddressOf()))))
            return fail(L"DirectWrite fabrikasi olusturulamadi.");
    }

    if (!CreateTargetBitmap()) return fail(L"Hedef bitmap olusturulamadi.");

    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_dcomp))))
        return fail(L"DirectComposition cihazi olusturulamadi.");
    if (FAILED(m_dcomp->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget)))
        return fail(L"DirectComposition hedefi olusturulamadi.");
    if (FAILED(m_dcomp->CreateVisual(&m_visual)))
        return fail(L"DirectComposition gorseli olusturulamadi.");

    m_visual->SetContent(m_swap.Get());
    m_dcompTarget->SetRoot(m_visual.Get());
    m_dcomp->Commit();
    return true;
}

bool Renderer::CreateTargetBitmap() {
    ComPtr<IDXGISurface> surface;
    if (FAILED(m_swap->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;

    // 96 DPI sabit: tum koordinatlar aygit pikselinde, izgara hizasi bozulmasin.
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);

    if (FAILED(m_ctx->CreateBitmapFromDxgiSurface(surface.Get(), &props, &m_target))) return false;
    m_ctx->SetTarget(m_target.Get());
    m_ctx->SetDpi(96.0f, 96.0f);
    m_ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

void Renderer::ReleaseTargetBitmap() {
    if (m_ctx) m_ctx->SetTarget(nullptr);
    m_target.Reset();
}

void Renderer::ReleaseDeviceResources() {
    // Fircalar ve hedef eski cihaza ait. DComp hedefi pencereye yalnizca bir kez
    // baglanabildigi icin yenisi kurulmadan once birakilmali.
    m_brushes.clear();
    m_cellBrush.Reset();
    ReleaseTargetBitmap();
    m_visual.Reset();
    m_dcompTarget.Reset();
    m_dcomp.Reset();
    m_ctx.Reset();
    m_d2dDevice.Reset();
    m_swap.Reset();
    m_d3dCtx.Reset();
    m_d3d.Reset();
}

bool Renderer::RecreateDevice() {
    if (!m_hwnd || !m_d2dFactory || !m_dwrite) return false;
    ReleaseDeviceResources();

    RECT rc{};
    if (GetClientRect(m_hwnd, &rc)) {
        m_width = std::max<UINT>(1, rc.right - rc.left);
        m_height = std::max<UINT>(1, rc.bottom - rc.top);
    }
    // Surucu kurulurken donanim cihazi birkac saniye acilmayabilir; hemen WARP'a
    // dusersek oturumun geri kalaninda yazilimsal cizimde kaliriz.
    if (!CreateDeviceResources(nullptr, m_recreateFails >= 6)) {
        ReleaseDeviceResources();
        ++m_recreateFails;
        return false;
    }
    m_deviceLost = false;
    m_recreateFails = 0;
    return true;
}

void Renderer::Shutdown() {
    m_formats.clear();
    for (auto& c : m_glyphCache) c.clear();
    for (auto& f : m_faces) f.Reset();
    for (auto& f : m_fallbackFormat) f.Reset();
    m_fallbackLayouts.clear();
    ReleaseDeviceResources();
    m_d2dFactory.Reset();
    m_dwrite.Reset();
}

void Renderer::Resize(UINT w, UINT h) {
    w = std::max<UINT>(1, w);
    h = std::max<UINT>(1, h);
    if (m_drawing) {
        m_pendW = w; m_pendH = h;
        m_resizePending = true;
        return;
    }
    // Hedef yoksa ayni boyut da yeniden denenmeli; yoksa basarisiz bir
    // ResizeBuffers sonrasi pencere kalici olarak bos kalir.
    if (w == m_width && h == m_height && m_target) return;
    m_width = w;
    m_height = h;
    if (!m_swap || m_deviceLost) return; // Begin() cihazi guncel boyutla kuracak
    ReleaseTargetBitmap();
    if (FAILED(m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0)) || !CreateTargetBitmap()) {
        m_deviceLost = true;
    }
}

void Renderer::SetDpi(UINT dpi) {
    if (!dpi || dpi == m_dpi) return;
    m_dpi = dpi;
    m_brushes.clear();
    m_formats.clear();
    if (m_dwrite) { CreateFonts(nullptr); ComputeMetrics(); }
}

void Renderer::SetFontSizePt(float pt) {
    pt = std::clamp(pt, 6.0f, 42.0f);
    if (std::fabs(pt - m_fontPt) < 0.01f) return;
    m_fontPt = pt;
    ComputeMetrics();
}

bool Renderer::SetFontFamily(const std::wstring& family) {
    if (family == m_preferredFamily) return true;
    if (!m_dwrite) { m_preferredFamily = family; return true; }
    // Yuklu olmayan aile CreateFonts'ta sessizce aday listesine duser; once
    // dogrula ki onceki font ve tercih hic degismeden kalsin.
    if (!family.empty()) {
        ComPtr<IDWriteFontCollection> coll;
        UINT32 idx = 0;
        BOOL exists = FALSE;
        if (FAILED(m_dwrite->GetSystemFontCollection(&coll, FALSE)) ||
            FAILED(coll->FindFamilyName(family.c_str(), &idx, &exists)) || !exists) {
            return false;
        }
    }
    const std::wstring previous = m_preferredFamily;
    m_preferredFamily = family;
    if (!CreateFonts(nullptr)) {
        m_preferredFamily = previous;
        CreateFonts(nullptr);
        ComputeMetrics();
        return false;
    }
    m_formats.clear();
    ComputeMetrics();
    return true;
}

// ------------------------------------------------------------------ font ---

bool Renderer::CreateFonts(std::wstring* err) {
    ComPtr<IDWriteFontCollection> coll;
    if (FAILED(m_dwrite->GetSystemFontCollection(&coll, FALSE))) {
        if (err) *err = L"Sistem font koleksiyonu alinamadi.";
        return false;
    }

    std::wstring chosen;
    UINT32 famIndex = 0;
    if (!m_preferredFamily.empty()) {
        BOOL exists = FALSE;
        if (SUCCEEDED(coll->FindFamilyName(m_preferredFamily.c_str(), &famIndex, &exists)) && exists) {
            chosen = m_preferredFamily;
        }
    }
    if (chosen.empty()) {
        for (const wchar_t* cand : theme::MonoFontCandidates) {
            BOOL exists = FALSE;
            if (SUCCEEDED(coll->FindFamilyName(cand, &famIndex, &exists)) && exists) {
                chosen = cand;
                break;
            }
        }
    }
    if (chosen.empty()) {
        if (err) *err = L"Sabit genislikli font bulunamadi.";
        return false;
    }
    m_fm.family = chosen;

    ComPtr<IDWriteFontFamily> family;
    if (FAILED(coll->GetFontFamily(famIndex, &family))) {
        if (err) *err = L"Font ailesi alinamadi.";
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        ComPtr<IDWriteFont> font;
        if (FAILED(family->GetFirstMatchingFont(kFaceWeights[i], DWRITE_FONT_STRETCH_NORMAL,
                                                kFaceStyles[i], &font))) {
            m_faces[i] = m_faces[0];
            continue;
        }
        ComPtr<IDWriteFontFace> face;
        if (FAILED(font->CreateFontFace(&face))) {
            m_faces[i] = m_faces[0];
            continue;
        }
        m_faces[i] = face;
        m_glyphCache[i].clear();
    }
    if (!m_faces[0]) {
        if (err) *err = L"Font yuzeyi olusturulamadi.";
        return false;
    }
    for (int i = 1; i < 4; ++i) if (!m_faces[i]) m_faces[i] = m_faces[0];
    return true;
}

// Fontta olmayan karakterler (emoji, CJK, Nerd ikonlari) sistem yedeklemesiyle
// ciziliyor. Boyut terminal fontuyla ayni olmali; satir araligi hucreye
// sabitlenince ilk satirin taban cizgisi tam m_fm.baseline'a oturuyor.
// Her font/boyut/DPI degisimi ComputeMetrics'ten gectigi icin oradan cagriliyor.
void Renderer::CreateFallbackFormats() {
    m_fallbackLayouts.clear();
    for (int i = 0; i < 4; ++i) {
        m_fallbackFormat[i].Reset();
        if (!m_dwrite) continue;
        ComPtr<IDWriteTextFormat> f;
        if (FAILED(m_dwrite->CreateTextFormat(m_fm.family.c_str(), nullptr,
                                              kFaceWeights[i], kFaceStyles[i],
                                              DWRITE_FONT_STRETCH_NORMAL, m_fm.sizePx, L"", &f)))
            continue;
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        f->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, m_fm.cellH, m_fm.baseline);
        m_fallbackFormat[i] = f;
    }
}

void Renderer::ComputeMetrics() {
    if (!m_faces[0]) return;

    const float sizePx = m_fontPt * (float)m_dpi / 72.0f;
    m_fm.sizePx = sizePx;

    DWRITE_FONT_METRICS fm{};
    m_faces[0]->GetMetrics(&fm);
    const float upem = (float)fm.designUnitsPerEm;
    const float scale = sizePx / upem;

    UINT32 cp = (UINT32)'M';
    UINT16 gi = 0;
    m_faces[0]->GetGlyphIndices(&cp, 1, &gi);
    DWRITE_GLYPH_METRICS gm{};
    m_faces[0]->GetDesignGlyphMetrics(&gi, 1, &gm, FALSE);

    m_fm.cellW = std::max(1.0f, std::floor(gm.advanceWidth * scale + 0.5f));
    m_fm.cellH = std::max(1.0f, std::floor((fm.ascent + fm.descent + fm.lineGap) * scale + 0.5f));
    m_fm.baseline = std::floor((fm.ascent + fm.lineGap * 0.5f) * scale + 0.5f);
    m_fm.underlineThickness = std::max(1.0f, std::floor(fm.underlineThickness * scale + 0.5f));
    m_fm.underlineY = std::min(m_fm.cellH - m_fm.underlineThickness,
                               m_fm.baseline - std::floor(fm.underlinePosition * scale));

    for (auto& c : m_glyphCache) c.clear();
    CreateFallbackFormats();
}

int Renderer::FaceIndex(uint16_t flags) const {
    return ((flags & CF_Bold) ? 1 : 0) | ((flags & CF_Italic) ? 2 : 0);
}

uint16_t Renderer::GlyphFor(int faceIdx, char32_t cp) {
    auto& cache = m_glyphCache[faceIdx];
    auto it = cache.find((uint32_t)cp);
    if (it != cache.end()) return it->second;

    UINT32 c = (UINT32)cp;
    UINT16 gi = 0;
    if (m_faces[faceIdx]) m_faces[faceIdx]->GetGlyphIndices(&c, 1, &gi);
    if (cache.size() < 65536) cache.emplace((uint32_t)cp, gi);
    return gi;
}

// ---------------------------------------------------------------- cizim ----

ID2D1SolidColorBrush* Renderer::Brush(uint32_t color, float alpha) {
    if (!m_ctx) return nullptr;
    const uint64_t key = BrushKey(color, alpha);
    auto it = m_brushes.find(key);
    if (it != m_brushes.end()) return it->second.Get();
    ComPtr<ID2D1SolidColorBrush> b;
    if (FAILED(m_ctx->CreateSolidColorBrush(theme::Rgb(color, alpha), &b))) return nullptr;
    // Arayuz renkleri sinirli ama animasyonlu alfa her karede yeni anahtar
    // uretebilir; sinirsiz buyumesin. D2D bekleyen cizimlerin fircasini
    // kendisi tuttugu icin kare ortasinda bosaltmak guvenli.
    if (m_brushes.size() >= 512) m_brushes.clear();
    auto res = m_brushes.emplace(key, b);
    return res.first->second.Get();
}

ID2D1SolidColorBrush* Renderer::CellBrush(uint32_t color) {
    if (!m_ctx) return nullptr;
    if (!m_cellBrush && FAILED(m_ctx->CreateSolidColorBrush(theme::Rgb(color), &m_cellBrush)))
        return nullptr;
    m_cellBrush->SetColor(theme::Rgb(color));
    return m_cellBrush.Get();
}

IDWriteTextFormat* Renderer::Format(float sizePx, bool semibold, bool mono) {
    const uint64_t key = FormatKey(sizePx, semibold, mono);
    auto it = m_formats.find(key);
    if (it != m_formats.end()) return it->second.Get();

    const wchar_t* family = mono ? m_fm.family.c_str() : theme::UiFont;
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(m_dwrite->CreateTextFormat(
            family, nullptr,
            semibold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            sizePx, L"", &f))) {
        return nullptr;
    }
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    auto res = m_formats.emplace(key, f);
    return res.first->second.Get();
}

bool Renderer::Begin() {
    if (!m_faces[0]) return false; // Init tamamlanmadi ya da Shutdown edildi
    if (m_deviceLost || !m_target) {
        const ULONGLONG now = GetTickCount64();
        if (now >= m_nextRecreateTick) {
            m_nextRecreateTick = now + 500;
            RecreateDevice();
        }
        if (m_deviceLost || !m_target) {
            // Cizim yoksa Present'in vsync beklemesi de yok; cagiran dongu kareyi
            // hemen yeniden ister. Kisa bekleme %100 CPU donmesini onluyor,
            // mesaj gelirse hemen donuyor.
            MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            return false;
        }
    }
    m_ctx->BeginDraw();
    m_drawing = true;
    // Tamamen saydam temizlik: pencere seffafligi buna dayaniyor.
    // Opak olmasi gereken her bolgeyi cagiran taraf kendisi dolduruyor.
    m_ctx->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    return true;
}

void Renderer::End() {
    if (!m_drawing) return;
    m_drawing = false;
    // RECREATE_TARGET, DXGI yuzeyine cizen baglamda cihaz kaybi demek: yalnizca
    // hedef bitmap'i eski cihazda yeniden kurmak hicbir zaman duzelmez.
    const HRESULT hr = m_ctx->EndDraw();
    if (DeviceLostHr(hr) || DeviceLostHr(m_swap->Present(1, 0))) {
        m_deviceLost = true;
        // Bu kare ekrana cikmadi; bir sonraki kare (Begin'de yeniden kurulum)
        // baska bir olay beklemeden gelsin diye WM_PAINT iste.
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (m_resizePending) {
        m_resizePending = false;
        Resize(m_pendW, m_pendH);
    }
}

void Renderer::Fill(const D2D1_RECT_F& r, uint32_t color, float alpha) {
    if (auto* b = Brush(color, alpha)) m_ctx->FillRectangle(r, b);
}

void Renderer::FillRound(const D2D1_RECT_F& r, float radius, uint32_t color, float alpha) {
    if (auto* b = Brush(color, alpha)) {
        m_ctx->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
    }
}

void Renderer::Stroke(const D2D1_RECT_F& r, uint32_t color, float width, float alpha) {
    if (auto* b = Brush(color, alpha)) {
        D2D1_RECT_F inset = D2D1::RectF(r.left + width * 0.5f, r.top + width * 0.5f,
                                        r.right - width * 0.5f, r.bottom - width * 0.5f);
        m_ctx->DrawRectangle(inset, b, width);
    }
}

void Renderer::StrokeRound(const D2D1_RECT_F& r, float radius, uint32_t color, float width, float alpha) {
    if (auto* b = Brush(color, alpha)) {
        D2D1_RECT_F inset = D2D1::RectF(r.left + width * 0.5f, r.top + width * 0.5f,
                                        r.right - width * 0.5f, r.bottom - width * 0.5f);
        float rad = std::max(0.0f, radius - width * 0.5f);
        m_ctx->DrawRoundedRectangle(D2D1::RoundedRect(inset, rad, rad), b, width);
    }
}


void Renderer::Line(float x0, float y0, float x1, float y1, uint32_t color, float width) {
    if (auto* b = Brush(color, 1.0f)) {
        m_ctx->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), b, width);
    }
}

void Renderer::Disc(float cx, float cy, float r, uint32_t color, float alpha) {
    if (auto* b = Brush(color, alpha)) {
        m_ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b);
    }
}

void Renderer::Ring(float cx, float cy, float r, uint32_t color, float width, float alpha) {
    if (auto* b = Brush(color, alpha)) {
        m_ctx->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b, width);
    }
}

void Renderer::Text(const std::wstring& s, const D2D1_RECT_F& r, uint32_t color,
                    float sizePx, Align align, bool semibold, bool mono, float alpha,
                    bool wrap) {
    if (s.empty()) return;
    IDWriteTextFormat* f = Format(sizePx, semibold, mono);
    ID2D1SolidColorBrush* b = Brush(color, alpha);
    if (!f || !b) return;
    f->SetTextAlignment(align == Align::Left   ? DWRITE_TEXT_ALIGNMENT_LEADING
                      : align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                               : DWRITE_TEXT_ALIGNMENT_TRAILING);
    f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    f->SetParagraphAlignment(wrap ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                  : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_ctx->DrawTextW(s.c_str(), (UINT32)s.size(), f, r, b,
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Renderer::MeasureText(const std::wstring& s, float sizePx, bool semibold, bool mono) {
    if (s.empty()) return 0.0f;
    IDWriteTextFormat* f = Format(sizePx, semibold, mono);
    if (!f) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, 100000.0f, 100.0f, &layout)))
        return 0.0f;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    return tm.widthIncludingTrailingWhitespace;
}

void Renderer::PushClip(const D2D1_RECT_F& r) {
    m_ctx->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
}

void Renderer::PopClip() {
    m_ctx->PopAxisAlignedClip();
}

// ------------------------------------------------------------- terminal ----

void Renderer::DrawTerminal(const Screen& s, const D2D1_RECT_F& area,
                            bool focused, bool cursorOn, int cursorStyle,
                            const Selection& sel, float bgAlpha) {
    if (!m_drawing || !m_faces[0]) return;
    const int cols = s.Cols();
    const int rows = s.Rows();
    if (cols <= 0 || rows <= 0) return;

    const float cw = m_fm.cellW;
    const float chh = m_fm.cellH;

    Fill(area, theme::TermBg, bgAlpha);
    PushClip(area);

    m_row.resize((size_t)cols);
    m_fg.resize((size_t)cols);
    m_bg.resize((size_t)cols);
    m_fallback.clear();

    int sy0 = sel.y0, sy1 = sel.y1, sx0 = sel.x0, sx1 = sel.x1;
    if (sel.active && (sy1 < sy0 || (sy1 == sy0 && sx1 < sx0))) {
        std::swap(sy0, sy1);
        std::swap(sx0, sx1);
    }

    for (int y = 0; y < rows; ++y) {
        const float ry = area.top + y * chh;
        if (ry >= area.bottom) break;

        s.CopyViewRow(y, m_row.data());

        int selFrom = -1, selTo = -1;
        if (sel.active && y >= sy0 && y <= sy1) {
            selFrom = (y == sy0) ? std::max(0, sx0) : 0;
            selTo   = (y == sy1) ? std::min(cols, sx1 + 1) : cols;
        }

        int lastCol = -1;
        for (int x = 0; x < cols; ++x) {
            const bool selected = (selFrom >= 0 && x >= selFrom && x < selTo);
            ResolveCell(m_row[(size_t)x], selected, m_fg[(size_t)x], m_bg[(size_t)x]);
            if (!PlainSpace(m_row[(size_t)x]) || m_bg[(size_t)x] != theme::TermBg) lastCol = x;
        }
        if (lastCol < 0) continue;

        // --- 1) arka plan kosullari ---
        for (int x = 0; x <= lastCol; ) {
            const uint32_t bg = m_bg[(size_t)x];
            int start = x;
            while (x <= lastCol && m_bg[(size_t)x] == bg) ++x;
            if (bg != theme::TermBg) {
                if (auto* b = CellBrush(bg))
                    m_ctx->FillRectangle(D2D1::RectF(area.left + start * cw, ry, area.left + x * cw, ry + chh), b);
            }
        }

        // --- 2) glyph kosullari ---
        int x = 0;
        while (x <= lastCol) {
            // Bos hucreleri atla, kosu orada baslasin.
            while (x <= lastCol && PlainSpace(m_row[(size_t)x])) ++x;
            if (x > lastCol) break;

            const uint16_t f0 = m_row[(size_t)x].flags;
            const uint32_t fg = m_fg[(size_t)x];
            const int fi = FaceIndex(f0);
            const bool ul = ((f0 & CF_Underline) != 0) || (m_row[(size_t)x].linkId != 0);
            const bool st = (f0 & CF_Strike) != 0;
            const float runX = area.left + x * cw;

            m_glyphs.clear();
            m_advances.clear();

            while (x <= lastCol) {
                const Cell& c = m_row[(size_t)x];
                const bool cellUl = ((c.flags & CF_Underline) != 0) || (c.linkId != 0);
                if (m_fg[(size_t)x] != fg || FaceIndex(c.flags) != fi ||
                    cellUl != ul ||
                    ((c.flags & CF_Strike) != 0) != st) break;

                const bool wide = (x + 1 < cols) && (m_row[(size_t)x + 1].flags & CF_WideTail);
                char32_t ch = c.ch;
                if ((c.flags & CF_Hidden) || ch == 0 || (c.flags & CF_WideTail)) ch = U' ';

                uint16_t g = GlyphFor(fi, ch);
                if (g == 0 && ch != U' ') {
                    m_fallback.push_back({ area.left + x * cw, ry, ch, fg, fi });
                    g = GlyphFor(fi, U' ');
                }
                m_glyphs.push_back(g);
                m_advances.push_back(wide ? cw * 2.0f : cw);
                x += wide ? 2 : 1;
            }

            if (m_glyphs.empty()) continue;

            DWRITE_GLYPH_RUN run{};
            run.fontFace = m_faces[fi].Get();
            run.fontEmSize = m_fm.sizePx;
            run.glyphCount = (UINT32)m_glyphs.size();
            run.glyphIndices = m_glyphs.data();
            run.glyphAdvances = m_advances.data();
            run.glyphOffsets = nullptr;
            run.isSideways = FALSE;
            run.bidiLevel = 0;

            if (auto* b = CellBrush(fg)) {
                m_ctx->DrawGlyphRun(D2D1::Point2F(runX, ry + m_fm.baseline), &run, b,
                                    DWRITE_MEASURING_MODE_NATURAL);

                if (ul || st) {
                    float w = 0.0f;
                    for (float a : m_advances) w += a;
                    if (ul) {
                        m_ctx->FillRectangle(D2D1::RectF(runX, ry + m_fm.underlineY, runX + w,
                                                         ry + m_fm.underlineY + m_fm.underlineThickness), b);
                    }
                    if (st) {
                        const float sy = ry + m_fm.baseline - m_fm.sizePx * 0.28f;
                        m_ctx->FillRectangle(D2D1::RectF(runX, sy, runX + w, sy + m_fm.underlineThickness), b);
                    }
                }
            }
        }
    }

    // --- 3) fontta bulunmayan karakterler: sistem yedeklemesiyle ---
    for (const auto& f : m_fallback) DrawFallbackGlyph(f.x, f.y, f.ch, f.fi, CellBrush(f.fg));

    // --- 4) imlec ---
    int cx = 0, cy = 0;
    if (s.CursorVisible() && s.CursorInView(cx, cy) && cx >= 0 && cx < cols && cy >= 0 && cy < rows) {
        s.CopyViewRow(cy, m_row.data());
        const Cell& c = m_row[(size_t)cx];
        // Genis karakterin ilk hucresindeyse imlec iki hucreyi kaplar; yoksa
        // yeniden cizilen glifin sag yarisi blok disinda kalir.
        const bool wideLead = (cx + 1 < cols) && (m_row[(size_t)cx + 1].flags & CF_WideTail);
        const float x = area.left + cx * cw;
        const float y = area.top + cy * chh;
        D2D1_RECT_F r = D2D1::RectF(x, y, x + (wideLead ? cw * 2.0f : cw), y + chh);
        const bool bar = (cursorStyle == 5 || cursorStyle == 6);
        const bool under = (cursorStyle == 3 || cursorStyle == 4);
        if (under) r.top = r.bottom - std::max(2.0f, chh * 0.12f);
        if (bar)   r.right = r.left + std::max(2.0f, cw * 0.18f);

        if (!focused) {
            Stroke(r, theme::TermCursor, 1.0f, 0.8f);
        } else if (cursorOn) {
            Fill(r, theme::TermCursor);
            // Blok imlecin altindaki karakter ters renkte yeniden ciziliyor. Gizli
            // (SGR 8) hucre acik edilmesin; kirpma glifin blok disina tasmasini onler.
            if (!bar && !under && c.ch && c.ch != U' ' && !(c.flags & (CF_WideTail | CF_Hidden))) {
                const int fi = FaceIndex(c.flags);
                const uint16_t g = GlyphFor(fi, c.ch);
                if (auto* b = Brush(theme::TermBg, 1.0f)) {
                    PushClip(r);
                    if (g != 0) {
                        FLOAT adv = r.right - r.left;
                        DWRITE_GLYPH_RUN run{};
                        run.fontFace = m_faces[fi].Get();
                        run.fontEmSize = m_fm.sizePx;
                        run.glyphCount = 1;
                        run.glyphIndices = &g;
                        run.glyphAdvances = &adv;
                        m_ctx->DrawGlyphRun(D2D1::Point2F(x, y + m_fm.baseline), &run, b,
                                            DWRITE_MEASURING_MODE_NATURAL);
                    } else {
                        DrawFallbackGlyph(x, y, c.ch, fi, b);
                    }
                    PopClip();
                }
            }
        }
    }

    PopClip();
}

void Renderer::DrawFallbackGlyph(float x, float y, char32_t ch, int fi, ID2D1Brush* brush) {
    if (!brush || fi < 0 || fi > 3 || !m_fallbackFormat[fi] || !m_dwrite) return;
    const uint64_t key = ((uint64_t)ch << 2) | (uint64_t)fi;
    IDWriteTextLayout* layout = nullptr;
    auto it = m_fallbackLayouts.find(key);
    if (it != m_fallbackLayouts.end()) {
        layout = it->second.Get();
    } else {
        wchar_t buf[2];
        UINT32 n = 0;
        if (ch >= 0x10000) {
            const char32_t v = ch - 0x10000;
            buf[n++] = (wchar_t)(0xD800 + (v >> 10));
            buf[n++] = (wchar_t)(0xDC00 + (v & 0x3FF));
        } else {
            buf[n++] = (wchar_t)ch;
        }
        // Genislik 2 hucre: dar hucredeki ikon da genis emoji de sigsin; fazlasi kirpilir.
        ComPtr<IDWriteTextLayout> l;
        if (FAILED(m_dwrite->CreateTextLayout(buf, n, m_fallbackFormat[fi].Get(),
                                              m_fm.cellW * 2.0f, m_fm.cellH, &l)))
            return;
        if (m_fallbackLayouts.size() >= 4096) m_fallbackLayouts.clear();
        layout = m_fallbackLayouts.emplace(key, l).first->second.Get();
    }
    m_ctx->DrawTextLayout(D2D1::Point2F(x, y), layout, brush,
                          D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

bool Renderer::SaveScreenshot(const wchar_t* filename) {
    if (!m_swap || !m_d3d || !m_d3dCtx) return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swap->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    hr = m_d3d->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr)) return false;

    m_d3dCtx->CopyResource(staging.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = m_d3dCtx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    IWICImagingFactory* pFactory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (SUCCEEDED(hr)) {
        IWICBitmapEncoder* pEncoder = nullptr;
        IWICStream* pStream = nullptr;
        hr = pFactory->CreateStream(&pStream);
        if (SUCCEEDED(hr)) hr = pStream->InitializeFromFilename(filename, GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = pFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &pEncoder);
        if (SUCCEEDED(hr)) hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);

        IWICBitmapFrameEncode* pFrame = nullptr;
        if (SUCCEEDED(hr)) hr = pEncoder->CreateNewFrame(&pFrame, nullptr);
        if (SUCCEEDED(hr)) hr = pFrame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = pFrame->SetSize(desc.Width, desc.Height);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(hr)) hr = pFrame->SetPixelFormat(&format);

        if (SUCCEEDED(hr)) {
            std::vector<uint32_t> rowBuf(desc.Width);
            BYTE* srcRow = (BYTE*)mapped.pData;
            for (UINT y = 0; y < desc.Height; ++y) {
                uint32_t* srcPixels = (uint32_t*)(srcRow + y * mapped.RowPitch);
                for (UINT x = 0; x < desc.Width; ++x) {
                    rowBuf[x] = srcPixels[x] | 0xFF000000u; // Force opaque alpha for preview
                }
                pFrame->WritePixels(1, desc.Width * sizeof(uint32_t), desc.Width * sizeof(uint32_t), (BYTE*)rowBuf.data());
            }
            pFrame->Commit();
            pEncoder->Commit();
        }

        if (pFrame) pFrame->Release();
        if (pEncoder) pEncoder->Release();
        if (pStream) pStream->Release();
        pFactory->Release();
    }

    m_d3dCtx->Unmap(staging.Get(), 0);
    return SUCCEEDED(hr);
}

} // namespace ft
