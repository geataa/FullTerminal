#pragma once
//
// Terminal ekran modeli: hucre izgarasi, scrollback, imlec, kaydirma bolgesi,
// alternatif tampon. VT ayristiricisi bu sinifin uzerine yazar.
//
#include "vt/Cell.h"

#include <vector>
#include <deque>
#include <string>
#include <cstdint>

namespace ft {

class Screen {
public:
    static constexpr int kScrollbackLimit = 10000; // FR-TERM-007 diske tasmayi getirecek
    static constexpr int kMaxCols = 2000;
    static constexpr int kMaxRows = 1000;

    Screen() { Resize(80, 24); }

    void Resize(int cols, int rows);
    int  Cols() const { return m_cols; }
    int  Rows() const { return m_rows; }

    // ---- Kalem ----------------------------------------------------------
    Pen&       pen()       { return m_pen; }
    const Pen& pen() const { return m_pen; }

    // ---- Yazim ----------------------------------------------------------
    void PutChar(char32_t cp);
    void LineFeed();
    void CarriageReturn();
    void Backspace();
    void Tab(int count);
    void BackTab(int count);
    void ReverseIndex();
    void NextLine();

    // ---- Imlec ----------------------------------------------------------
    void SetCursor(int x, int y);          // 0 tabanli, origin moduna gore kirpilir
    void MoveCursor(int dx, int dy);
    void SetCursorCol(int x);
    void SetCursorRow(int y);
    int  CursorX() const { return m_cx; }
    int  CursorY() const { return m_cy; }
    void SaveCursor();
    void RestoreCursor();
    void SetCursorVisible(bool v) { m_cursorVisible = v; Touch(); }
    bool CursorVisible() const    { return m_cursorVisible; }

    // ---- Silme ----------------------------------------------------------
    void EraseInDisplay(int mode); // 0 imlecten sona, 1 basa, 2 tumu, 3 scrollback
    void EraseInLine(int mode);    // 0 sona, 1 basa, 2 satir
    void EraseChars(int n);
    void InsertChars(int n);
    void DeleteChars(int n);
    void InsertLines(int n);
    void DeleteLines(int n);

    // ---- Kaydirma -------------------------------------------------------
    void SetScrollRegion(int top, int bottom); // 0 tabanli, bottom dahil
    void ScrollUp(int n);
    void ScrollDown(int n);

    // ---- Modlar ---------------------------------------------------------
    void SetAutoWrap(bool v)   { m_autoWrap = v; }
    bool AutoWrap() const      { return m_autoWrap; }
    void SetOriginMode(bool v) { m_originMode = v; SetCursor(0, 0); }
    bool OriginMode() const    { return m_originMode; }
    void UseAltBuffer(bool alt);
    bool IsAltBuffer() const   { return m_useAlt; }
    void SetBracketedPaste(bool v) { m_bracketedPaste = v; }
    bool BracketedPaste() const    { return m_bracketedPaste; }
    void SetAppCursorKeys(bool v)  { m_appCursorKeys = v; }
    bool AppCursorKeys() const     { return m_appCursorKeys; }
    void SetAppKeypad(bool v)      { m_appKeypad = v; }
    bool AppKeypad() const         { return m_appKeypad; }
    void SetMouseMode(int m)       { m_mouseMode = m; }
    int  MouseMode() const         { return m_mouseMode; }
    void SetMouseSgr(bool v)       { m_mouseSgr = v; }   // DECSET 1006
    bool MouseSgr() const          { return m_mouseSgr; }

    // ---- Sekme duraklari ------------------------------------------------
    void SetTabStop();
    void ClearTabStop(bool all);
    void ResetTabStops();

    // ---- Test / hizalama ------------------------------------------------
    void FillScreen(char32_t cp);
    void Reset();
    void Clear();

    // ---- Gorunum --------------------------------------------------------
    int  ScrollbackRows() const { return (int)m_scrollback.size(); }
    int  ViewOffset() const     { return m_viewOffset; }
    void ScrollViewLines(int delta);
    void ScrollViewToBottom();
    bool ViewAtBottom() const   { return m_viewOffset == 0; }

    // dst en az Cols() hucre tutmali.
    void CopyViewRow(int y, Cell* dst) const;
    // Imlec gorunur alanda mi; oyleyse konumunu doldurur.
    bool CursorInView(int& outX, int& outY) const;

    // Mutlak satir: scrollback'in ilk satirindan (kirpilip atilanlar dahil)
    // sayilan kararli indeks. Yeni cikti ve scrollback kirpilmasi bu numarayi
    // degistirmez; secim bunla tutulursa icerik kaysa da ayni metni gosterir.
    int64_t ViewRowToAbs(int viewRow) const;
    int     AbsToViewRow(int64_t absRow) const; // gorunum disindaysa <0 veya >=Rows()

    // Gorunum koordinatlarindaki secimi UTF-8 metne cevirir.
    std::string TextInRange(int x0, int y0, int x1, int y1) const;
    // Mutlak satirlarla secim (uclar dahil). Otomatik kaydirmayla bolunen
    // satirlar yeni satirsiz birlesir, gercek satir sonlari CRLF olur.
    std::string TextInAbsRange(int x0, int64_t y0, int x1, int64_t y1) const;
    // Imlecin bulundugu mantiksal satirin (sarmalanan onceki satirlar dahil) metni.
    std::string CursorLineText() const;
    // Ekranin gorunur alt kismindaki son N satiri UTF-8 olarak getirir (Ajan tespiti icin)
    std::vector<std::string> GetTailLines(int maxLines = 15) const;

    // ---- Baglantilar (OSC 8) -------------------------------------------
    void        SetCurrentHyperlink(const std::string& url);
    std::string GetHyperlink(uint16_t id) const;
    std::string GetLinkAt(int col, int viewRow) const;
    uint16_t    GetLinkIdAt(int col, int viewRow) const;

    // ---- Semantik Kabuk (OSC 133 / FinalTerm) ---------------------------
    void        HandleOsc133(const std::string& body);
    int         LastExitCode() const        { return m_lastExitCode; }
    bool        LastCommandFailed() const   { return m_lastCommandFailed; }
    void        DismissCommandFailure()     { m_lastCommandFailed = false; Touch(); }
    std::string GetLastFailedCommandOutput() const;
    void        AddCommandMark(int64_t absRow);
    bool        JumpToPreviousCommand();
    bool        JumpToNextCommand();
    const std::vector<int64_t>& CommandMarks() const { return m_commandMarks; }

    uint64_t Revision() const { return m_revision; }
    void     Touch()          { ++m_revision; }

    std::wstring title;
    std::wstring cwd;

private:
    std::vector<Cell>&       buf()       { return m_useAlt ? m_alt : m_primary; }
    const std::vector<Cell>& buf() const { return m_useAlt ? m_alt : m_primary; }
    std::vector<uint8_t>&       wraps()       { return m_useAlt ? m_wrapAlt : m_wrapPrimary; }
    const std::vector<uint8_t>& wraps() const { return m_useAlt ? m_wrapAlt : m_wrapPrimary; }

    Cell*       row(int y)       { return buf().data() + (size_t)y * m_cols; }
    const Cell* row(int y) const { return buf().data() + (size_t)y * m_cols; }

    void ClearRow(int y);
    void CopyRow(int dst, int src);                 // hucreler + sarma bayragi
    void PushToScrollback(int y);
    void PushHistory(const Cell* r, int cols, bool wrapped);
    void DoLineFeed(bool wrapped);
    const Cell* AbsRow(int64_t abs, int& len, bool& wrapped) const;
    int  TopMargin() const    { return m_scrollTop; }
    int  BottomMargin() const { return m_scrollBottom; }

    int m_cols = 0;
    int m_rows = 0;

    // Satir otomatik kaydirmayla bittiyse wrapped=1: kopyalamada alttaki
    // satirla yeni satirsiz birlesir.
    struct SbRow {
        std::vector<Cell> cells;
        bool wrapped = false;
    };

    std::vector<Cell> m_primary;
    std::vector<Cell> m_alt;
    std::vector<uint8_t> m_wrapPrimary;   // izgara satiri basina sarma bayragi
    std::vector<uint8_t> m_wrapAlt;
    std::deque<SbRow> m_scrollback;
    int64_t m_trimmed = 0;                // scrollback basindan atilan satir sayisi

    Pen m_pen;
    int m_cx = 0, m_cy = 0;
    bool m_pendingWrap = false;

    struct SavedCursor { int x = 0, y = 0; Pen pen; bool wrap = false; } m_saved, m_savedAlt;

    int  m_scrollTop = 0;
    int  m_scrollBottom = 0;
    bool m_autoWrap = true;
    bool m_originMode = false;
    bool m_useAlt = false;
    bool m_cursorVisible = true;
    bool m_bracketedPaste = false;
    bool m_appCursorKeys = false;
    bool m_appKeypad = false;
    int  m_mouseMode = 0;
    bool m_mouseSgr = false;

    std::vector<bool> m_tabStops;
    int m_viewOffset = 0;
    int m_shrinkDropped = 0;
    uint64_t m_revision = 1;

    // OSC 8 baglanti tablosu
    std::vector<std::string> m_hyperlinks;
    uint16_t m_currentLinkId = 0;

    // OSC 133 semantik komut durumu
    int     m_lastExitCode = 0;
    bool    m_lastCommandFailed = false;
    bool    m_inCommandExecution = false;
    int64_t m_commandStartRow = 0;
    int64_t m_commandEndRow = 0;
    std::vector<int64_t> m_commandMarks;
};

} // namespace ft
