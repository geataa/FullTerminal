#pragma once
//
// Kucuk anlik-mod (immediate mode) widget seti.
// Gety'nin elle cizilmis kontrol yaklasimini genellestirir: her kare
// yeniden cizilir, tiklamalar cizim sirasinda hit-test edilir.
//
#include "render/Renderer.h"

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace ft {

// Pano yardimcilari. Alan duzenleyici ve terminal ayni yolu kullanir.
std::wstring ClipboardGetText(HWND owner);
bool         ClipboardSetText(HWND owner, const std::wstring& text);

// Tus, basildigi andaki degistiricileriyle birlikte kuyruklanir.
// GetKeyState'i cizim aninda okumak yaris yaratirdi.
struct UiKey {
    int  vk = 0;
    bool ctrl = false;
    bool shift = false;
};

struct UiInput {
    float mx = -1.0f, my = -1.0f;
    // Sol tusun basildigi nokta. Tik yalnizca basis da ayni widget'ta
    // basladiysa sayilir; -1 = basis baslik/sekme/terminal tarafindan alindi.
    float px = -1.0f, py = -1.0f;
    bool  down = false;
    bool  clicked = false;      // bu karede tamamlanan tik
    bool  dblClick = false;
    int   wheel = 0;
    std::wstring chars;         // biriken WM_CHAR
    std::vector<UiKey> keys;    // biriken WM_KEYDOWN
};

class Ui {
public:
    explicit Ui(Renderer& r) : m_r(r) {}

    void SetOwner(HWND h) { m_owner = h; }
    void Begin(UiInput& in, float scale);
    void End();

    bool Hot(const D2D1_RECT_F& r) const;
    bool PressIn(const D2D1_RECT_F& r) const;   // basis bu dikdortgende mi basladi
    bool Clicked(const D2D1_RECT_F& r) const;   // Hot + tik + basis ayni yerde
    bool DblClicked(const D2D1_RECT_F& r) const; // Hot + cift tik + basis ayni yerde

    bool Button(int id, const D2D1_RECT_F& r, const std::wstring& label,
                bool primary = false, bool danger = false, bool enabled = true);
    bool Row(int id, const D2D1_RECT_F& r, bool selected, uint32_t stripe = 0);
    bool Field(int id, const D2D1_RECT_F& r, std::wstring& text,
               const std::wstring& placeholder = L"", bool password = false,
               float leftPad = 0.0f);
    bool TextArea(int id, const D2D1_RECT_F& r, std::wstring& text,
                  const std::wstring& placeholder = L"", bool mono = true);
    // Sayi / renk alanlari: odakliyken kalici bir tampon duzenlenir; deger
    // yalnizca metin gecerli ve aralikta ise yazilir. Enter sinirlayip bicimler,
    // odak kaybinda yarim metin atilir.
    bool NumField(int id, const D2D1_RECT_F& r, int& value, int lo, int hi,
                  const std::wstring& placeholder = L"");
    bool HexColorField(int id, const D2D1_RECT_F& r, uint32_t& rgb,
                       const std::wstring& placeholder = L"");
    bool Check(int id, const D2D1_RECT_F& r, bool& value, const std::wstring& label);
    bool Choice(int id, const D2D1_RECT_F& r, const std::vector<std::wstring>& items, int& index);

    void Label(const D2D1_RECT_F& r, const std::wstring& s, uint32_t color, float sizePx,
               bool bold = false, Renderer::Align a = Renderer::Align::Left, bool mono = false);
    void Caption(const D2D1_RECT_F& r, const std::wstring& s);
    void Separator(float x0, float x1, float y);
    void Badge(const D2D1_RECT_F& r, const std::wstring& text, uint32_t color);

    int  Focus() const { return m_focus; }
    void SetFocus(int id);
    void SelectAll() { m_anchor = 0; m_caret = 1000000; }
    bool WantsKeyboard() const { return m_focus != 0; }
    void SetCaretVisible(bool v) { m_caretOn = v; }

    float Scale() const { return m_s; }

private:
    void EditField(std::wstring& text, bool password);
    void EditTextArea(std::wstring& text);
    int  CaretFromX(const std::wstring& shown, float px, float relX);

    Renderer& m_r;
    UiInput*  m_in = nullptr;
    HWND      m_owner = nullptr;
    float     m_s = 1.0f;

    int   m_focus = 0;
    int   m_caret = -1;         // imlec konumu
    int   m_anchor = -1;        // secim capasi; m_caret == m_anchor ise secim yok
    int   m_dragField = 0;      // fare basiliyken suruklenen alanin kimligi
    float m_fieldOffset = 0.0f; // odakli alanin yatay kaydirmasi
    bool  m_caretOn = true;

    int          m_editId = 0;  // NumField/HexColorField tamponunun sahibi
    std::wstring m_editBuf;
    std::unordered_map<int, float> m_areaScrolls;
};

} // namespace ft
