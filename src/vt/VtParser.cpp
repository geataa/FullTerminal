#include "vt/VtParser.h"
#include "core/Utf8.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cstdlib>

namespace ft {
namespace {
constexpr size_t kMaxParams = 32;
constexpr size_t kMaxOsc    = 8192;
}

void VtParser::Reset() {
    m_state = State::Ground;
    ClearParams();
    m_osc.clear();
    m_stringEsc = false;
    m_cp = 0;
    m_need = 0;
    m_cursorStyle = 0;
    m_s.SetCursorVisible(true);
}

void VtParser::ClearParams() {
    m_params.clear();
    m_subMask = 0;
    m_curIsSub = false;
    m_curParam = 0;
    m_haveParam = false;
    m_sawParam = false;
    m_private = false;
    m_intermediate.clear();
}

// "1;;2" -> [1,0,2],  "1;" -> [1,0],  "" -> []
void VtParser::ParamDigit(uint8_t d) {
    if (m_curParam < 100000) m_curParam = m_curParam * 10 + (d - '0');
    m_haveParam = true;
    m_sawParam = true;
}

void VtParser::PushParam() {
    static_assert(kMaxParams <= 32, "m_subMask 32 bit");
    if (m_params.size() < kMaxParams) {
        if (m_curIsSub) m_subMask |= 1u << m_params.size();
        m_params.push_back(m_haveParam ? m_curParam : 0);
    }
}

// ':' sonraki parametreyi bir oncekinin alt parametresi yapar (38:2::r:g:b).
// ';' ile ayni diziye dusmeleri gerekiyor ama SGR ikisini ayirt etmeli.
void VtParser::ParamSeparator(bool colon) {
    PushParam();
    m_curIsSub = colon;
    m_curParam = 0;
    m_haveParam = false;
    m_sawParam = true;
}

void VtParser::ParamFinish() {
    if (!m_sawParam) return;
    PushParam();
    m_curIsSub = false;
    m_curParam = 0;
    m_haveParam = false;
}

int VtParser::Param(size_t i, int fallback) const {
    if (i >= m_params.size()) return fallback;
    return m_params[i] <= 0 ? fallback : m_params[i];
}

void VtParser::Put(char32_t cp) {
    m_s.PutChar(cp);
}

void VtParser::Execute(uint8_t c) {
    switch (c) {
    case 0x07: if (onBell) onBell(); break;                    // BEL
    case 0x08: m_s.Backspace(); break;                         // BS
    case 0x09: m_s.Tab(1); break;                              // HT
    case 0x0A: case 0x0B: case 0x0C: m_s.LineFeed(); break;    // LF VT FF
    case 0x0D: m_s.CarriageReturn(); break;                    // CR
    default: break;                                            // SO/SI vb. yok sayilir
    }
}

void VtParser::Feed(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        const uint8_t b = (uint8_t)data[i];

        // ESC her yerde diziyi keser ve yeniden baslatir.
        // Metin dizileri (OSC/DCS) haric: orada ESC, ST'nin ilk yarisi olabilir.
        if (b == 0x1B &&
            m_state != State::OscString &&
            m_state != State::DcsIgnore &&
            m_state != State::StringIgnore) {
            m_state = State::Esc;
            ClearParams();
            m_stringEsc = false;
            m_need = 0;
            continue;
        }

        switch (m_state) {

        // ------------------------------------------------------------ GROUND
        case State::Ground: {
            if (b < 0x20) { Execute(b); m_need = 0; continue; }
            if (b == 0x7F) continue; // DEL

            if (m_need > 0) {
                if ((b & 0xC0) != 0x80) { // bozuk devam bayti
                    m_need = 0;
                    Put(U'�');
                    --i;
                    continue;
                }
                m_cp = (m_cp << 6) | (b & 0x3F);
                if (--m_need == 0) Put(m_cp);
                continue;
            }
            if (b < 0x80)                { Put(b); }
            else if ((b & 0xE0) == 0xC0) { m_cp = b & 0x1F; m_need = 1; }
            else if ((b & 0xF0) == 0xE0) { m_cp = b & 0x0F; m_need = 2; }
            else if ((b & 0xF8) == 0xF0) { m_cp = b & 0x07; m_need = 3; }
            else                         { Put(U'�'); }
            continue;
        }

        // --------------------------------------------------------------- ESC
        case State::Esc: {
            if (b == '[') { m_state = State::CsiEntry; ClearParams(); continue; }
            if (b == ']') { m_state = State::OscString; m_osc.clear(); continue; }
            if (b == 'P') { m_state = State::DcsIgnore; continue; }
            if (b == 'X' || b == '^' || b == '_') { m_state = State::StringIgnore; continue; }
            if (b >= 0x20 && b <= 0x2F) { m_intermediate.push_back((char)b); m_state = State::EscIntermediate; continue; }
            if (b < 0x20) { Execute(b); continue; }
            EscDispatch(b);
            m_state = State::Ground;
            continue;
        }

        case State::EscIntermediate: {
            if (b >= 0x20 && b <= 0x2F) { m_intermediate.push_back((char)b); continue; }
            if (b < 0x20) { Execute(b); continue; }
            EscDispatch(b);
            m_state = State::Ground;
            continue;
        }

        // --------------------------------------------------------------- CSI
        case State::CsiEntry:
        case State::CsiParam: {
            if (b < 0x20) { Execute(b); continue; }
            if (b >= '0' && b <= '9') { ParamDigit(b); m_state = State::CsiParam; continue; }
            if (b == ';' || b == ':') { ParamSeparator(b == ':'); m_state = State::CsiParam; continue; }
            if (m_state == State::CsiEntry && b >= '<' && b <= '?') {
                m_private = true;
                m_intermediate.push_back((char)b);
                continue;
            }
            if (b >= 0x20 && b <= 0x2F) {
                m_intermediate.push_back((char)b);
                m_state = State::CsiIntermediate;
                continue;
            }
            if (b >= 0x40 && b <= 0x7E) {
                ParamFinish();
                CsiDispatch(b);
                m_state = State::Ground;
                ClearParams();
                continue;
            }
            m_state = State::CsiIgnore;
            continue;
        }

        case State::CsiIntermediate: {
            if (b < 0x20) { Execute(b); continue; }
            if (b >= 0x20 && b <= 0x2F) { m_intermediate.push_back((char)b); continue; }
            if (b >= 0x40 && b <= 0x7E) {
                ParamFinish();
                CsiDispatch(b);
                m_state = State::Ground;
                ClearParams();
                continue;
            }
            m_state = State::CsiIgnore;
            continue;
        }

        case State::CsiIgnore: {
            if (b >= 0x40 && b <= 0x7E) { m_state = State::Ground; ClearParams(); }
            continue;
        }

        // --------------------------------------------------------------- OSC
        // ST = ESC \ . Icerikte gecen duz ters bolu diziyi bitirmemeli,
        // o yuzden ESC'yi ayri bir bayrakla takip ediyoruz.
        case State::OscString: {
            if (m_stringEsc) {
                m_stringEsc = false;
                if (b == '\\') { OscDispatch(); m_state = State::Ground; continue; }
                m_osc.clear();
                m_state = State::Esc;
                ClearParams();
                --i;                 // ESC sonrasi bayti bastan isle
                continue;
            }
            if (b == 0x1B) { m_stringEsc = true; continue; }
            if (b == 0x07) { OscDispatch(); m_state = State::Ground; continue; }
            if (b == 0x18 || b == 0x1A) { m_osc.clear(); m_state = State::Ground; continue; }
            if (m_osc.size() < kMaxOsc) m_osc.push_back((char)b);
            continue;
        }

        // ------------------------------------------------- DCS / SOS-PM-APC
        case State::DcsIgnore:
        case State::StringIgnore: {
            if (m_stringEsc) {
                m_stringEsc = false;
                if (b == '\\') { m_state = State::Ground; continue; }
                m_state = State::Esc;
                ClearParams();
                --i;
                continue;
            }
            if (b == 0x1B) { m_stringEsc = true; continue; }
            if (b == 0x07 || b == 0x9C) { m_state = State::Ground; }
            continue;
        }
        }
    }
}

// ------------------------------------------------------------------- ESC ---

void VtParser::EscDispatch(uint8_t final) {
    if (!m_intermediate.empty()) {
        if (m_intermediate[0] == '#' && final == '8') { m_s.FillScreen(U'E'); return; }
        // Karakter seti atamalari: ( ) * + — M0'da yok sayiliyor.
        return;
    }
    switch (final) {
    case '7': m_s.SaveCursor(); break;
    case '8': m_s.RestoreCursor(); break;
    case 'D': m_s.LineFeed(); break;
    case 'E': m_s.NextLine(); break;
    case 'M': m_s.ReverseIndex(); break;
    case 'H': m_s.SetTabStop(); break;
    case 'c': m_s.Reset(); m_cursorStyle = 0; break;
    case '=': m_s.SetAppKeypad(true); break;
    case '>': m_s.SetAppKeypad(false); break;
    default: break;
    }
}

// ------------------------------------------------------------------- OSC ---

void VtParser::OscDispatch() {
    if (m_osc.empty()) return;
    size_t sep = m_osc.find(';');
    const std::string numStr = m_osc.substr(0, sep);
    const std::string body = (sep == std::string::npos) ? std::string() : m_osc.substr(sep + 1);
    m_osc.clear();

    int num = -1;
    if (!numStr.empty() && numStr.find_first_not_of("0123456789") == std::string::npos) {
        num = std::atoi(numStr.c_str());
    }

    switch (num) {
    case 0: case 2:
        m_s.title = Utf8ToWide(body);
        if (onTitle) onTitle();
        break;
    case 7: { // OSC 7: calisma dizini (FR-TERM-014)
        std::string p = body;
        const std::string pfx = "file://";
        if (p.rfind(pfx, 0) == 0) {
            size_t slash = p.find('/', pfx.size());
            p = (slash == std::string::npos) ? std::string() : p.substr(slash);
        }
        if (!p.empty() && p[0] == '/' && p.size() > 3 && p[2] == ':') p.erase(0, 1);
        m_s.cwd = Utf8ToWide(p);
        break;
    }
    case 8: { // OSC 8: Hyperlinks (params;url)
        std::string url;
        size_t pSep = body.find(';');
        if (pSep != std::string::npos) {
            url = body.substr(pSep + 1);
        } else {
            url = body;
        }
        m_s.SetCurrentHyperlink(url);
        break;
    }
    case 133: { // OSC 133: Semantic Shell Integration (FinalTerm)
        m_s.HandleOsc133(body);
        break;
    }
    default:
        break; // 4, 10, 11, 52 ... sonraki milestone'lar
    }
}

// ------------------------------------------------------------------- CSI ---

void VtParser::CsiDispatch(uint8_t final) {
    const bool priv = m_private;

    switch (final) {
    case '@': m_s.InsertChars(Param(0, 1)); break;
    case 'A': m_s.MoveCursor(0, -Param(0, 1)); break;
    case 'B': m_s.MoveCursor(0,  Param(0, 1)); break;
    case 'C': m_s.MoveCursor( Param(0, 1), 0); break;
    case 'D': m_s.MoveCursor(-Param(0, 1), 0); break;
    case 'E': m_s.MoveCursor(0,  Param(0, 1)); m_s.SetCursorCol(0); break;
    case 'F': m_s.MoveCursor(0, -Param(0, 1)); m_s.SetCursorCol(0); break;
    case 'G': case '`': m_s.SetCursorCol(Param(0, 1) - 1); break;
    case 'H': case 'f': m_s.SetCursor(Param(1, 1) - 1, Param(0, 1) - 1); break;
    case 'I': m_s.Tab(Param(0, 1)); break;
    case 'J': m_s.EraseInDisplay(m_params.empty() ? 0 : m_params[0]); break;
    case 'K': m_s.EraseInLine(m_params.empty() ? 0 : m_params[0]); break;
    case 'L': m_s.InsertLines(Param(0, 1)); break;
    case 'M': m_s.DeleteLines(Param(0, 1)); break;
    case 'P': m_s.DeleteChars(Param(0, 1)); break;
    case 'S': m_s.ScrollUp(Param(0, 1)); break;
    case 'T': m_s.ScrollDown(Param(0, 1)); break;
    case 'X': m_s.EraseChars(Param(0, 1)); break;
    case 'Z': m_s.BackTab(Param(0, 1)); break;
    case 'a': m_s.MoveCursor(Param(0, 1), 0); break;
    case 'd': m_s.SetCursorRow(Param(0, 1) - 1); break;
    case 'e': m_s.MoveCursor(0, Param(0, 1)); break;

    case 'h': SetMode(priv, true); break;
    case 'l': SetMode(priv, false); break;
    case 'm': ApplySgr(); break;

    case 'n': {
        const int p = m_params.empty() ? 0 : m_params[0];
        if (!priv && onReply) {
            if (p == 5) onReply("\x1b[0n");
            else if (p == 6) {
                onReply("\x1b[" + std::to_string(m_s.CursorY() + 1) + ";" +
                        std::to_string(m_s.CursorX() + 1) + "R");
            }
        }
        break;
    }

    case 'c':
        if (onReply && !priv) onReply("\x1b[?62;22c"); // VT220 + renk
        break;

    case 'r':
        if (!priv) m_s.SetScrollRegion(Param(0, 1) - 1, Param(1, m_s.Rows()) - 1);
        break;

    case 's': m_s.SaveCursor(); break;
    case 'u': m_s.RestoreCursor(); break;

    case 'q':
        if (!m_intermediate.empty() && m_intermediate.back() == ' ') {
            m_cursorStyle = m_params.empty() ? 0 : m_params[0]; // DECSCUSR
        }
        break;

    case 'p':
        if (!m_intermediate.empty() && m_intermediate[0] == '!') { // DECSTR
            m_s.Reset();
            m_cursorStyle = 0;
        }
        break;

    case 't': break; // pencere islemleri: yok sayiliyor
    default: break;
    }
}

void VtParser::SetMode(bool priv, bool on) {
    for (size_t i = 0; i < m_params.size(); ++i) {
        const int m = m_params[i];
        if (!priv) {
            // ANSI modlari: 4 IRM, 20 LNM. M0'da gerekmiyor.
            continue;
        }
        switch (m) {
        case 1:    m_s.SetAppCursorKeys(on); break;
        case 6:    m_s.SetOriginMode(on); break;
        case 7:    m_s.SetAutoWrap(on); break;
        case 25:   m_s.SetCursorVisible(on); break;
        // xterm gibi: 1000/1002/1003'ten herhangi birinin RST'si izlemeyi
        // tamamen kapatir (hangisi acik olursa olsun).
        case 1000: m_s.SetMouseMode(on ? 1000 : 0); break;
        case 1002: m_s.SetMouseMode(on ? 1002 : 0); break;
        case 1003: m_s.SetMouseMode(on ? 1003 : 0); break;
        case 1006: m_s.SetMouseSgr(on); break;
        case 1047: m_s.UseAltBuffer(on); break;
        case 1048: on ? m_s.SaveCursor() : m_s.RestoreCursor(); break;
        case 1049:
            if (on) { m_s.SaveCursor(); m_s.UseAltBuffer(true); }
            else    { m_s.UseAltBuffer(false); m_s.RestoreCursor(); }
            break;
        case 2004: m_s.SetBracketedPaste(on); break;
        default: break;
        }
    }
}

// ------------------------------------------------------------------- SGR ---

void VtParser::ApplySgr() {
    Pen& p = m_s.pen();
    if (m_params.empty()) { p.Reset(); return; }

    for (size_t i = 0; i < m_params.size(); ++i) {
        const int v = m_params[i];
        switch (v) {
        case 0:  p.Reset(); break;
        case 1:  p.flags |= CF_Bold; break;
        case 2:  p.flags |= CF_Dim; break;
        case 3:  p.flags |= CF_Italic; break;
        case 4:  // 4:0 alti cizgisiz, 4:1..5 cizgi stilleri (hepsi duz cizilir)
            if (IsSubParam(i + 1) && m_params[i + 1] == 0) p.flags &= ~CF_Underline;
            else p.flags |= CF_Underline;
            break;
        case 5: case 6: p.flags |= CF_Blink; break;
        case 7:  p.flags |= CF_Inverse; break;
        case 8:  p.flags |= CF_Hidden; break;
        case 9:  p.flags |= CF_Strike; break;
        case 21: case 22: p.flags &= ~(CF_Bold | CF_Dim); break;
        case 23: p.flags &= ~CF_Italic; break;
        case 24: p.flags &= ~CF_Underline; break;
        case 25: p.flags &= ~CF_Blink; break;
        case 27: p.flags &= ~CF_Inverse; break;
        case 28: p.flags &= ~CF_Hidden; break;
        case 29: p.flags &= ~CF_Strike; break;

        case 39: p.flags |= CF_FgDefault; p.flags &= ~CF_FgIndexed; p.fg = 0; break;
        case 49: p.flags |= CF_BgDefault; p.flags &= ~CF_BgIndexed; p.bg = 0; break;

        case 38: case 48: case 58: { // 58 alt cizgi rengi: tuketilir, cizilmez
            const bool fg = (v == 38);
            const size_t n = m_params.size();
            bool have = false;
            bool isIndexed = false;
            uint32_t c = 0;
            auto rgb = [&](size_t b) {
                return ((uint32_t)(m_params[b] & 0xFF) << 16) |
                       ((uint32_t)(m_params[b + 1] & 0xFF) << 8) |
                        (uint32_t)(m_params[b + 2] & 0xFF);
            };

            if (IsSubParam(i + 1)) {
                // Iki nokta uslubu: 38:5:i, 38:2:r:g:b ya da 38:2:cs:r:g:b.
                // Alt parametre grubunun tamami tuketilir.
                size_t k = 0;
                while (i + 1 + k < n && IsSubParam(i + 1 + k)) ++k;
                const int kind = m_params[i + 1];
                if (kind == 5 && k >= 2) {
                    c = (uint32_t)(m_params[i + 2] & 0xFF); have = true; isIndexed = true;
                } else if (kind == 2 && k >= 4) {
                    c = rgb(k >= 5 ? i + 3 : i + 2); have = true;
                }
                i += k;
            } else {
                // Noktali virgul uslubu: 38;5;i ve 38;2;r;g;b, renk uzayi alani yok.
                // Arkasindan gelen parametreler (;1 kalin, ;48;2;... ) ayri SGR'dir.
                const int kind = (i + 1 < n) ? m_params[i + 1] : -1;
                if (kind == 5) {
                    if (i + 2 < n) { c = (uint32_t)(m_params[i + 2] & 0xFF); have = true; isIndexed = true; i += 2; }
                    else i = n;
                } else if (kind == 2) {
                    if (i + 4 < n) { c = rgb(i + 2); have = true; i += 4; }
                    else i = n;
                }
            }
            if (have && v != 58) {
                if (fg) {
                    if (isIndexed) {
                        p.fg = c;
                        p.flags |= CF_FgIndexed;
                        p.flags &= ~CF_FgDefault;
                    } else {
                        // ConPTY conhost varsayilan metin sentezi (Campbell / Klasik beyaz-gri):
                        if (c == 0xCCCCCC || c == 0xC0C0C0 || c == 0xF2F2F2 || c == 0xEEEDF0 || c == 0xC5C5C5) {
                            p.flags |= CF_FgDefault;
                            p.flags &= ~CF_FgIndexed;
                            p.fg = 0;
                        } else {
                            p.fg = c;
                            p.flags &= ~(CF_FgDefault | CF_FgIndexed);
                        }
                    }
                } else {
                    if (isIndexed) {
                        if (c == 0) {
                            // 256 renk tablosunda 0 siyah -> karanlik temalarda terminal zemini
                            p.flags |= CF_BgDefault;
                            p.flags &= ~CF_BgIndexed;
                            p.bg = 0;
                        } else {
                            p.bg = c;
                            p.flags |= CF_BgIndexed;
                            p.flags &= ~CF_BgDefault;
                        }
                    } else {
                        // ConPTY conhost varsayilan zemin sentezi (Campbell siyah, PowerShell mavisi):
                        // 0x0C0C0C = Campbell siyah
                        // 0x000000 = Klasik siyah
                        // 0x012456 = PowerShell conhost DarkBlue (1, 36, 86)
                        // 0x000080 = Klasik PowerShell DarkBlue (0, 0, 128)
                        if (c == 0x0C0C0C || c == 0x000000 || c == 0x012456 || c == 0x000080) {
                            p.flags |= CF_BgDefault;
                            p.flags &= ~CF_BgIndexed;
                            p.bg = 0;
                        } else {
                            p.bg = c;
                            p.flags &= ~(CF_BgDefault | CF_BgIndexed);
                        }
                    }
                }
            }
            break;
        }

        default:
            if (v >= 30 && v <= 37) {
                if (v == 37) {
                    // ANSI 37 (standart beyaz/acik metin) -> aktif temanin varsayilan metin rengi
                    p.flags |= CF_FgDefault;
                    p.flags &= ~CF_FgIndexed;
                    p.fg = 0;
                } else {
                    p.fg = (uint32_t)(v - 30);
                    p.flags |= CF_FgIndexed;
                    p.flags &= ~CF_FgDefault;
                }
            }
            else if (v >= 40 && v <= 47) {
                if (v == 40) {
                    // ANSI 40 (standart siyah zemin) -> aktif temanin varsayilan tuval rengi
                    p.flags |= CF_BgDefault;
                    p.flags &= ~CF_BgIndexed;
                    p.bg = 0;
                } else {
                    p.bg = (uint32_t)(v - 40);
                    p.flags |= CF_BgIndexed;
                    p.flags &= ~CF_BgDefault;
                }
            }
            else if (v >= 90 && v <= 97) {
                p.fg = (uint32_t)(v - 90 + 8);
                p.flags |= CF_FgIndexed;
                p.flags &= ~CF_FgDefault;
            }
            else if (v >= 100 && v <= 107) {
                p.bg = (uint32_t)(v - 100 + 8);
                p.flags |= CF_BgIndexed;
                p.flags &= ~CF_BgDefault;
            }
            break;
        }
        // Tanimadigimiz alt parametreler (4:3 gibi) ayri SGR sanilmasin.
        while (i + 1 < m_params.size() && IsSubParam(i + 1)) ++i;
    }
}

} // namespace ft
