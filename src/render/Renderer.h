#pragma once
//
// D3D11 + DXGI + DirectComposition tabani, uzerinde D2D/DirectWrite.
//
// Terminal metni hucre hucre DrawText ile degil, ayni bicimdeki hucreler tek
// bir DWRITE_GLYPH_RUN'a toplanip DrawGlyphRun ile ciziliyor. Glyph indisleri
// onbellekte tutuluyor, rasterizasyonu D2D'nin kendi atlasi yapiyor.
// Olcumler NFR-007 ve NFR-009'u karsilamazsa, sonraki adim kendi HLSL atlas
// motorumuz (03-MIMARI 5.2).
//
#include "vt/Screen.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <map>

namespace ft {

struct FontMetrics {
    float cellW = 8.0f;
    float cellH = 16.0f;
    float baseline = 12.0f;
    float underlineY = 14.0f;
    float underlineThickness = 1.0f;
    float sizePx = 14.0f;
    std::wstring family = L"Consolas";
};

struct Selection {
    bool active = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // gorunum koordinatlari, dahil
};

class Renderer {
public:
    enum class Align { Left, Center, Right };

    bool Init(HWND hwnd, UINT dpi, std::wstring* err);
    void Shutdown();
    // Font ve olcumler hazir. GPU cihazi kaybolsa da true kalir: olcumler
    // cihazdan bagimsiz, Begin() cihazi yeniden kurmayi kendisi deniyor.
    bool Ready() const { return m_faces[0] != nullptr; }

    void Resize(UINT pxW, UINT pxH);
    void SetDpi(UINT dpi);
    void SetFontSizePt(float pt);
    // Bos birakilirsa aday listesinden ilk bulunan kullanilir.
    bool SetFontFamily(const std::wstring& family);

    UINT  Width()  const { return m_width; }
    UINT  Height() const { return m_height; }
    float Scale()  const { return m_dpi / 96.0f; }
    const FontMetrics& Metrics() const { return m_fm; }

    bool Begin();
    void End();

    // ---- kabuk ----------------------------------------------------------
    void Fill(const D2D1_RECT_F& r, uint32_t color, float alpha = 1.0f);
    void FillRound(const D2D1_RECT_F& r, float radius, uint32_t color, float alpha = 1.0f);
    void Stroke(const D2D1_RECT_F& r, uint32_t color, float width = 1.0f, float alpha = 1.0f);
    void StrokeRound(const D2D1_RECT_F& r, float radius, uint32_t color, float width = 1.0f, float alpha = 1.0f);
    void Line(float x0, float y0, float x1, float y1, uint32_t color, float width = 1.0f);
    void Disc(float cx, float cy, float r, uint32_t color, float alpha = 1.0f);
    void Ring(float cx, float cy, float r, uint32_t color, float width = 1.0f, float alpha = 1.0f);
    void Text(const std::wstring& s, const D2D1_RECT_F& r, uint32_t color,
              float sizePx, Align align = Align::Left,
              bool semibold = false, bool mono = false, float alpha = 1.0f,
              bool wrap = false);
    float MeasureText(const std::wstring& s, float sizePx, bool semibold = false, bool mono = false);
    void PushClip(const D2D1_RECT_F& r);
    void PopClip();

    // ---- terminal -------------------------------------------------------
    // bgAlpha < 1 ise varsayilan arka plan saydam kalir (pencere seffafligi).
    void DrawTerminal(const Screen& screen, const D2D1_RECT_F& area,
                      bool focused, bool cursorOn, int cursorStyle,
                      const Selection& sel, float bgAlpha = 1.0f);

    bool SaveScreenshot(const wchar_t* filename);

private:
    using Face = Microsoft::WRL::ComPtr<IDWriteFontFace>;

    bool CreateDeviceResources(std::wstring* err, bool allowWarp = true);
    void ReleaseDeviceResources();
    bool RecreateDevice();
    bool CreateTargetBitmap();
    void ReleaseTargetBitmap();
    bool CreateFonts(std::wstring* err);
    void ComputeMetrics();
    void CreateFallbackFormats();

    ID2D1SolidColorBrush* Brush(uint32_t color, float alpha);
    // Terminal hucreleri icin tek, rengi her cizimde degistirilen firca:
    // truecolor ciktisi sinirsiz sayida renk uretir, onbellege alinmamali.
    ID2D1SolidColorBrush* CellBrush(uint32_t color);
    void DrawFallbackGlyph(float x, float y, char32_t ch, int faceIdx, ID2D1Brush* brush);
    IDWriteTextFormat* Format(float sizePx, bool semibold, bool mono);
    int FaceIndex(uint16_t flags) const;
    uint16_t GlyphFor(int faceIdx, char32_t cp);

    Microsoft::WRL::ComPtr<ID3D11Device>          m_d3d;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>   m_d3dCtx;
    Microsoft::WRL::ComPtr<IDXGISwapChain1>       m_swap;
    Microsoft::WRL::ComPtr<ID2D1Factory1>         m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1Device>           m_d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext>    m_ctx;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1>          m_target;
    Microsoft::WRL::ComPtr<IDWriteFactory>        m_dwrite;
    Microsoft::WRL::ComPtr<IDCompositionDevice>   m_dcomp;
    Microsoft::WRL::ComPtr<IDCompositionTarget>   m_dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual>   m_visual;

    Face m_faces[4];                   // 0 duz, 1 kalin, 2 italik, 3 kalin italik
    std::unordered_map<uint32_t, uint16_t> m_glyphCache[4];
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_fallbackFormat[4];   // yuz indisiyle ayni sira
    // (kod noktasi << 2 | yuz) -> hazir yerlesim; her karede yeniden yerlesim
    // ve font yedeklemesi aramasi yapilmasin.
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<IDWriteTextLayout>> m_fallbackLayouts;

    std::map<uint64_t, Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>> m_brushes;
    std::map<uint64_t, Microsoft::WRL::ComPtr<IDWriteTextFormat>>    m_formats;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_cellBrush;

    HWND  m_hwnd = nullptr;
    UINT  m_width = 0, m_height = 0;
    // BeginDraw/EndDraw arasinda gelen boyut degisikligi (ornegin bir dugme
    // pencere modunu degistirip WM_SIZE'i eszamanli tetikledi) hedef bitmap'i
    // cizimin ortasinda birakamaz; End()'e ertelenir.
    bool  m_drawing = false;
    bool  m_resizePending = false;
    UINT  m_pendW = 0, m_pendH = 0;
    // GPU cihazi kayboldu (surucu guncellemesi, TDR, eGPU cikarildi). Begin()
    // araliklarla yeniden kurmayi dener; basarisizken donguyu dondurmez.
    bool  m_deviceLost = false;
    ULONGLONG m_nextRecreateTick = 0;
    int   m_recreateFails = 0;
    UINT  m_dpi = 96;
    float m_fontPt = 11.0f;
    std::wstring m_preferredFamily;
    FontMetrics m_fm;

    // Cizim sirasinda yeniden kullanilan tamponlar
    std::vector<Cell>     m_row;
    std::vector<uint32_t> m_fg;
    std::vector<uint32_t> m_bg;
    std::vector<UINT16>   m_glyphs;
    std::vector<FLOAT>    m_advances;
    struct FallbackGlyph { float x, y; char32_t ch; uint32_t fg; int fi; };
    std::vector<FallbackGlyph> m_fallback;
};

} // namespace ft
