#pragma once
//
// FullTerminal gorsel kimligi.
//
// Gety ve GhostView'in "Obsidian" ailesinden geliyor ama ayni degil:
// oradaki mavi-siyah yuzeyler yerine daha notr, hafif soguk gri-siyah;
// vurgu rengi cyan (#00a2ff) yerine jade/teal (#45b5ac). Uyari amber,
// hata mercan. Her kabuk profili kendi rozet rengini tasiyor.
//
#include <windows.h>
#include <d2d1.h>
#include <cstdint>

namespace ft::theme {

// ---- Yuzeyler -------------------------------------------------------------
constexpr uint32_t Base      = 0x0E1116; // en derin zemin, pencere arka plani
constexpr uint32_t Surface   = 0x161A21; // panel, sekme seridi
constexpr uint32_t Elevated  = 0x1D232C; // hover, aktif sekme
constexpr uint32_t Sunken    = 0x0A0D11; // terminal tuvali
constexpr uint32_t Border    = 0x262D38;
constexpr uint32_t BorderHi  = 0x36404F;
constexpr uint32_t BgCard    = 0x1A2029;

// ---- Metin ----------------------------------------------------------------
constexpr uint32_t TextHi    = 0xE7EBF1;
constexpr uint32_t Text      = 0xBAC3D0;
constexpr uint32_t TextMuted = 0x8994A5;
constexpr uint32_t TextDim   = 0x5E6879;

// ---- Vurgu ----------------------------------------------------------------
constexpr uint32_t Accent    = 0x45B5AC; // jade
constexpr uint32_t AccentHi  = 0x6BD6CD;
constexpr uint32_t AccentDim = 0x1E4E4B;
constexpr uint32_t Amber     = 0xEE9B52;
constexpr uint32_t Green     = 0x5BC98C;
constexpr uint32_t Red       = 0xF0685C;
constexpr uint32_t Violet    = 0x9B8CF7;

// ---- Terminal varsayilanlari ve Renk Gamı Semalari -------------------------
inline uint32_t TermFg     = 0xD3DAE4;
inline uint32_t TermBg     = 0x0A0D11;
inline uint32_t TermCursor = 0x45B5AC;
inline uint32_t TermSel    = 0x2A4A52;

// ---- ANSI 16 --------------------------------------------------------------
inline uint32_t Ansi[16] = {
    0x1B212A, 0xE05E52, 0x54C98A, 0xE0A458,
    0x5A9CE8, 0xA98CF0, 0x45B5AC, 0xC3CBD8,
    0x3A4453, 0xF07E72, 0x74E0A6, 0xF2C078,
    0x7BB6F5, 0xC0A8FF, 0x6BD6CD, 0xEEF2F7,
};

struct ColorSchemeDef {
    const wchar_t* name;
    uint32_t bg;
    uint32_t fg;
    uint32_t cursor;
    uint32_t sel;
    uint32_t ansi[16];
};

inline const ColorSchemeDef Schemes[8] = {
    // 0: Obsidian (FullTerminal varsayilani)
    { L"Obsidian", 0x0A0D11, 0xD3DAE4, 0x45B5AC, 0x2A4A52, {
        0x1B212A, 0xE05E52, 0x54C98A, 0xE0A458, 0x5A9CE8, 0xA98CF0, 0x45B5AC, 0xC3CBD8,
        0x3A4453, 0xF07E72, 0x74E0A6, 0xF2C078, 0x7BB6F5, 0xC0A8FF, 0x6BD6CD, 0xEEF2F7
    }},
    // 1: Visor Dark
    { L"Visor Dark", 0x1E2229, 0xD3D7CF, 0x4E9A06, 0x3465A4, {
        0x2E3436, 0xCC0000, 0x4E9A06, 0xC4A000, 0x3465A4, 0x75507B, 0x06989A, 0xD3D7CF,
        0x555753, 0xEF2929, 0x8AE234, 0xFCE94F, 0x729FCF, 0xAD7FA8, 0x34E2E2, 0xEEEEEC
    }},
    // 2: Dracula
    { L"Dracula", 0x282A36, 0xF8F8F2, 0xBD93F9, 0x44475A, {
        0x21222C, 0xFF5555, 0x50FA7B, 0xF1FA8C, 0xBD93F9, 0xFF79C6, 0x8BE9FD, 0xF8F8F2,
        0x6272A4, 0xFF6E6E, 0x69FF94, 0xFFFFA5, 0xD6ACFF, 0xFF92DF, 0xA4FFFF, 0xFFFFFF
    }},
    // 3: Monokai Pro
    { L"Monokai Pro", 0x2D2A2E, 0xFCFCFA, 0xFFD866, 0x403E41, {
        0x403E41, 0xFF6188, 0xA9DC76, 0xFFD866, 0x78DCE8, 0xAB9DF2, 0x78DCE8, 0xFCFCFA,
        0x727072, 0xFF6188, 0xA9DC76, 0xFFD866, 0x78DCE8, 0xAB9DF2, 0x78DCE8, 0xFFFFFF
    }},
    // 4: Solarized Dark
    { L"Solarized Dark", 0x002B36, 0x839496, 0x2AA198, 0x073642, {
        0x073642, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5,
        0x002B36, 0xCB4B16, 0x586E75, 0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3
    }},
    // 5: Nord
    { L"Nord", 0x2E3440, 0xD8DEE9, 0x88C0D0, 0x434C5E, {
        0x3B4252, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x88C0D0, 0xE5E9F0,
        0x4C566A, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x8FBCBB, 0xECEFF4
    }},
    // 6: Cyberpunk
    { L"Cyberpunk", 0x1A102F, 0x00FFCC, 0xFF007F, 0x341A59, {
        0x100A1C, 0xFF0055, 0x00FF9F, 0xFFE600, 0x00B8FF, 0xB800FF, 0x00FFCC, 0xF1F1F1,
        0x3A205A, 0xFF3377, 0x33FFB2, 0xFFEB33, 0x33C6FF, 0xC633FF, 0x33FFD6, 0xFFFFFF
    }},
    // 7: Retro CRT Yesil
    { L"Retro CRT", 0x051205, 0x33FF33, 0x33FF33, 0x144014, {
        0x051505, 0x22AA22, 0x33FF33, 0x44DD44, 0x228822, 0x33CC33, 0x55FF55, 0x88FF88,
        0x0A280A, 0x33CC33, 0x44FF44, 0x66FF66, 0x33AA33, 0x44DD44, 0x77FF77, 0xAAFFAA
    }}
};

// ---- Yardimcilar ----------------------------------------------------------
inline D2D1_COLOR_F Rgb(uint32_t hex, float alpha = 1.0f) {
    return D2D1::ColorF(
        ((hex >> 16) & 0xFF) / 255.0f,
        ((hex >> 8) & 0xFF) / 255.0f,
        (hex & 0xFF) / 255.0f,
        alpha);
}

inline uint32_t Mix(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int shift) {
        float x = (float)((a >> shift) & 0xFF);
        float y = (float)((b >> shift) & 0xFF);
        return (uint32_t)(x + (y - x) * t) & 0xFFu;
    };
    return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

inline void SetColorScheme(int index, uint32_t customBg = 0x0A0D11, uint32_t customFg = 0xD3DAE4) {
    if (index >= 0 && index < 8) {
        const auto& s = Schemes[index];
        TermBg = s.bg;
        TermFg = s.fg;
        TermCursor = s.cursor;
        TermSel = s.sel;
        for (int i = 0; i < 16; ++i) Ansi[i] = s.ansi[i];
    } else {
        // Ozel (Custom)
        TermBg = customBg;
        TermFg = customFg;
        TermCursor = customFg;
        TermSel = Mix(customBg, customFg, 0.35f);
    }
}

// xterm 256 renk kupu -> RGB
inline uint32_t Xterm256(int index) {
    if (index < 0) index = 0;
    if (index < 16) return Ansi[index];
    if (index < 232) {
        int i = index - 16;
        static const int lvl[6] = { 0, 95, 135, 175, 215, 255 };
        int r = lvl[(i / 36) % 6];
        int g = lvl[(i / 6) % 6];
        int b = lvl[i % 6];
        return (uint32_t)((r << 16) | (g << 8) | b);
    }
    if (index < 256) {
        int v = 8 + (index - 232) * 10;
        return (uint32_t)((v << 16) | (v << 8) | v);
    }
    return TermFg;
}

// ---- Calisma zamaninda degisen vurgu rengi (Ayarlar > Gorunum) -----------
inline uint32_t g_accent    = Accent;
inline uint32_t g_accentHi  = AccentHi;
inline uint32_t g_accentDim = AccentDim;

inline void SetAccent(uint32_t a) {
    g_accent = a;
    g_accentHi = Mix(a, 0xFFFFFF, 0.30f);
    g_accentDim = Mix(a, 0x000000, 0.62f);
}
inline uint32_t Ac()    { return g_accent; }
inline uint32_t AcHi()  { return g_accentHi; }
inline uint32_t AcDim() { return g_accentDim; }

// Kullanicinin secebilecegi vurgu paleti.
inline const uint32_t AccentChoices[5] = { 0x45B5AC, 0x5A9CE8, 0xEE9B52, 0x9B8CF7, 0xF0685C };
inline const wchar_t* const AccentNames[5] = { L"Jade", L"Mavi", L"Amber", L"Mor", L"Mercan" };

// ---- Olculer (96 DPI tabani, dpiScale ile carpilir) -----------------------
constexpr float TitleBarH = 38.0f;
constexpr float StatusBarH = 24.0f;
constexpr float AccentRailW = 3.0f;   // sol kenardaki profil rengi seridi
constexpr float TermPadX = 10.0f;
constexpr float TermPadY = 6.0f;

// ---- Font -----------------------------------------------------------------
// Sirayla denenir, ilk bulunan kullanilir.
inline const wchar_t* const MonoFontCandidates[] = {
    L"Cascadia Mono",
    L"Cascadia Code",
    L"JetBrains Mono",
    L"Consolas",
    L"Lucida Console",
};
constexpr float MonoSizePt = 11.0f;

inline const wchar_t* UiFont = L"Segoe UI";
inline const wchar_t* IconFont = L"Segoe Fluent Icons";       // Win11
inline const wchar_t* IconFontFallback = L"Segoe MDL2 Assets"; // Win10

} // namespace ft::theme
