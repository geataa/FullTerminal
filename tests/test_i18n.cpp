#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include "core/I18n.h"

using namespace ft;

int main() {
    _setmode(_fileno(stdout), _O_U8TEXT);
    std::wcout << L"====================================================\n";
    std::wcout << L"  FullTerminal - i18n & 16 Dil Destegi Testi\n";
    std::wcout << L"====================================================\n";

    // 1. Dil Sayisi ve Varlik Dogrulamasi
    std::wcout << L"[TEST] 1. Dil Listesi ve Sayisi Kontrol Ediliyor...\n";
    const auto& langs = I18n::Languages();
    assert(langs.size() == 16);
    std::wcout << L"  [OK] Tam 16 dil tanimli.\n";

    // 2. Rusca ve Ukraynaca Kesinlikle Var Mi?
    std::wcout << L"[TEST] 2. Rusca (ru) ve Ukraynaca (uk) Varligi Kontrol Ediliyor...\n";
    bool haveRu = false;
    bool haveUk = false;
    bool haveHe = false;
    for (const auto& l : langs) {
        if (std::wstring(l.code) == L"ru") haveRu = true;
        if (std::wstring(l.code) == L"uk") haveUk = true;
        if (std::wstring(l.code) == L"he" || std::wstring(l.code) == L"iw") haveHe = true;
        if (std::wstring(l.name).find(L"עברית") != std::wstring::npos ||
            std::wstring(l.name).find(L"Hebrew") != std::wstring::npos) {
            haveHe = true;
        }
    }
    assert(haveRu && "Rusca (ru) bulunamadi!");
    assert(haveUk && "Ukraynaca (uk) bulunamadi!");
    assert(!haveHe && "Ibranice (he/iw) KESINLIKLE OLMAMALI kurali ihlal edildi!");
    std::wcout << L"  [OK] Rusca ve Ukraynaca basariyla dogrulandi.\n";
    std::wcout << L"  [OK] Ibranice kesinlikle bulunmuyor (kural saglandi).\n";

    // 3. Varsayilan Dil: Ingilizce
    std::wcout << L"[TEST] 3. Varsayilan Dil Kontrol Ediliyor...\n";
    I18n::Init(); // default
    assert(I18n::CurrentLang() == LangId::En);
    assert(std::wstring(I18n::CurrentCode()) == L"en");
    assert(std::wstring(Tr(Msg::NavTerminal)) == L"Terminal");
    assert(std::wstring(Tr(Msg::NavSettings)) == L"Settings");
    std::wcout << L"  [OK] Varsayilan secili dil: Ingilizce (en).\n";

    // 4. Rusca ve Ukraynaca Metin Kontrolu
    std::wcout << L"[TEST] 4. Rusca ve Ukraynaca Ceviri Dogrulamasi...\n";
    I18n::SetLanguage(LangId::Ru);
    assert(std::wstring(Tr(Msg::NavTerminal)) == L"Терминал");
    assert(std::wstring(Tr(Msg::NavSettings)) == L"Настройки");
    assert(std::wstring(Tr(Msg::ActionConnect)) == L"Подключиться");
    std::wcout << L"  [OK] Rusca ceviriler: " << Tr(Msg::NavTerminal) << L", " << Tr(Msg::NavSettings) << L"\n";

    I18n::SetLanguage(LangId::Uk);
    assert(std::wstring(Tr(Msg::NavTerminal)) == L"Термінал");
    assert(std::wstring(Tr(Msg::NavSettings)) == L"Налаштування");
    assert(std::wstring(Tr(Msg::ActionConnect)) == L"Підключитися");
    std::wcout << L"  [OK] Ukraynaca ceviriler: " << Tr(Msg::NavTerminal) << L", " << Tr(Msg::NavSettings) << L"\n";

    // 5. 16 Dilin Hepsi Icin Bos Olmayan Dizge Testi
    std::wcout << L"[TEST] 5. 16 Dilin Tum Anahtarlari Bos Mu Kontrol Ediliyor...\n";
    for (const auto& l : langs) {
        I18n::SetLanguage(l.id);
        for (int k = 0; k < (int)Msg::_Count; ++k) {
            const wchar_t* val = Tr(static_cast<Msg>(k));
            assert(val != nullptr && val[0] != L'\0');
        }
    }
    std::wcout << L"  [OK] 16 dil x " << (int)Msg::_Count << L" anahtarin hicbiri bos degil, eksiksiz!\n";

    // 6. Hizli Dil Dongusu (CycleLanguage)
    std::wcout << L"[TEST] 6. CycleLanguage (1-Tikla Dil Gecisi) Kontrol Ediliyor...\n";
    I18n::SetLanguage(LangId::En);
    I18n::CycleLanguage();
    assert(I18n::CurrentLang() == LangId::Tr);
    I18n::CycleLanguage();
    assert(I18n::CurrentLang() == LangId::Ru);
    I18n::CycleLanguage();
    assert(I18n::CurrentLang() == LangId::Uk);
    std::wcout << L"  [OK] CycleLanguage basariyla sonraki dillere gecis sagladi.\n";

    std::wcout << L"\n====================================================\n";
    std::wcout << L"  TUM I18N DIL TESTLERI BASARIYLA GECTI! (VERIFIED)\n";
    std::wcout << L"====================================================\n";
    return 0;
}
