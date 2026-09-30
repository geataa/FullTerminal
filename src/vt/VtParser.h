#pragma once
//
// DEC ANSI durum makinesi (Paul Williams'in serimine gore).
// Bilinmeyen diziler sessizce yutulur; ayristirici asla cikmaz.
//
#include "vt/Screen.h"

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace ft {

class VtParser {
public:
    explicit VtParser(Screen& screen) : m_s(screen) {}

    void Feed(const char* data, size_t len);
    void Reset();

    // Terminalin sunucuya geri yazmasi gereken yanitlar (DA, CPR, ...).
    std::function<void(const std::string&)> onReply;
    std::function<void()>                   onBell;
    std::function<void()>                   onTitle;

    int CursorStyle() const { return m_cursorStyle; } // DECSCUSR 0..6

private:
    enum class State {
        Ground, Esc, EscIntermediate,
        CsiEntry, CsiParam, CsiIntermediate, CsiIgnore,
        OscString, DcsIgnore, StringIgnore,
    };

    void Put(char32_t cp);
    void Execute(uint8_t c);
    void CsiDispatch(uint8_t final);
    void EscDispatch(uint8_t final);
    void OscDispatch();
    void ApplySgr();
    void SetMode(bool priv, bool enable);

    int  Param(size_t i, int fallback) const;
    bool IsSubParam(size_t i) const { return i < 32 && ((m_subMask >> i) & 1u); }
    void ClearParams();

    void ParamDigit(uint8_t d);
    void ParamSeparator(bool colon);
    void ParamFinish();
    void PushParam();

    Screen& m_s;
    State   m_state = State::Ground;

    std::vector<int> m_params;
    uint32_t    m_subMask = 0;       // bit i: m_params[i] ':' ile gelen alt parametre
    bool        m_curIsSub = false;  // toplanan parametreden once ':' vardi
    int         m_curParam = 0;
    bool        m_haveParam = false;
    bool        m_sawParam = false;
    bool        m_private = false;
    std::string m_intermediate;
    std::string m_osc;
    bool        m_stringEsc = false; // OSC/DCS icinde ST'nin ilk yarisi gorulduyse

    // UTF-8 akis cozucusu
    char32_t m_cp = 0;
    int      m_need = 0;

    int m_cursorStyle = 0;
};

} // namespace ft
