#include "ui/Ui.h"
#include "ui/Theme.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace ft {
namespace {

constexpr size_t kMaxPaste = 4096;   // tek satirlik alana yapistirma tavani

bool IsLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

bool IsDigitW(wchar_t c) { return c >= L'0' && c <= L'9'; }

int HexVal(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

// Yalniz rakamlardan olusan, tasmayacak uzunlukta metni cozer.
bool ParseDigits(const std::wstring& s, long long& out) {
    if (s.empty() || s.size() > 9) return false;
    long long v = 0;
    for (wchar_t c : s) {
        if (!IsDigitW(c)) return false;
        v = v * 10 + (c - L'0');
    }
    out = v;
    return true;
}

// "#RRGGBB" veya "RRGGBB"; tam alti hane sart, wcstoul tasmasi olmaz.
bool ParseHex6(const std::wstring& s, uint32_t& out) {
    const size_t i0 = (!s.empty() && s[0] == L'#') ? 1 : 0;
    if (s.size() - i0 != 6) return false;
    uint32_t v = 0;
    for (size_t i = i0; i < s.size(); ++i) {
        const int d = HexVal(s[i]);
        if (d < 0) return false;
        v = (v << 4) | (uint32_t)d;
    }
    out = v & 0xFFFFFFu;
    return true;
}

std::wstring FormatHex6(uint32_t v) {
    wchar_t b[16];
    swprintf_s(b, L"#%06X", v & 0xFFFFFFu);
    return b;
}

} // namespace

// ------------------------------------------------------------------ pano ---

std::wstring ClipboardGetText(HWND owner) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return {};
    // Panoyu baska bir surec tutuyor olabilir; birkac kez dene.
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(owner)) {
            std::wstring out;
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
                    out = p;
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
            return out;
        }
        Sleep(12);
    }
    return {};
}

bool ClipboardSetText(HWND owner, const std::wstring& text) {
    if (text.empty()) return false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(owner)) {
            EmptyClipboard();
            bool ok = false;
            const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                if (void* p = GlobalLock(g)) {
                    memcpy(p, text.c_str(), bytes);
                    GlobalUnlock(g);
                    ok = (SetClipboardData(CF_UNICODETEXT, g) != nullptr);
                    if (!ok) GlobalFree(g);
                } else {
                    GlobalFree(g);
                }
            }
            CloseClipboard();
            return ok;
        }
        Sleep(12);
    }
    return false;
}

// ---------------------------------------------------------------- cerceve ---

void Ui::Begin(UiInput& in, float scale) {
    m_in = &in;
    m_s = scale;
    if (!in.down) m_dragField = 0;   // fare birakildiysa surukleme biter
}

void Ui::End() {
    if (m_in) {
        m_in->chars.clear();
        m_in->keys.clear();
        if (m_in->clicked) m_in->px = m_in->py = -1.0f;   // basis bu tikla tuketildi
        m_in->clicked = false;
        m_in->dblClick = false;
        m_in->wheel = 0;
    }
    m_in = nullptr;
}

void Ui::SetFocus(int id) {
    m_focus = id;
    m_caret = -1;
    m_anchor = -1;
    m_fieldOffset = 0.0f;
    m_dragField = 0;
}

bool Ui::Hot(const D2D1_RECT_F& r) const {
    if (!m_in) return false;
    return m_in->mx >= r.left && m_in->mx < r.right &&
           m_in->my >= r.top && m_in->my < r.bottom;
}

bool Ui::PressIn(const D2D1_RECT_F& r) const {
    if (!m_in) return false;
    return m_in->px >= r.left && m_in->px < r.right &&
           m_in->py >= r.top && m_in->py < r.bottom;
}

bool Ui::Clicked(const D2D1_RECT_F& r) const {
    return m_in && (m_in->clicked || m_in->dblClick) && Hot(r) && PressIn(r);
}

bool Ui::DblClicked(const D2D1_RECT_F& r) const {
    return m_in && m_in->dblClick && Hot(r) && PressIn(r);
}

// ---------------------------------------------------------------- buton ----

bool Ui::Button(int id, const D2D1_RECT_F& r, const std::wstring& label,
                bool primary, bool danger, bool enabled) {
    const bool hot = enabled && Hot(r);
    const bool pressed = hot && m_in && m_in->down && PressIn(r);
    const float rad = std::floor(4 * m_s);

    uint32_t bg, fg, border = 0;
    if (!enabled) {
        bg = theme::Surface; fg = theme::TextDim; border = theme::Border;
    } else if (danger) {
        bg = pressed ? theme::Red : (hot ? theme::Red : theme::Surface);
        fg = hot ? 0xFFFFFF : theme::Red;
        border = theme::Red;
    } else if (primary) {
        bg = pressed ? theme::AcDim() : (hot ? theme::AcHi() : theme::Ac());
        fg = 0x0A0D11;
    } else {
        bg = pressed ? theme::Base : (hot ? theme::Elevated : theme::Surface);
        fg = theme::Text;
        border = hot ? theme::BorderHi : theme::Border;
    }

    m_r.FillRound(r, rad, bg, (danger && !hot) ? 0.0f : 1.0f);
    if (border) m_r.Stroke(r, border, std::max(1.0f, std::floor(1 * m_s)));
    m_r.Text(label, r, fg, 12.5f * m_s, Renderer::Align::Center, primary);

    if (hot && m_in && m_in->clicked && PressIn(r)) {
        SetFocus(0);
        return true;
    }
    (void)id;
    return false;
}

// ---------------------------------------------------------- liste satiri ----

bool Ui::Row(int id, const D2D1_RECT_F& r, bool selected, uint32_t stripe) {
    const bool hot = Hot(r);
    if (selected)  m_r.Fill(r, theme::Elevated);
    else if (hot)  m_r.Fill(r, theme::Elevated, 0.45f);

    if (stripe) {
        m_r.Fill(D2D1::RectF(r.left, r.top, r.left + std::floor(3 * m_s), r.bottom), stripe);
    }
    if (selected) {
        m_r.Fill(D2D1::RectF(r.right - std::floor(2 * m_s), r.top, r.right, r.bottom), theme::Ac());
    }
    (void)id;
    return hot && m_in && m_in->clicked && PressIn(r);
}

// --------------------------------------------------------- metin girdisi ----

int Ui::CaretFromX(const std::wstring& shown, float px, float relX) {
    if (shown.empty() || relX <= 0.0f) return 0;
    if (m_r.MeasureText(shown, px) <= relX) return (int)shown.size();

    size_t lo = 0, hi = shown.size();
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        if (m_r.MeasureText(shown.substr(0, mid), px) <= relX) lo = mid;
        else hi = mid - 1;
    }
    if (lo < shown.size()) {
        const float a = m_r.MeasureText(shown.substr(0, lo), px);
        const float b = m_r.MeasureText(shown.substr(0, lo + 1), px);
        if (relX > (a + b) * 0.5f) ++lo;
    }
    if (lo < shown.size() && IsLowSurrogate(shown[lo])) ++lo;  // vekil ciftin ortasi
    return (int)lo;
}

void Ui::EditField(std::wstring& text, bool password) {
    if (!m_in) return;

    auto clampAll = [&] {
        const int n = (int)text.size();
        m_caret = std::clamp(m_caret < 0 ? n : m_caret, 0, n);
        m_anchor = std::clamp(m_anchor < 0 ? m_caret : m_anchor, 0, n);
    };
    clampAll();

    auto hasSel = [&] { return m_caret != m_anchor; };
    auto selLo  = [&] { return std::min(m_caret, m_anchor); };
    auto selHi  = [&] { return std::max(m_caret, m_anchor); };

    auto eraseSel = [&] {
        if (!hasSel()) return false;
        const int a = selLo(), b = selHi();
        text.erase((size_t)a, (size_t)(b - a));
        m_caret = m_anchor = a;
        return true;
    };
    // Secim yoksa alanin tamami kopyalanir: bu alanlar tek degerlik, kullanici
    // once "tumunu sec" yapmak zorunda kalmasin.
    auto copyOut = [&] {
        if (password) return;
        ClipboardSetText(m_owner,
            hasSel() ? text.substr((size_t)selLo(), (size_t)(selHi() - selLo())) : text);
    };
    auto pasteIn = [&] {
        std::wstring cb = ClipboardGetText(m_owner);
        if (cb.empty()) return;
        if (cb.size() > kMaxPaste) cb.resize(kMaxPaste);
        for (wchar_t& c : cb) {
            if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
        }
        eraseSel();
        text.insert((size_t)m_caret, cb);
        m_caret += (int)cb.size();
        m_anchor = m_caret;
    };
    auto wordLeft = [&](int i) {
        while (i > 0 && iswspace(text[(size_t)i - 1])) --i;
        while (i > 0 && !iswspace(text[(size_t)i - 1])) --i;
        return i;
    };
    auto wordRight = [&](int i) {
        const int n = (int)text.size();
        while (i < n && !iswspace(text[(size_t)i])) ++i;
        while (i < n && iswspace(text[(size_t)i])) ++i;
        return i;
    };

    for (const UiKey& k : m_in->keys) {
        switch (k.vk) {
        case VK_LEFT:
            if (k.ctrl)                    m_caret = wordLeft(m_caret);
            else if (hasSel() && !k.shift) m_caret = selLo();
            else                           m_caret = std::max(0, m_caret - 1);
            if (!k.shift) m_anchor = m_caret;
            break;

        case VK_RIGHT:
            if (k.ctrl)                    m_caret = wordRight(m_caret);
            else if (hasSel() && !k.shift) m_caret = selHi();
            else                           m_caret = std::min((int)text.size(), m_caret + 1);
            if (!k.shift) m_anchor = m_caret;
            break;

        case VK_HOME:
            m_caret = 0;
            if (!k.shift) m_anchor = 0;
            break;

        case VK_END:
            m_caret = (int)text.size();
            if (!k.shift) m_anchor = m_caret;
            break;

        case VK_BACK:
            if (!eraseSel() && m_caret > 0) {
                const int from = k.ctrl ? wordLeft(m_caret) : m_caret - 1;
                text.erase((size_t)from, (size_t)(m_caret - from));
                m_caret = m_anchor = from;
            }
            break;

        case VK_DELETE:
            if (k.shift) {                       // Shift+Del = kes
                copyOut();
                if (!eraseSel()) { text.clear(); m_caret = m_anchor = 0; }
            } else if (!eraseSel() && m_caret < (int)text.size()) {
                const int to = k.ctrl ? wordRight(m_caret) : m_caret + 1;
                text.erase((size_t)m_caret, (size_t)(to - m_caret));
                m_anchor = m_caret;
            }
            break;

        case VK_INSERT:
            if (k.ctrl)       copyOut();
            else if (k.shift) pasteIn();
            break;

        case 'A': if (k.ctrl) { m_anchor = 0; m_caret = (int)text.size(); } break;
        case 'C': if (k.ctrl) copyOut(); break;
        case 'V': if (k.ctrl) pasteIn(); break;
        case 'X':
            if (k.ctrl) {
                copyOut();
                if (!eraseSel()) { text.clear(); m_caret = m_anchor = 0; }
            }
            break;
        default: break;
        }
        clampAll();
    }

    for (wchar_t c : m_in->chars) {
        if (c < 0x20 || c == 0x7F) continue;   // kontrol karakterleri
        eraseSel();
        text.insert((size_t)m_caret, 1, c);
        ++m_caret;
        m_anchor = m_caret;
    }

    m_in->chars.clear();
    m_in->keys.clear();
}

void Ui::EditTextArea(std::wstring& text) {
    if (!m_in) return;

    auto clampAll = [&] {
        const int n = (int)text.size();
        m_caret = std::clamp(m_caret < 0 ? n : m_caret, 0, n);
        m_anchor = std::clamp(m_anchor < 0 ? m_caret : m_anchor, 0, n);
    };
    clampAll();

    auto hasSel = [&] { return m_caret != m_anchor; };
    auto selLo  = [&] { return std::min(m_caret, m_anchor); };
    auto selHi  = [&] { return std::max(m_caret, m_anchor); };

    auto eraseSel = [&] {
        if (!hasSel()) return false;
        const int a = selLo(), b = selHi();
        text.erase((size_t)a, (size_t)(b - a));
        m_caret = m_anchor = a;
        return true;
    };

    auto copyOut = [&] {
        ClipboardSetText(m_owner,
            hasSel() ? text.substr((size_t)selLo(), (size_t)(selHi() - selLo())) : text);
    };

    auto pasteIn = [&] {
        std::wstring cb = ClipboardGetText(m_owner);
        if (cb.empty()) return;
        constexpr size_t kMaxAreaPaste = 65536;
        if (cb.size() > kMaxAreaPaste) cb.resize(kMaxAreaPaste);
        std::wstring norm;
        norm.reserve(cb.size());
        for (wchar_t c : cb) {
            if (c == L'\r') continue;
            norm.push_back(c);
        }
        eraseSel();
        text.insert((size_t)m_caret, norm);
        m_caret += (int)norm.size();
        m_anchor = m_caret;
    };

    std::vector<size_t> lineStarts;
    lineStarts.push_back(0);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n') lineStarts.push_back(i + 1);
    }

    auto posToLineCol = [&](int pos, int& outLine, int& outCol) {
        outLine = 0;
        outCol = 0;
        for (int i = 0; i < (int)lineStarts.size(); ++i) {
            size_t nextStart = (i + 1 < (int)lineStarts.size()) ? lineStarts[i + 1] : (text.size() + 1);
            if ((size_t)pos < nextStart || i == (int)lineStarts.size() - 1) {
                outLine = i;
                outCol = (int)((size_t)pos - lineStarts[i]);
                return;
            }
        }
    };

    auto lineColToPos = [&](int l, int c) -> int {
        l = std::clamp(l, 0, (int)lineStarts.size() - 1);
        size_t nextStart = (l + 1 < (int)lineStarts.size()) ? lineStarts[l + 1] : (text.size() + 1);
        size_t lineLen = (nextStart > lineStarts[l]) ? (nextStart - lineStarts[l] - (l + 1 < (int)lineStarts.size() ? 1 : 0)) : 0;
        if (lineLen > 0 && lineStarts[l] + lineLen <= text.size() && text[lineStarts[l] + lineLen - 1] == L'\r') --lineLen;
        c = std::clamp(c, 0, (int)lineLen);
        return (int)(lineStarts[l] + c);
    };

    for (const UiKey& k : m_in->keys) {
        int curL = 0, curC = 0;
        posToLineCol(m_caret, curL, curC);

        switch (k.vk) {
        case VK_LEFT:
            if (hasSel() && !k.shift) m_caret = selLo();
            else                      m_caret = std::max(0, m_caret - 1);
            if (!k.shift) m_anchor = m_caret;
            break;
        case VK_RIGHT:
            if (hasSel() && !k.shift) m_caret = selHi();
            else                      m_caret = std::min((int)text.size(), m_caret + 1);
            if (!k.shift) m_anchor = m_caret;
            break;
        case VK_UP:
            if (curL > 0) m_caret = lineColToPos(curL - 1, curC);
            else          m_caret = 0;
            if (!k.shift) m_anchor = m_caret;
            break;
        case VK_DOWN:
            if (curL + 1 < (int)lineStarts.size()) m_caret = lineColToPos(curL + 1, curC);
            else                                   m_caret = (int)text.size();
            if (!k.shift) m_anchor = m_caret;
            break;
        case VK_HOME:
            if (k.ctrl) m_caret = 0;
            else        m_caret = (int)lineStarts[curL];
            if (!k.shift) m_anchor = m_caret;
            break;
        case VK_END:
            if (k.ctrl) m_caret = (int)text.size();
            else {
                size_t nextStart = (curL + 1 < (int)lineStarts.size()) ? lineStarts[curL + 1] : (text.size() + 1);
                size_t lineLen = (nextStart > lineStarts[curL]) ? (nextStart - lineStarts[curL] - (curL + 1 < (int)lineStarts.size() ? 1 : 0)) : 0;
                m_caret = (int)(lineStarts[curL] + lineLen);
            }
            if (!k.shift) m_anchor = m_caret;
            break;
        case VK_RETURN:
            eraseSel();
            text.insert((size_t)m_caret, 1, L'\n');
            ++m_caret;
            m_anchor = m_caret;
            break;
        case VK_BACK:
            if (!eraseSel() && m_caret > 0) {
                text.erase((size_t)m_caret - 1, 1);
                --m_caret;
                m_anchor = m_caret;
            }
            break;
        case VK_DELETE:
            if (!eraseSel() && m_caret < (int)text.size()) {
                text.erase((size_t)m_caret, 1);
            }
            break;
        case 'A':
            if (k.ctrl) {
                m_anchor = 0;
                m_caret = (int)text.size();
            }
            break;
        case 'C':
            if (k.ctrl) copyOut();
            break;
        case 'V':
            if (k.ctrl) pasteIn();
            break;
        case 'X':
            if (k.ctrl) {
                copyOut();
                if (!eraseSel()) { text.clear(); m_caret = m_anchor = 0; }
            }
            break;
        default: break;
        }
        clampAll();
    }

    for (wchar_t c : m_in->chars) {
        if (c == 0x7F) continue;
        if (c < 0x20 && c != L'\n' && c != L'\t') continue;
        eraseSel();
        if (c == L'\t') {
            text.insert((size_t)m_caret, L"  ");
            m_caret += 2;
        } else if (c == L'\r' || c == L'\n') {
            text.insert((size_t)m_caret, 1, L'\n');
            ++m_caret;
        } else {
            text.insert((size_t)m_caret, 1, c);
            ++m_caret;
        }
        m_anchor = m_caret;
    }

    m_in->chars.clear();
    m_in->keys.clear();
}

bool Ui::Field(int id, const D2D1_RECT_F& r, std::wstring& text,
               const std::wstring& placeholder, bool password, float leftPad) {
    const bool hot = Hot(r);
    const float pad = std::floor(9 * m_s) + leftPad;
    const float size = 12.5f * m_s;
    const D2D1_RECT_F inner = D2D1::RectF(r.left + pad, r.top, r.right - pad, r.bottom);

    std::wstring shown = password ? std::wstring(text.size(), L'*') : text;
    const std::wstring before = text;

    // ---- fare: odak, imlec konumu, surukleyerek secim ----------------------
    if (m_in) {
        const float offsetNow = (m_focus == id) ? m_fieldOffset : 0.0f;
        const float relX = m_in->mx - inner.left + offsetNow;

        if (hot && m_in->down && m_dragField != id && PressIn(r)) {
            m_focus = id;                                   // basar basmaz odak
            m_dragField = id;
            m_caret = m_anchor = CaretFromX(shown, size, relX);
        } else if (m_dragField == id && m_in->down) {
            m_caret = CaretFromX(shown, size, relX);        // capa kalsin, imlec kaysin
        }
        if (hot && m_in->dblClick && m_focus == id) {
            m_anchor = 0;
            m_caret = (int)text.size();
        }
    }

    const bool focused = (m_focus == id);
    if (focused) {
        EditField(text, password);
        shown = password ? std::wstring(text.size(), L'*') : text;
    }

    // ---- kutu --------------------------------------------------------------
    const float rad = std::floor(4 * m_s);
    m_r.FillRound(r, rad, theme::Sunken);
    m_r.Stroke(r, focused ? theme::Ac() : (hot ? theme::BorderHi : theme::Border),
               std::max(1.0f, std::floor((focused ? 1.6f : 1.0f) * m_s)));

    if (shown.empty() && !focused) {
        m_r.Text(placeholder, inner, theme::TextDim, size);
        return text != before;
    }

    // ---- imlec gorunur kalsin diye yatay kaydirma --------------------------
    float offset = 0.0f;
    if (focused) {
        const int caret = std::clamp(m_caret < 0 ? (int)shown.size() : m_caret,
                                     0, (int)shown.size());
        const float caretW = m_r.MeasureText(shown.substr(0, (size_t)caret), size);
        const float avail = std::max(1.0f, inner.right - inner.left);
        const float total = m_r.MeasureText(shown, size);
        offset = m_fieldOffset;
        if (caretW - offset > avail - std::floor(2 * m_s)) offset = caretW - avail + std::floor(2 * m_s);
        if (caretW - offset < 0.0f) offset = caretW;
        if (total - offset < avail) offset = std::max(0.0f, total - avail);
        if (total <= avail) offset = 0.0f;
        m_fieldOffset = offset;
    }

    m_r.PushClip(inner);

    // ---- secim vurgusu -----------------------------------------------------
    if (focused && m_caret != m_anchor) {
        const int lo = std::clamp(std::min(m_caret, m_anchor), 0, (int)shown.size());
        const int hi = std::clamp(std::max(m_caret, m_anchor), 0, (int)shown.size());
        const float a = m_r.MeasureText(shown.substr(0, (size_t)lo), size);
        const float b = m_r.MeasureText(shown.substr(0, (size_t)hi), size);
        m_r.Fill(D2D1::RectF(inner.left - offset + a, r.top + std::floor(5 * m_s),
                             inner.left - offset + b, r.bottom - std::floor(5 * m_s)),
                 theme::Ac(), 0.35f);
    }

    m_r.Text(shown, D2D1::RectF(inner.left - offset, inner.top, inner.right + 4000.0f, inner.bottom),
             theme::TextHi, size);

    if (focused && m_caretOn && m_caret == m_anchor) {
        const int caret = std::clamp(m_caret < 0 ? (int)shown.size() : m_caret,
                                     0, (int)shown.size());
        const float cx = inner.left - offset + m_r.MeasureText(shown.substr(0, (size_t)caret), size);
        m_r.Fill(D2D1::RectF(cx, r.top + std::floor(7 * m_s),
                             cx + std::max(1.0f, std::floor(1.5f * m_s)),
                             r.bottom - std::floor(7 * m_s)), theme::Ac());
    }

    m_r.PopClip();
    return text != before;
}

bool Ui::TextArea(int id, const D2D1_RECT_F& r, std::wstring& text,
                  const std::wstring& placeholder, bool mono) {
    const bool hot = Hot(r);
    const float pad = std::floor(8 * m_s);
    const float size = 11.5f * m_s;
    const float lineH = std::floor(17.0f * m_s);
    const D2D1_RECT_F inner = D2D1::RectF(r.left + pad, r.top + pad, r.right - pad, r.bottom - pad);
    const float innerH = std::max(1.0f, inner.bottom - inner.top);
    const std::wstring before = text;

    // Line spans olustur
    struct LineSpan { size_t start; size_t len; };
    std::vector<LineSpan> lines;
    size_t cur = 0;
    while (cur <= text.size()) {
        size_t nl = text.find(L'\n', cur);
        if (nl == std::wstring::npos) {
            size_t len = text.size() - cur;
            if (len > 0 && text[cur + len - 1] == L'\r') --len;
            lines.push_back({ cur, len });
            break;
        } else {
            size_t len = nl - cur;
            if (len > 0 && text[cur + len - 1] == L'\r') --len;
            lines.push_back({ cur, len });
            cur = nl + 1;
        }
    }
    if (lines.empty()) lines.push_back({ 0, 0 });

    float& scrollY = m_areaScrolls[id];

    if (m_in) {
        if (hot && m_in->wheel != 0) {
            scrollY = std::max(0.0f, scrollY - m_in->wheel * lineH * 3.0f);
        }

        if (hot && m_in->down && m_dragField != id && PressIn(r)) {
            SetFocus(id);
            m_dragField = id;
            int lIdx = std::clamp((int)((m_in->my - inner.top + scrollY) / lineH), 0, (int)lines.size() - 1);
            std::wstring lineStr = text.substr(lines[lIdx].start, lines[lIdx].len);
            int cIdx = CaretFromX(lineStr, size, m_in->mx - inner.left);
            m_caret = m_anchor = (int)(lines[lIdx].start + cIdx);
        } else if (m_dragField == id && m_in->down) {
            int lIdx = std::clamp((int)((m_in->my - inner.top + scrollY) / lineH), 0, (int)lines.size() - 1);
            std::wstring lineStr = text.substr(lines[lIdx].start, lines[lIdx].len);
            int cIdx = CaretFromX(lineStr, size, m_in->mx - inner.left);
            m_caret = (int)(lines[lIdx].start + cIdx);
        }
        if (hot && m_in->dblClick && m_focus == id) {
            m_anchor = 0;
            m_caret = (int)text.size();
        }
    }

    const bool focused = (m_focus == id);
    if (focused) {
        EditTextArea(text);
        lines.clear();
        cur = 0;
        while (cur <= text.size()) {
            size_t nl = text.find(L'\n', cur);
            if (nl == std::wstring::npos) {
                size_t len = text.size() - cur;
                if (len > 0 && text[cur + len - 1] == L'\r') --len;
                lines.push_back({ cur, len });
                break;
            } else {
                size_t len = nl - cur;
                if (len > 0 && text[cur + len - 1] == L'\r') --len;
                lines.push_back({ cur, len });
                cur = nl + 1;
            }
        }
        if (lines.empty()) lines.push_back({ 0, 0 });
    }

    const float totalContentH = lines.size() * lineH;
    const float maxScroll = std::max(0.0f, totalContentH - innerH);
    if (focused) {
        int cL = 0;
        for (int i = 0; i < (int)lines.size(); ++i) {
            size_t nextStart = (i + 1 < (int)lines.size()) ? lines[i + 1].start : (text.size() + 1);
            if ((size_t)m_caret < nextStart || i == (int)lines.size() - 1) {
                cL = i;
                break;
            }
        }
        const float caretTop = cL * lineH;
        if (caretTop < scrollY) scrollY = caretTop;
        if (caretTop + lineH > scrollY + innerH) scrollY = caretTop + lineH - innerH;
    }
    scrollY = std::clamp(scrollY, 0.0f, maxScroll);

    const float rad = std::floor(4 * m_s);
    m_r.FillRound(r, rad, theme::Sunken);
    m_r.Stroke(r, focused ? theme::Ac() : (hot ? theme::BorderHi : theme::Border),
               std::max(1.0f, std::floor((focused ? 1.6f : 1.0f) * m_s)));

    if (text.empty() && !focused) {
        m_r.PushClip(inner);
        float py = inner.top;
        size_t pcur = 0;
        while (pcur <= placeholder.size()) {
            size_t pnl = placeholder.find(L'\n', pcur);
            std::wstring pline = (pnl == std::wstring::npos) ? placeholder.substr(pcur) : placeholder.substr(pcur, pnl - pcur);
            m_r.Text(pline, D2D1::RectF(inner.left, py, inner.right, py + lineH), theme::TextDim, size, Renderer::Align::Left, false, mono);
            py += lineH;
            if (pnl == std::wstring::npos) break;
            pcur = pnl + 1;
        }
        m_r.PopClip();
        return text != before;
    }

    m_r.PushClip(inner);

    const int selStart = std::min(m_caret, m_anchor);
    const int selEnd   = std::max(m_caret, m_anchor);
    const bool hasSel  = (focused && m_caret != m_anchor);

    for (int i = 0; i < (int)lines.size(); ++i) {
        const float ly = inner.top + i * lineH - scrollY;
        if (ly + lineH < inner.top || ly > inner.bottom) continue;

        const size_t lineStart = lines[i].start;
        const size_t lineLen   = lines[i].len;
        const std::wstring lineStr = text.substr(lineStart, lineLen);

        if (hasSel) {
            const int lineStartI = (int)lineStart;
            const int lineEndI   = (int)(lineStart + lineLen);
            const int sLo = std::max(selStart, lineStartI);
            const int sHi = std::min(selEnd, lineEndI);
            if (sLo < sHi) {
                const float x1 = inner.left + m_r.MeasureText(lineStr.substr(0, (size_t)(sLo - lineStartI)), size, false, mono);
                const float x2 = inner.left + m_r.MeasureText(lineStr.substr(0, (size_t)(sHi - lineStartI)), size, false, mono);
                m_r.Fill(D2D1::RectF(x1, ly, x2, ly + lineH), theme::Ac(), 0.35f);
            }
        }

        m_r.Text(lineStr, D2D1::RectF(inner.left, ly, inner.right, ly + lineH),
                 theme::TextHi, size, Renderer::Align::Left, false, mono);
    }

    if (focused && m_caretOn) {
        int cL = 0, cC = 0;
        for (int i = 0; i < (int)lines.size(); ++i) {
            size_t nextStart = (i + 1 < (int)lines.size()) ? lines[i + 1].start : (text.size() + 1);
            if ((size_t)m_caret < nextStart || i == (int)lines.size() - 1) {
                cL = i;
                cC = std::min((int)lines[i].len, (int)((size_t)m_caret - lines[i].start));
                break;
            }
        }
        const float caretY = inner.top + cL * lineH - scrollY;
        if (caretY + lineH >= inner.top && caretY <= inner.bottom) {
            const std::wstring linePrefix = text.substr(lines[cL].start, (size_t)cC);
            const float caretX = inner.left + m_r.MeasureText(linePrefix, size, false, mono);
            m_r.Line(caretX, caretY + std::floor(1 * m_s), caretX, caretY + lineH - std::floor(1 * m_s), theme::TextHi, std::max(1.5f, std::floor(1.8f * m_s)));
        }
    }

    if (maxScroll > 0.0f) {
        const float barTrackH = innerH;
        const float barThumbH = std::max(std::floor(20 * m_s), barTrackH * (innerH / totalContentH));
        const float thumbY = inner.top + (scrollY / maxScroll) * (barTrackH - barThumbH);
        const float sbW = std::floor(3 * m_s);
        const D2D1_RECT_F thumbR = D2D1::RectF(r.right - sbW - std::floor(3 * m_s), thumbY, r.right - std::floor(3 * m_s), thumbY + barThumbH);
        m_r.FillRound(thumbR, sbW * 0.5f, 0x4A5568, 0.6f);
    }

    m_r.PopClip();

    return text != before;
}

// ------------------------------------------------------- sayi / renk -------
// Metin her karede sinirlanmis degerden yeniden uretilseydi kismi girdi
// aninda bozulurdu ("22" silinince "1", sonra "80801"). Odakliyken kalici
// tampon duzenlenir; deger yalnizca gecerliyse yazilir.

bool Ui::NumField(int id, const D2D1_RECT_F& r, int& value, int lo, int hi,
                  const std::wstring& placeholder) {
    const int before = value;
    // Odak gitti: gecerli her tus zaten yazildi; kalan (aralik disi/bos)
    // tampon atilir ve alan son gecerli degeri gosterir. Burada yazmak,
    // tampon baska bir kayda (baska host) aitse yanlis yere yazardi.
    if (m_editId == id && m_focus != id) {
        m_editId = 0;
        m_editBuf.clear();
    }

    bool enter = false;
    if (m_focus == id && m_in) {
        for (const UiKey& k : m_in->keys) if (k.vk == VK_RETURN) enter = true;
        std::wstring keep;
        for (wchar_t c : m_in->chars) if (IsDigitW(c)) keep.push_back(c);
        m_in->chars.swap(keep);
    }

    std::wstring tmp;
    if (m_editId != id) tmp = std::to_wstring(value);
    std::wstring& text = (m_editId == id) ? m_editBuf : tmp;
    Field(id, r, text, placeholder);

    if (m_focus == id) {
        if (m_editId != id) { m_editBuf = text; m_editId = id; }   // odak bu karede geldi
        std::wstring& b = m_editBuf;
        b.erase(std::remove_if(b.begin(), b.end(), [](wchar_t c) { return !IsDigitW(c); }), b.end());
        if (b.size() > 9) b.resize(9);
        long long v = 0;
        const bool ok = ParseDigits(b, v);
        if (ok && v >= lo && v <= hi) value = (int)v;
        if (enter) {
            if (ok) value = (int)std::clamp<long long>(v, lo, hi);
            b = std::to_wstring(value);
        }
    }
    return value != before;
}

bool Ui::HexColorField(int id, const D2D1_RECT_F& r, uint32_t& rgb,
                       const std::wstring& placeholder) {
    const uint32_t before = rgb;
    if (m_editId == id && m_focus != id) {   // odak gitti: yarim kalan metin atilir
        m_editId = 0;
        m_editBuf.clear();
    }

    bool enter = false;
    if (m_focus == id && m_in) {
        for (const UiKey& k : m_in->keys) if (k.vk == VK_RETURN) enter = true;
        std::wstring keep;
        for (wchar_t c : m_in->chars) if (c == L'#' || HexVal(c) >= 0) keep.push_back(c);
        m_in->chars.swap(keep);
    }

    std::wstring tmp;
    if (m_editId != id) tmp = FormatHex6(rgb);
    std::wstring& text = (m_editId == id) ? m_editBuf : tmp;
    Field(id, r, text, placeholder);

    if (m_focus == id) {
        if (m_editId != id) { m_editBuf = text; m_editId = id; }
        // '#' yalniz basta, en fazla alti onaltilik hane (yapistirma dahil)
        std::wstring clean;
        int digits = 0;
        for (wchar_t c : m_editBuf) {
            if (c == L'#' && clean.empty()) { clean.push_back(c); continue; }
            if (HexVal(c) >= 0 && digits < 6) { clean.push_back((wchar_t)towupper(c)); ++digits; }
        }
        m_editBuf.swap(clean);
        uint32_t v = 0;
        if (ParseHex6(m_editBuf, v)) rgb = v;
        if (enter) m_editBuf = FormatHex6(rgb);
    }
    return rgb != before;
}

// ---------------------------------------------------------------- onay -----

bool Ui::Check(int id, const D2D1_RECT_F& r, bool& value, const std::wstring& label) {
    const bool hot = Hot(r);
    const float box = std::floor(17 * m_s);
    const float cy = (r.top + r.bottom) * 0.5f;
    const D2D1_RECT_F b = D2D1::RectF(r.left, cy - box * 0.5f, r.left + box, cy + box * 0.5f);

    m_r.FillRound(b, std::floor(3 * m_s), value ? theme::Ac() : theme::Sunken);
    if (!value) m_r.Stroke(b, hot ? theme::BorderHi : theme::Border, std::max(1.0f, std::floor(1 * m_s)));
    if (value) {
        const float w = std::max(1.5f, std::floor(1.8f * m_s));
        m_r.Line(b.left + box * 0.24f, cy, b.left + box * 0.44f, cy + box * 0.22f, 0x0A0D11, w);
        m_r.Line(b.left + box * 0.44f, cy + box * 0.22f, b.left + box * 0.78f, cy - box * 0.24f, 0x0A0D11, w);
    }
    m_r.Text(label, D2D1::RectF(b.right + std::floor(10 * m_s), r.top, r.right, r.bottom),
             hot ? theme::TextHi : theme::Text, 12.5f * m_s);

    if (hot && m_in && m_in->clicked && PressIn(r)) {
        value = !value;
        SetFocus(0);
        return true;
    }
    (void)id;
    return false;
}

// ------------------------------------------------------- segment secici ----

bool Ui::Choice(int id, const D2D1_RECT_F& r, const std::vector<std::wstring>& items, int& index) {
    if (items.empty()) return false;
    const float rad = std::floor(4 * m_s);
    m_r.FillRound(r, rad, theme::Sunken);
    m_r.Stroke(r, theme::Border, std::max(1.0f, std::floor(1 * m_s)));

    const float pad = std::floor(2 * m_s);
    const float w = (r.right - r.left - pad * 2) / (float)items.size();
    bool changed = false;

    for (size_t i = 0; i < items.size(); ++i) {
        const D2D1_RECT_F seg = D2D1::RectF(r.left + pad + w * i, r.top + pad,
                                            r.left + pad + w * (i + 1), r.bottom - pad);
        const bool active = ((int)i == index);
        const bool hot = Hot(seg);
        if (active)     m_r.FillRound(seg, rad - 1, theme::Ac(), 0.20f);
        else if (hot)   m_r.FillRound(seg, rad - 1, theme::Elevated);
        m_r.Text(items[i], seg, active ? theme::AcHi() : (hot ? theme::TextHi : theme::TextMuted),
                 12.0f * m_s, Renderer::Align::Center, active);
        if (hot && m_in && m_in->clicked && !active && PressIn(seg)) {
            index = (int)i;
            changed = true;
            SetFocus(0);
        }
    }
    (void)id;
    return changed;
}

// --------------------------------------------------------------- metin -----

void Ui::Label(const D2D1_RECT_F& r, const std::wstring& s, uint32_t color, float sizePx,
               bool bold, Renderer::Align a, bool mono) {
    m_r.Text(s, r, color, sizePx, a, bold, mono);
}

void Ui::Caption(const D2D1_RECT_F& r, const std::wstring& s) {
    m_r.Text(s, r, theme::TextDim, 10.5f * m_s, Renderer::Align::Left, false, true);
}

void Ui::Separator(float x0, float x1, float y) {
    m_r.Fill(D2D1::RectF(x0, y, x1, y + 1), theme::Border);
}

void Ui::Badge(const D2D1_RECT_F& r, const std::wstring& text, uint32_t color) {
    m_r.FillRound(r, std::floor(3 * m_s), color, 0.18f);
    m_r.Text(text, r, color, 9.5f * m_s, Renderer::Align::Center, true, true);
}

} // namespace ft
