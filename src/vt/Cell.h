#pragma once
#include <cstdint>

namespace ft {

enum CellFlags : uint16_t {
    CF_None      = 0,
    CF_Bold      = 1u << 0,
    CF_Dim       = 1u << 1,
    CF_Italic    = 1u << 2,
    CF_Underline = 1u << 3,
    CF_Blink     = 1u << 4,
    CF_Inverse   = 1u << 5,
    CF_Hidden    = 1u << 6,
    CF_Strike    = 1u << 7,
    CF_FgDefault = 1u << 8,
    CF_BgDefault = 1u << 9,
    CF_WideTail  = 1u << 10, // genis karakterin ikinci hucresi, cizilmez
    CF_FgIndexed = 1u << 11, // fg bir renk paleti indeksidir (0..255, theme::Xterm256 ile cozulur)
    CF_BgIndexed = 1u << 12, // bg bir renk paleti indeksidir (0..255, theme::Xterm256 ile cozulur)
};

constexpr uint16_t CF_DefaultColors = CF_FgDefault | CF_BgDefault;

// Aktif yazim ozellikleri (SGR durumu).
struct Pen {
    uint32_t fg = 0;
    uint32_t bg = 0;
    uint16_t flags = CF_DefaultColors;

    void Reset() { fg = 0; bg = 0; flags = CF_DefaultColors; }
};

struct Cell {
    char32_t ch    = U' ';
    uint32_t fg    = 0;
    uint32_t bg    = 0;
    uint16_t flags = CF_DefaultColors;
    uint16_t linkId = 0;

    bool SameStyle(const Cell& o) const {
        return fg == o.fg && bg == o.bg && flags == o.flags && linkId == o.linkId;
    }
    bool IsBlank() const {
        return (ch == U' ' || ch == 0) && (flags & CF_BgDefault) && !(flags & CF_Inverse);
    }
    void Clear(const Pen& p) {
        ch = U' ';
        fg = p.fg;
        bg = p.bg;
        // Silinen hucre arka plani kalemden gelir ama metin ozellikleri gitmeli.
        flags = (uint16_t)(p.flags & (CF_FgDefault | CF_BgDefault | CF_FgIndexed | CF_BgIndexed));
        linkId = 0;
    }
};

static_assert(sizeof(Cell) == 16, "Cell 16 bayt olmali");

} // namespace ft
