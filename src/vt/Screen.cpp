#include "vt/Screen.h"
#include "core/Utf8.h"

#include <algorithm>

namespace ft {
namespace {
inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

void Screen::Resize(int cols, int rows) {
    cols = Clamp(cols, 1, kMaxCols);
    rows = Clamp(rows, 1, kMaxRows);
    if (cols == m_cols && rows == m_rows) return;

    Cell blank;
    blank.Clear(Pen{});

    std::vector<Cell> np((size_t)cols * rows, blank);
    std::vector<Cell> na((size_t)cols * rows, blank);
    std::vector<uint8_t> nwp((size_t)rows, 0);
    std::vector<uint8_t> nwa((size_t)rows, 0);

    // Kucultmede ustten yalnizca imleci gorunur tutmaya yetecek kadar satir
    // atilir (xterm gibi); imlecin altindaki satirlar alttan kesilir. Yoksa her
    // zoom/Quake boyu degisiminde banner ve prompt scrollback'e kopyalanir.
    // Alt ekrandayken birincil tamponun imleci m_saved'da (1049) durur.
    int dropP = 0, dropA = 0;
    int pullP = 0;
    if (m_cols > 0 && m_rows > 0) {
        const int copyCols = std::min(m_cols, cols);
        const int curP = Clamp(m_useAlt ? m_saved.y : m_cy, 0, m_rows - 1);
        const int curA = Clamp(m_useAlt ? m_cy : m_savedAlt.y, 0, m_rows - 1);
        dropP = std::max(0, curP - (rows - 1));
        dropA = std::max(0, curA - (rows - 1));
        // Sutun sayisi degisince eski sarma bayraklari anlamsiz; conhost
        // yeniden cizerken tekrar kurulur.
        const bool keepWrap = (cols == m_cols);

        // Ustten dusen satirlar tarihe gitsin (yeniden akitma FR-TERM-008)
        if (dropP > 0) {
            for (int i = 0; i < dropP && !m_primary.empty(); ++i) {
                PushHistory(m_primary.data() + (size_t)i * m_cols, m_cols,
                            m_wrapPrimary[(size_t)i] != 0);
            }
            m_shrinkDropped += dropP;
        }

        // Genislemede (tam ekran / resize buyutme) yalnizca kucultmede atilan satirlar geri cekilsin
        if (!m_useAlt && !m_scrollback.empty() && rows > m_rows && m_shrinkDropped > 0) {
            const int maxPull = std::min((int)m_scrollback.size(), m_shrinkDropped);
            pullP = std::min(maxPull, rows - m_rows);
            m_shrinkDropped -= pullP;
            const size_t sbStart = m_scrollback.size() - (size_t)pullP;
            for (int k = 0; k < pullP; ++k) {
                const SbRow& sr = m_scrollback[sbStart + (size_t)k];
                const int numCells = std::min((int)sr.cells.size(), cols);
                if (numCells > 0) {
                    std::copy_n(sr.cells.data(), numCells, np.data() + (size_t)k * cols);
                }
                nwp[(size_t)k] = sr.wrapped ? 1 : 0;
            }
            m_scrollback.erase(m_scrollback.end() - pullP, m_scrollback.end());
        }

        const int copyP = std::min(m_rows - dropP, rows - pullP);
        const int copyA = std::min(m_rows - dropA, rows);
        for (int i = 0; i < copyP && !m_primary.empty(); ++i) {
            const int src = dropP + i;
            const int dst = pullP + i;
            std::copy_n(m_primary.data() + (size_t)src * m_cols, copyCols,
                        np.data() + (size_t)dst * cols);
            if (keepWrap) nwp[(size_t)dst] = m_wrapPrimary[(size_t)src];
        }
        for (int i = 0; i < copyA && !m_alt.empty(); ++i) {
            const int src = dropA + i;
            std::copy_n(m_alt.data() + (size_t)src * m_cols, copyCols,
                        na.data() + (size_t)i * cols);
            if (keepWrap) nwa[(size_t)i] = m_wrapAlt[(size_t)src];
        }
    }

    m_primary.swap(np);
    m_alt.swap(na);
    m_wrapPrimary.swap(nwp);
    m_wrapAlt.swap(nwa);
    m_cols = cols;
    m_rows = rows;

    m_cy = Clamp(m_cy - (m_useAlt ? dropA : dropP) + (m_useAlt ? 0 : pullP), 0, m_rows - 1);
    m_cx = Clamp(m_cx, 0, m_cols - 1);
    // Kayitli imlecler de izgarayla birlikte kaysin (vim'den cikinca dogru satir).
    m_saved.y    = std::max(0, m_saved.y - dropP + pullP);
    m_savedAlt.y = std::max(0, m_savedAlt.y - dropA);
    m_pendingWrap = false;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_viewOffset = 0;
    ResetTabStops();
    Touch();
}

void Screen::ResetTabStops() {
    m_tabStops.assign((size_t)m_cols, false);
    for (int x = 8; x < m_cols; x += 8) m_tabStops[(size_t)x] = true;
}

void Screen::SetTabStop() {
    if (m_cx >= 0 && m_cx < m_cols) m_tabStops[(size_t)m_cx] = true;
}

void Screen::ClearTabStop(bool all) {
    if (all) m_tabStops.assign((size_t)m_cols, false);
    else if (m_cx >= 0 && m_cx < m_cols) m_tabStops[(size_t)m_cx] = false;
}

void Screen::ClearRow(int y) {
    Cell* r = row(y);
    for (int x = 0; x < m_cols; ++x) r[x].Clear(m_pen);
    wraps()[(size_t)y] = 0;
}

void Screen::CopyRow(int dst, int src) {
    std::copy_n(row(src), m_cols, row(dst));
    std::vector<uint8_t>& w = wraps();
    w[(size_t)dst] = w[(size_t)src];
}

void Screen::PushToScrollback(int y) {
    if (m_useAlt) return;
    PushHistory(row(y), m_cols, m_wrapPrimary[(size_t)y] != 0);
}

void Screen::PushHistory(const Cell* r, int cols, bool wrapped) {
    // Sarmalanan satirin sondaki boslugu metnin parcasi ("foo bar" tam kolonda
    // bolunduyse), kirpilirsa kopyalamada kelimeler yapisir.
    int len = cols;
    if (!wrapped) {
        while (len > 0 && r[len - 1].IsBlank()) --len;
    }

    SbRow sr;
    if ((int)m_scrollback.size() >= kScrollbackLimit) {
        sr = std::move(m_scrollback.front()); // kapasiteyi yeniden kullan
        m_scrollback.pop_front();
        ++m_trimmed;
    }
    sr.cells.assign(r, r + len);
    sr.wrapped = wrapped;
    m_scrollback.push_back(std::move(sr));

    // Kullanici yukari kaydirmissa baktigi icerik yerinde kalsin. Tampon
    // doluyken en eski satir atilinca da indeksler bir kayar; ofset yine artar.
    if (m_viewOffset > 0) {
        m_viewOffset = std::min(m_viewOffset + 1, (int)m_scrollback.size());
    }
}

// ---------------------------------------------------------------- yazim ----

void Screen::PutChar(char32_t cp) {
    m_shrinkDropped = 0;
    const int w = CharWidth(cp);
    if (w == 0) return; // birlesen isaretler M0'da atlaniyor (FR-TERM-005)

    if (m_pendingWrap) {
        m_pendingWrap = false;
        m_cx = 0;
        DoLineFeed(true);
    }

    if (m_cx + w > m_cols) {
        if (m_autoWrap) {
            m_cx = 0;
            DoLineFeed(true);
        } else {
            m_cx = std::max(0, m_cols - w);
        }
    }

    Cell* r = row(m_cy);

    // Genis bir karakterin yarisina yaziyorsak esi yetim kalmasin:
    // kuyruga yaziliyorsa soldaki bas, yazilan araligin hemen sagindaki hucre
    // kuyruksa (basi simdi eziliyor) o kuyruk temizlenir.
    if ((r[m_cx].flags & CF_WideTail) && m_cx > 0) r[m_cx - 1].Clear(m_pen);
    const int end = m_cx + w;
    if (end < m_cols && (r[end].flags & CF_WideTail)) r[end].Clear(m_pen);

    r[m_cx].ch     = cp;
    r[m_cx].fg     = m_pen.fg;
    r[m_cx].bg     = m_pen.bg;
    r[m_cx].flags  = m_pen.flags;
    r[m_cx].linkId = m_currentLinkId;

    if (w == 2 && m_cx + 1 < m_cols) {
        r[m_cx + 1].ch     = U' ';
        r[m_cx + 1].fg     = m_pen.fg;
        r[m_cx + 1].bg     = m_pen.bg;
        r[m_cx + 1].flags  = (uint16_t)(m_pen.flags | CF_WideTail);
        r[m_cx + 1].linkId = m_currentLinkId;
    }

    m_cx += w;
    if (m_cx >= m_cols) {
        m_cx = m_cols - 1;
        m_pendingWrap = m_autoWrap;
    }
    Touch();
}

void Screen::LineFeed() {
    DoLineFeed(false);
}

// Acik LF gercek satir sonu: satirin sarma bayragi silinir. Otomatik
// kaydirmadan gelen LF ise bayragi kurar (Windows Terminal ile ayni kural).
void Screen::DoLineFeed(bool wrapped) {
    m_shrinkDropped = 0;
    wraps()[(size_t)m_cy] = wrapped ? 1 : 0;
    m_pendingWrap = false;
    if (m_cy == m_scrollBottom)      ScrollUp(1);
    else if (m_cy < m_rows - 1)      ++m_cy;
    Touch();
}

void Screen::NextLine() {
    m_cx = 0;
    LineFeed();
}

void Screen::CarriageReturn() {
    m_cx = 0;
    m_pendingWrap = false;
    Touch();
}

void Screen::Backspace() {
    m_pendingWrap = false;
    if (m_cx > 0) --m_cx;
    Touch();
}

void Screen::Tab(int count) {
    for (int i = 0; i < count; ++i) {
        int x = m_cx + 1;
        while (x < m_cols - 1 && !m_tabStops[(size_t)x]) ++x;
        m_cx = std::min(x, m_cols - 1);
    }
    m_pendingWrap = false;
    Touch();
}

void Screen::BackTab(int count) {
    for (int i = 0; i < count; ++i) {
        int x = m_cx - 1;
        while (x > 0 && !m_tabStops[(size_t)x]) --x;
        m_cx = std::max(x, 0);
    }
    Touch();
}

void Screen::ReverseIndex() {
    m_pendingWrap = false;
    if (m_cy == m_scrollTop) ScrollDown(1);
    else if (m_cy > 0)       --m_cy;
    Touch();
}

// --------------------------------------------------------------- imlec -----

void Screen::SetCursor(int x, int y) {
    if (m_originMode) y += m_scrollTop;
    m_cx = Clamp(x, 0, m_cols - 1);
    m_cy = m_originMode ? Clamp(y, m_scrollTop, m_scrollBottom)
                        : Clamp(y, 0, m_rows - 1);
    m_pendingWrap = false;
    Touch();
}

void Screen::MoveCursor(int dx, int dy) {
    m_cx = Clamp(m_cx + dx, 0, m_cols - 1);
    // Dikey hareket kaydirma bolgesinin disina tasmaz.
    const int lo = (m_cy >= m_scrollTop) ? m_scrollTop : 0;
    const int hi = (m_cy <= m_scrollBottom) ? m_scrollBottom : m_rows - 1;
    m_cy = Clamp(m_cy + dy, lo, hi);
    m_pendingWrap = false;
    Touch();
}

void Screen::SetCursorCol(int x) { m_cx = Clamp(x, 0, m_cols - 1); m_pendingWrap = false; Touch(); }

void Screen::SetCursorRow(int y) {
    if (m_originMode) y += m_scrollTop;
    m_cy = Clamp(y, 0, m_rows - 1);
    m_pendingWrap = false;
    Touch();
}

void Screen::SaveCursor() {
    SavedCursor& s = m_useAlt ? m_savedAlt : m_saved;
    s.x = m_cx; s.y = m_cy; s.pen = m_pen; s.wrap = m_autoWrap;
}

void Screen::RestoreCursor() {
    const SavedCursor& s = m_useAlt ? m_savedAlt : m_saved;
    m_cx = Clamp(s.x, 0, m_cols - 1);
    m_cy = Clamp(s.y, 0, m_rows - 1);
    m_pen = s.pen;
    m_autoWrap = s.wrap;
    m_pendingWrap = false;
    Touch();
}

// --------------------------------------------------------------- silme -----

void Screen::EraseInDisplay(int mode) {
    m_shrinkDropped = 0;
    switch (mode) {
    case 0:
        EraseInLine(0);
        for (int y = m_cy + 1; y < m_rows; ++y) ClearRow(y);
        break;
    case 1:
        EraseInLine(1);
        for (int y = 0; y < m_cy; ++y) ClearRow(y);
        break;
    case 2: {
        // Satirlari silmeden once scrollback'e aktar ki kullanici yukari kaydirdiginda
        // banner ve onceki ciktilari gorebilsin (Windows Terminal ve xterm davranisi).
        if (!m_useAlt) {
            int lastNonBlank = -1;
            for (int y = m_rows - 1; y >= 0; --y) {
                const Cell* r = row(y);
                for (int x = 0; x < m_cols; ++x) {
                    if (!r[x].IsBlank()) { lastNonBlank = y; break; }
                }
                if (lastNonBlank >= 0) break;
            }
            for (int y = 0; y <= lastNonBlank; ++y) {
                PushToScrollback(y);
            }
        }
        for (int y = 0; y < m_rows; ++y) ClearRow(y);
        break;
    }
    case 3:
        // Mutlak satir numaralari kaymasin: silinenler de "kirpilmis" sayilir.
        m_trimmed += (int64_t)m_scrollback.size();
        m_scrollback.clear();
        m_viewOffset = 0;
        break;
    default: break;
    }
    Touch();
}

void Screen::EraseInLine(int mode) {
    Cell* r = row(m_cy);
    int from = 0, to = m_cols;
    if (mode == 0)      from = m_cx;
    else if (mode == 1) to = std::min(m_cx + 1, m_cols);
    else if (mode != 2) return;
    for (int x = from; x < to; ++x) r[x].Clear(m_pen);
    // Satirin sonu silindiyse artik alt satira tasmiyor.
    if (to == m_cols) wraps()[(size_t)m_cy] = 0;
    m_pendingWrap = false;
    Touch();
}

void Screen::EraseChars(int n) {
    if (n < 1) n = 1;
    Cell* r = row(m_cy);
    const int to = std::min(m_cx + n, m_cols);
    for (int x = m_cx; x < to; ++x) r[x].Clear(m_pen);
    Touch();
}

void Screen::InsertChars(int n) {
    if (n < 1) n = 1;
    n = std::min(n, m_cols - m_cx);
    Cell* r = row(m_cy);
    for (int x = m_cols - 1; x >= m_cx + n; --x) r[x] = r[x - n];
    for (int x = m_cx; x < m_cx + n; ++x) r[x].Clear(m_pen);
    Touch();
}

void Screen::DeleteChars(int n) {
    if (n < 1) n = 1;
    n = std::min(n, m_cols - m_cx);
    Cell* r = row(m_cy);
    for (int x = m_cx; x < m_cols - n; ++x) r[x] = r[x + n];
    for (int x = m_cols - n; x < m_cols; ++x) r[x].Clear(m_pen);
    Touch();
}

void Screen::InsertLines(int n) {
    if (m_cy < m_scrollTop || m_cy > m_scrollBottom) return;
    if (n < 1) n = 1;
    n = std::min(n, m_scrollBottom - m_cy + 1);
    for (int y = m_scrollBottom; y >= m_cy + n; --y) CopyRow(y, y - n);
    for (int y = m_cy; y < m_cy + n; ++y) ClearRow(y);
    Touch();
}

void Screen::DeleteLines(int n) {
    if (m_cy < m_scrollTop || m_cy > m_scrollBottom) return;
    if (n < 1) n = 1;
    n = std::min(n, m_scrollBottom - m_cy + 1);
    for (int y = m_cy; y + n <= m_scrollBottom; ++y) CopyRow(y, y + n);
    for (int y = m_scrollBottom - n + 1; y <= m_scrollBottom; ++y) ClearRow(y);
    Touch();
}

// ------------------------------------------------------------ kaydirma -----

void Screen::SetScrollRegion(int top, int bottom) {
    if (top < 0) top = 0;
    if (bottom < 0 || bottom >= m_rows) bottom = m_rows - 1;
    if (top >= bottom) { top = 0; bottom = m_rows - 1; }
    m_scrollTop = top;
    m_scrollBottom = bottom;
    SetCursor(0, 0);
}

void Screen::ScrollUp(int n) {
    if (n < 1) return;
    const int top = m_scrollTop, bot = m_scrollBottom;
    const int span = bot - top + 1;
    n = std::min(n, span);

    const bool toHistory = !m_useAlt && top == 0 && bot == m_rows - 1;
    if (toHistory) {
        for (int i = 0; i < n; ++i) PushToScrollback(top + i);
    }
    for (int y = top; y + n <= bot; ++y) CopyRow(y, y + n);
    for (int y = bot - n + 1; y <= bot; ++y) ClearRow(y);
    Touch();
}

void Screen::ScrollDown(int n) {
    if (n < 1) return;
    const int top = m_scrollTop, bot = m_scrollBottom;
    n = std::min(n, bot - top + 1);
    for (int y = bot; y - n >= top; --y) CopyRow(y, y - n);
    for (int y = top; y < top + n; ++y) ClearRow(y);
    Touch();
}

// --------------------------------------------------------------- modlar ----

void Screen::UseAltBuffer(bool alt) {
    if (alt == m_useAlt) return;
    m_useAlt = alt;
    if (alt) {
        Pen saved = m_pen;
        m_pen.Reset();
        for (int y = 0; y < m_rows; ++y) ClearRow(y);
        m_pen = saved;
        m_viewOffset = 0;
    }
    m_pendingWrap = false;
    Touch();
}

void Screen::FillScreen(char32_t cp) {
    for (int y = 0; y < m_rows; ++y) {
        Cell* r = row(y);
        for (int x = 0; x < m_cols; ++x) {
            r[x].Clear(m_pen);
            r[x].ch = cp;
        }
        wraps()[(size_t)y] = 0;
    }
    Touch();
}

void Screen::Reset() {
    m_pen.Reset();
    m_useAlt = false;
    m_autoWrap = true;
    m_originMode = false;
    m_cursorVisible = true;
    m_bracketedPaste = false;
    m_appCursorKeys = false;
    m_appKeypad = false;
    m_mouseMode = 0;
    m_mouseSgr = false;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_cx = m_cy = 0;
    m_pendingWrap = false;
    m_viewOffset = 0;
    for (int y = 0; y < m_rows; ++y) ClearRow(y);
    { bool a = m_useAlt; m_useAlt = true; for (int y = 0; y < m_rows; ++y) ClearRow(y); m_useAlt = a; }
    ResetTabStops();
    m_currentLinkId = 0;
    m_hyperlinks.clear();
    m_lastExitCode = 0;
    m_lastCommandFailed = false;
    m_inCommandExecution = false;
    m_commandStartRow = 0;
    m_commandEndRow = 0;
    m_commandMarks.clear();
    Touch();
}

// ------------------------------------------------------------- gorunum -----

void Screen::ScrollViewLines(int delta) {
    // Alternatif tamponun scrollback'i yok (xterm): birincil ekranin eski
    // satirlari tam ekran uygulamanin ustune karismasin.
    if (m_useAlt) { if (m_viewOffset != 0) { m_viewOffset = 0; Touch(); } return; }
    const int maxOff = (int)m_scrollback.size();
    m_viewOffset = Clamp(m_viewOffset + delta, 0, maxOff);
    Touch();
}

void Screen::ScrollViewToBottom() {
    if (m_viewOffset != 0) { m_viewOffset = 0; Touch(); }
}

void Screen::CopyViewRow(int y, Cell* dst) const {
    Cell blank;
    blank.ch = U' ';
    blank.fg = 0; blank.bg = 0;
    blank.flags = CF_DefaultColors;

    const int sb = (int)m_scrollback.size();
    const int top = sb - m_viewOffset;   // gorunumun ilk mutlak satiri
    const int abs = top + y;

    if (abs < 0 || abs >= sb + m_rows) {
        for (int x = 0; x < m_cols; ++x) dst[x] = blank;
        return;
    }
    if (abs < sb) {
        const std::vector<Cell>& r = m_scrollback[(size_t)abs].cells;
        const int n = std::min((int)r.size(), m_cols);
        for (int x = 0; x < n; ++x) dst[x] = r[(size_t)x];
        for (int x = n; x < m_cols; ++x) dst[x] = blank;
    } else {
        const Cell* r = row(abs - sb);
        std::copy_n(r, m_cols, dst);
    }
}

bool Screen::CursorInView(int& outX, int& outY) const {
    const int viewY = m_cy + m_viewOffset;
    if (viewY < 0 || viewY >= m_rows) return false;
    outX = m_cx;
    outY = viewY;
    return true;
}

int64_t Screen::ViewRowToAbs(int viewRow) const {
    return m_trimmed + (int64_t)m_scrollback.size() - m_viewOffset + viewRow;
}

int Screen::AbsToViewRow(int64_t absRow) const {
    const int64_t v = absRow - ViewRowToAbs(0);
    constexpr int64_t kLim = 1 << 30; // int'e sigsin; gorunum disi zaten
    return (int)(v < -kLim ? -kLim : (v > kLim ? kLim : v));
}

const Cell* Screen::AbsRow(int64_t abs, int& len, bool& wrapped) const {
    const int64_t sb = (int64_t)m_scrollback.size();
    const int64_t i = abs - m_trimmed;
    len = 0;
    wrapped = false;
    if (i < 0 || i >= sb + m_rows) return nullptr;
    if (i < sb) {
        const SbRow& r = m_scrollback[(size_t)i];
        len = (int)r.cells.size();
        wrapped = r.wrapped;
        return r.cells.data();
    }
    const int y = (int)(i - sb);
    len = m_cols;
    wrapped = wraps()[(size_t)y] != 0;
    return row(y);
}

std::string Screen::TextInRange(int x0, int y0, int x1, int y1) const {
    if (y1 < y0 || (y1 == y0 && x1 < x0)) { std::swap(x0, x1); std::swap(y0, y1); }
    y0 = Clamp(y0, 0, m_rows - 1);
    y1 = Clamp(y1, 0, m_rows - 1);
    return TextInAbsRange(x0, ViewRowToAbs(y0), x1, ViewRowToAbs(y1));
}

std::string Screen::TextInAbsRange(int x0, int64_t y0, int x1, int64_t y1) const {
    if (y1 < y0 || (y1 == y0 && x1 < x0)) { std::swap(x0, x1); std::swap(y0, y1); }

    // Kirpilip atilmis ya da henuz olmayan satirlar secimden duser.
    const int64_t first = m_trimmed;
    const int64_t last  = m_trimmed + (int64_t)m_scrollback.size() + m_rows - 1;
    if (y1 < first || y0 > last) return {};
    if (y0 < first) { y0 = first; x0 = 0; }
    if (y1 > last)  { y1 = last;  x1 = kMaxCols; }

    std::string out;
    for (int64_t y = y0; y <= y1; ++y) {
        int len = 0;
        bool wrapped = false;
        const Cell* r = AbsRow(y, len, wrapped);

        int from = (y == y0) ? Clamp(x0, 0, len) : 0;
        const int to = (y == y1) ? Clamp(x1 + 1, 0, len) : len;
        // Secim genis karakterin kuyrugundan basliyorsa karakterin kendisini al.
        if (from > 0 && from < len && (r[from].flags & CF_WideTail)) --from;

        // Sarmalanan satir alttakiyle ayni mantiksal satir: sondaki bosluk
        // metnin parcasi, satir sonu da eklenmez (yapistirinca komut bolunmesin).
        const bool join = wrapped && y != y1;
        int end = to;
        if (!join) {
            while (end > from && (r[end - 1].ch == U' ' || r[end - 1].ch == 0)) --end;
        }
        for (int x = from; x < end; ++x) {
            if (r[x].flags & CF_WideTail) continue;
            const char32_t c = r[x].ch;
            AppendUtf8(out, c ? c : U' ');
        }
        if (y != y1 && !join) out += "\r\n";
    }
    return out;
}

std::string Screen::CursorLineText() const {
    const int64_t gridTop = m_trimmed + (int64_t)m_scrollback.size();
    const int64_t cur = gridTop + m_cy;
    int64_t start = cur;
    // Uzun bir istem ust satirlardan sarmalanarak gelmis olabilir. Alt ekranda
    // scrollback birincil tampona ait, oraya tasma.
    const int64_t floor = m_useAlt ? gridTop : m_trimmed;
    for (int n = 0; n < 16 && start > floor; ++n) {
        int len = 0;
        bool wrapped = false;
        AbsRow(start - 1, len, wrapped);
        if (!wrapped) break;
        --start;
    }
    return TextInAbsRange(0, start, m_cols - 1, cur);
}

std::vector<std::string> Screen::GetTailLines(int maxLines) const {
    std::vector<std::string> lines;
    if (m_rows <= 0 || m_cols <= 0) return lines;
    int count = std::min(maxLines, m_rows);
    int startY = m_rows - count;
    for (int y = startY; y < m_rows; ++y) {
        std::string line = TextInRange(0, y, m_cols - 1, y);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\n' || line.back() == '\t')) {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

void Screen::Clear() {
    m_pen.Reset();
    EraseInDisplay(2);
    EraseInDisplay(3);
    SetCursor(0, 0);
    m_cursorVisible = true;
    Touch();
}

// ----------------------------------------------------------- OSC 8 & 133 ----

void Screen::SetCurrentHyperlink(const std::string& url) {
    if (url.empty()) {
        m_currentLinkId = 0;
        return;
    }
    if (m_currentLinkId > 0 && m_currentLinkId <= m_hyperlinks.size() &&
        m_hyperlinks[m_currentLinkId - 1] == url) {
        return;
    }
    for (size_t i = 0; i < m_hyperlinks.size(); ++i) {
        if (m_hyperlinks[i] == url) {
            m_currentLinkId = static_cast<uint16_t>(i + 1);
            return;
        }
    }
    if (m_hyperlinks.size() < 65534) {
        m_hyperlinks.push_back(url);
        m_currentLinkId = static_cast<uint16_t>(m_hyperlinks.size());
    } else {
        m_currentLinkId = 0;
    }
}

std::string Screen::GetHyperlink(uint16_t id) const {
    if (id == 0 || id > m_hyperlinks.size()) return {};
    return m_hyperlinks[id - 1];
}

uint16_t Screen::GetLinkIdAt(int col, int viewRow) const {
    const int sb = static_cast<int>(m_scrollback.size());
    const int top = sb - m_viewOffset;
    const int abs = top + viewRow;
    if (abs < 0 || abs >= sb + m_rows) return 0;
    if (abs < sb) {
        const auto& r = m_scrollback[static_cast<size_t>(abs)].cells;
        if (col >= 0 && col < static_cast<int>(r.size())) {
            return r[static_cast<size_t>(col)].linkId;
        }
    } else {
        const int ry = abs - sb;
        if (ry >= 0 && ry < m_rows && col >= 0 && col < m_cols) {
            return row(ry)[col].linkId;
        }
    }
    return 0;
}

std::string Screen::GetLinkAt(int col, int viewRow) const {
    const uint16_t id = GetLinkIdAt(col, viewRow);
    if (id != 0) {
        return GetHyperlink(id);
    }

    const int sb = static_cast<int>(m_scrollback.size());
    const int top = sb - m_viewOffset;
    const int abs = top + viewRow;
    if (abs < 0 || abs >= sb + m_rows) return {};

    std::string line = TextInRange(0, viewRow, m_cols - 1, viewRow);
    if (line.empty()) return {};

    static const std::string schemes[] = { "https://", "http://", "file://" };
    for (const auto& s : schemes) {
        size_t pos = 0;
        while ((pos = line.find(s, pos)) != std::string::npos) {
            size_t end = pos + s.size();
            while (end < line.size()) {
                char ch = line[end];
                if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
                    ch == '"' || ch == '\'' || ch == '`' || ch == '<' || ch == '>' ||
                    ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}') {
                    break;
                }
                ++end;
            }
            while (end > pos + s.size() && (line[end - 1] == '.' || line[end - 1] == ',' ||
                                            line[end - 1] == ';' || line[end - 1] == ':')) {
                --end;
            }

            if (col >= static_cast<int>(pos) && col < static_cast<int>(end)) {
                return line.substr(pos, end - pos);
            }
            pos = end;
        }
    }
    return {};
}

void Screen::HandleOsc133(const std::string& body) {
    if (body.empty()) return;
    const char type = body[0];
    switch (type) {
    case 'A': // Prompt baslangici
        m_inCommandExecution = false;
        AddCommandMark(ViewRowToAbs(m_cy));
        Touch();
        break;
    case 'B': // Komut girisi baslangici
        m_inCommandExecution = false;
        break;
    case 'C': // Komut calismaya basladi (cikti basliyor)
        m_inCommandExecution = true;
        m_lastCommandFailed = false;
        m_commandStartRow = ViewRowToAbs(m_cy);
        Touch();
        break;
    case 'D': { // Komut tamamlandi (D veya D;exit_code)
        m_inCommandExecution = false;
        int exitCode = 0;
        size_t semi = body.find(';');
        if (semi != std::string::npos && semi + 1 < body.size()) {
            exitCode = std::atoi(body.c_str() + semi + 1);
        }
        m_lastExitCode = exitCode;
        m_lastCommandFailed = (exitCode != 0);
        m_commandEndRow = ViewRowToAbs(m_cy);
        Touch();
        break;
    }
    case 'P': { // Ozellik (ornegin P;Cwd=...)
        if (body.rfind("P;Cwd=", 0) == 0 && body.size() > 6) {
            cwd = Utf8ToWide(body.substr(6));
        }
        break;
    }
    default:
        break;
    }
}

std::string Screen::GetLastFailedCommandOutput() const {
    if (m_commandEndRow >= m_commandStartRow && m_commandStartRow > 0) {
        std::string out = TextInAbsRange(0, m_commandStartRow, m_cols - 1, m_commandEndRow);
        while (!out.empty() && (out.front() == '\r' || out.front() == '\n' || out.front() == ' ')) {
            out.erase(out.begin());
        }
        while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' ')) {
            out.pop_back();
        }
        if (!out.empty()) return out;
    }

    auto tail = GetTailLines(10);
    std::string res;
    for (const auto& line : tail) {
        if (!line.empty()) {
            if (!res.empty()) res += "\n";
            res += line;
        }
    }
    return res;
}

void Screen::AddCommandMark(int64_t absRow) {
    if (m_commandMarks.empty() || m_commandMarks.back() != absRow) {
        m_commandMarks.push_back(absRow);
        if (m_commandMarks.size() > 1000) {
            m_commandMarks.erase(m_commandMarks.begin(), m_commandMarks.begin() + 100);
        }
    }
}

bool Screen::JumpToPreviousCommand() {
    if (m_commandMarks.empty()) return false;
    const int sb = static_cast<int>(m_scrollback.size());
    const int currentVisibleTop = sb - m_viewOffset;

    int64_t target = -1;
    for (auto it = m_commandMarks.rbegin(); it != m_commandMarks.rend(); ++it) {
        if (*it < currentVisibleTop) {
            target = *it;
            break;
        }
    }

    if (target < 0) {
        target = m_commandMarks.front();
    }

    m_viewOffset = std::max(0, std::min(sb, sb - static_cast<int>(target)));
    Touch();
    return true;
}

bool Screen::JumpToNextCommand() {
    if (m_commandMarks.empty()) return false;
    const int sb = static_cast<int>(m_scrollback.size());
    const int currentVisibleTop = sb - m_viewOffset;

    for (int64_t mark : m_commandMarks) {
        if (mark > currentVisibleTop) {
            m_viewOffset = std::max(0, std::min(sb, sb - static_cast<int>(mark)));
            Touch();
            return true;
        }
    }

    ScrollViewToBottom();
    return true;
}

} // namespace ft
