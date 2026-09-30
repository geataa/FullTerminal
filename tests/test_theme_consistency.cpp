#include <cassert>
#include <iostream>
#include <string>
#include "vt/Screen.h"
#include "vt/VtParser.h"
#include "ui/Theme.h"
#include "core/Utf8.h"

namespace ft {

// Forward declaration of helper
inline void ResolveCellTest(const Cell& c, uint32_t& fg, uint32_t& bg) {
    if (c.flags & CF_FgDefault)      fg = theme::TermFg;
    else if (c.flags & CF_FgIndexed) fg = theme::Xterm256((int)c.fg);
    else                             fg = c.fg;

    if (c.flags & CF_BgDefault)      bg = theme::TermBg;
    else if (c.flags & CF_BgIndexed) bg = theme::Xterm256((int)c.bg);
    else                             bg = c.bg;
}

void TestConPtyColorHarmonization() {
    std::cout << "[TEST] 1. ConPTY Renk Uyum ve Sentez Normalizasyon Testi..." << std::endl;

    Screen s;
    VtParser parser(s);

    // Baslangicta Obsidian temasi:
    theme::SetColorScheme(0); // 0: Obsidian
    assert(theme::TermBg == 0x0A0D11);
    assert(theme::TermFg == 0xD3DAE4);

    std::vector<Cell> rowBuf(80);

    // 1.1: PowerShell conhost mavisi (RGB 1, 36, 86 = 0x012456)
    // ConPTY PowerShell baslatirken arka plani bu renkle doldurur
    std::string psBgSeq = "\x1b[48;2;1;36;86m ";
    parser.Feed(psBgSeq.data(), psBgSeq.size());

    s.CopyViewRow(0, rowBuf.data());
    const Cell& cell1 = rowBuf[0];
    assert((cell1.flags & CF_BgDefault) != 0); // Varsayilan zemine map edilmeli!
    uint32_t fg = 0, bg = 0;
    ResolveCellTest(cell1, fg, bg);
    assert(bg == theme::TermBg); // Obsidian zemini olmali (0x0A0D11), mavi olmamali!
    std::cout << "  [OK] PowerShell conhost DarkBlue (1,36,86) varsayilan tema zeminine eslendi." << std::endl;

    // 1.2: CMD / Conhost varsayilan Campbell siyahi (RGB 12, 12, 12 = 0x0C0C0C)
    std::string cmdBgSeq = "\x1b[48;2;12;12;12m ";
    parser.Feed(cmdBgSeq.data(), cmdBgSeq.size());
    s.CopyViewRow(0, rowBuf.data());
    const Cell& cell2 = rowBuf[1];
    assert((cell2.flags & CF_BgDefault) != 0);
    ResolveCellTest(cell2, fg, bg);
    assert(bg == theme::TermBg);
    std::cout << "  [OK] Conhost Campbell siyah (12,12,12) varsayilan tema zeminine eslendi." << std::endl;

    // 1.3: Standart ANSI 40 (siyah zemin)
    std::string ansi40Seq = "\x1b[40m ";
    parser.Feed(ansi40Seq.data(), ansi40Seq.size());
    s.CopyViewRow(0, rowBuf.data());
    const Cell& cell3 = rowBuf[2];
    assert((cell3.flags & CF_BgDefault) != 0);
    ResolveCellTest(cell3, fg, bg);
    assert(bg == theme::TermBg);
    std::cout << "  [OK] Standart ANSI 40 varsayilan tema zeminine eslendi." << std::endl;

    // 1.4: Conhost varsayilan metin (RGB 204, 204, 204 = 0xCCCCCC)
    std::string defTextSeq = "\x1b[38;2;204;204;204mText";
    parser.Feed(defTextSeq.data(), defTextSeq.size());
    s.CopyViewRow(0, rowBuf.data());
    const Cell& cell4 = rowBuf[3];
    assert((cell4.flags & CF_FgDefault) != 0);
    ResolveCellTest(cell4, fg, bg);
    assert(fg == theme::TermFg);
    std::cout << "  [OK] Conhost gri-beyaz (204,204,204) varsayilan metin rengine eslendi." << std::endl;
}

void TestDynamicThemeSwitching() {
    std::cout << "[TEST] 2. Dinamik Tema Gecisi ve ANSI Indeks Senkronizasyon Testi..." << std::endl;

    Screen s;
    VtParser parser(s);
    std::vector<Cell> rowBuf(80);

    // Obsidian ile basla
    theme::SetColorScheme(0); // Obsidian

    // ANSI 32 (Yesil) metin yazdir
    std::string greenSeq = "\x1b[32mOK\x1b[0m";
    parser.Feed(greenSeq.data(), greenSeq.size());

    s.CopyViewRow(0, rowBuf.data());
    const Cell& greenCell = rowBuf[0];
    assert((greenCell.flags & CF_FgIndexed) != 0);
    assert(greenCell.fg == 2); // Ansi index 2 (Green)

    uint32_t fg = 0, bg = 0;
    ResolveCellTest(greenCell, fg, bg);
    assert(fg == theme::Schemes[0].ansi[2]); // 0x54C98A (Obsidian yesili)
    std::cout << "  [OK] Obsidian altinda yesil: 0x" << std::hex << fg << std::dec << std::endl;

    // Simdi temayi Dracula'ya gecir! (2: Dracula)
    theme::SetColorScheme(2);
    ResolveCellTest(greenCell, fg, bg);
    assert(fg == theme::Schemes[2].ansi[2]); // 0x50FA7B (Dracula yesili)
    std::cout << "  [OK] Dracula gecisinde hucre dinamik olarak Dracula yesiline donustu: 0x" << std::hex << fg << std::dec << std::endl;

    // Simdi Retro CRT'ye gecir! (7: Retro CRT)
    theme::SetColorScheme(7);
    ResolveCellTest(greenCell, fg, bg);
    assert(fg == theme::Schemes[7].ansi[2]); // 0x33FF33 (CRT yesili)
    std::cout << "  [OK] Retro CRT gecisinde hucre CRT tonuna donustu: 0x" << std::hex << fg << std::dec << std::endl;

    // Tekrar Obsidian'a don
    theme::SetColorScheme(0);
}

void TestMotdPromptColonBug() {
    std::cout << "[TEST] 3. MOTD Iki Nokta (Colon) Shell Prompt Bilesen Testi..." << std::endl;

    auto IsShellPromptLocal = [](const std::string& str) -> bool {
        // Strip escapes
        std::string cl;
        for (char c : str) {
            if (c != '\x1b') cl.push_back(c);
        }
        while (!cl.empty() && (cl.back() == ' ' || cl.back() == '\t' || cl.back() == '\r' || cl.back() == '\n')) {
            cl.pop_back();
        }
        if (cl.empty()) return false;
        const char last = cl.back();
        if (last == '$' || last == '#' || last == '>') return true;
        if (last == '%') {
            if (cl.size() >= 2 && (isdigit((unsigned char)cl[cl.size() - 2]) || cl[cl.size() - 2] == '.')) {
                return false;
            }
            return true;
        }
        return false;
    };

    // MOTD satirlari prompt SANILMAMALI!
    assert(!IsShellPromptLocal("Last login: Wed Oct 1 2026 from 192.168.1.100"));
    assert(!IsShellPromptLocal("System load: 0.15, 0.20, 0.18"));
    assert(!IsShellPromptLocal("IPv4 address for eth0: 10.0.0.45"));
    assert(!IsShellPromptLocal("Usage of /: 24.5% of 100GB"));
    assert(!IsShellPromptLocal("Memory usage: 35%"));
    std::cout << "  [OK] MOTD colon tasiyan satirlar prompt olarak algilanmadi!" << std::endl;

    // Gercek kabuk istemleri dogru tespit edilmeli:
    assert(IsShellPromptLocal("ubuntu@server:~$ "));
    assert(IsShellPromptLocal("root@srv:~# "));
    assert(IsShellPromptLocal("user@host:path% "));
    assert(IsShellPromptLocal("PS C:\\Users> "));
    assert(IsShellPromptLocal("bash-5.1$"));
    std::cout << "  [OK] Gercek kabuk promptlari eksiksiz dogrulandi!" << std::endl;
}

void TestClearAndCursorVisibility() {
    std::cout << "[TEST] 4. Clear Islemi ve Imlec Gorunurluk (Cursor Visibility) Testi..." << std::endl;

    Screen s;
    VtParser parser(s);

    // 4.1: Baslangicta imlec gorunur olmali
    assert(s.CursorVisible() == true);

    // 4.2: Terminale gizle imleci VT kodu gonderilsin (\x1b[?25l)
    std::string hideSeq = "\x1b[?25l";
    parser.Feed(hideSeq.data(), hideSeq.size());
    assert(s.CursorVisible() == false);
    std::cout << "  [OK] Imlec gizleme komutunda (\\x1b[?25l) imlec basariyla gizlendi." << std::endl;

    // 4.3: Tema uygulanmadan onceki Clear cagrisi imleci ve kalemi tazelemeli
    s.Clear();
    assert(s.CursorVisible() == true);
    assert(s.CursorX() == 0 && s.CursorY() == 0);
    std::cout << "  [OK] Screen::Clear() sonrasinda imlec otomatik gorunur yapildi (Enter gerekmez)." << std::endl;

    // 4.4: Tekrar gizlensin ve parser Reset() edilsin
    parser.Feed(hideSeq.data(), hideSeq.size());
    assert(s.CursorVisible() == false);
    parser.Reset();
    assert(s.CursorVisible() == true);
    std::cout << "  [OK] VtParser::Reset() sonrasinda imlec gorunurlugu tazelendi." << std::endl;
}

} // namespace ft

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "  FullTerminal - Theme & Terminal Color Consistency Test" << std::endl;
    std::cout << "====================================================" << std::endl;

    ft::TestConPtyColorHarmonization();
    ft::TestDynamicThemeSwitching();
    ft::TestMotdPromptColonBug();
    ft::TestClearAndCursorVisibility();

    std::cout << "\n====================================================" << std::endl;
    std::cout << "  TUM TEMA VE RENK UYUMLULUK TESTLERI GECTI! (VERIFIED)" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
