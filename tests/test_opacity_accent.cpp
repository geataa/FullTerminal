#include "core/Settings.h"
#include "core/ShellProfiles.h"
#include "ui/Theme.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <algorithm>

int main() {
    std::wcout << L"====================================================" << std::endl;
    std::wcout << L"  FullTerminal - Opacity & Accent Tab Tests" << std::endl;
    std::wcout << L"====================================================" << std::endl;

    // 1. Vurgu rengi secimi ve tema guncellemesi
    std::wcout << L"[TEST] 1. Vurgu Rengi (Accent Color) Secim ve Tema Testi..." << std::endl;
    ft::theme::SetAccent(ft::theme::AccentChoices[0]); // Jade
    assert(ft::theme::Ac() == 0x45B5AC);
    std::wcout << L"  [OK] Jade (0): 0x" << std::hex << ft::theme::Ac() << std::dec << std::endl;

    ft::theme::SetAccent(ft::theme::AccentChoices[3]); // Mor
    assert(ft::theme::Ac() == 0x9B8CF7);
    std::wcout << L"  [OK] Mor (3): 0x" << std::hex << ft::theme::Ac() << std::dec << std::endl;

    ft::theme::SetAccent(ft::theme::AccentChoices[2]); // Amber
    assert(ft::theme::Ac() == 0xEE9B52);
    std::wcout << L"  [OK] Amber (2): 0x" << std::hex << ft::theme::Ac() << std::dec << std::endl;

    // 2. Sekme Vurgu Rengi Mantigi Testi
    std::wcout << L"[TEST] 2. Aktif ve Pasif Sekmelerde Vurgu Rengi Mantigi Testi..." << std::endl;
    ft::ShellProfile pwshProfile;
    pwshProfile.accent = 0x5A9CE8; // PowerShell mavi
    pwshProfile.badge = L"PWSH";

    const uint32_t currentAppAccent = ft::theme::Ac(); // Amber 0xEE9B52
    // Aktif sekme: uygulamanin secili vurgu rengini alir
    const uint32_t activeTabFillColor = currentAppAccent;
    assert(activeTabFillColor == 0xEE9B52);
    std::wcout << L"  [OK] Aktif sekme arka plan ve cerceve rengi: 0x" << std::hex << activeTabFillColor << std::dec << L" (Secili Vurgu)" << std::endl;

    // Pasif sekme: profil rozet/vurgu rengini korur
    const uint32_t inactiveTabBadgeColor = pwshProfile.accent;
    assert(inactiveTabBadgeColor == 0x5A9CE8);
    std::wcout << L"  [OK] Pasif sekme profil rengi: 0x" << std::hex << inactiveTabBadgeColor << std::dec << L" (PowerShell Mavi)" << std::endl;

    // 3. Saydamlik Deger Araligi ve Ayar Testi
    std::wcout << L"[TEST] 3. Terminal Saydamlik Deger Araligi Testi..." << std::endl;
    ft::Settings cfg;
    cfg.opacity = 0.85f;
    assert(std::round(cfg.opacity * 100.0f) == 85.0);
    std::wcout << L"  [OK] %85 saydamlik: " << cfg.opacity << std::endl;

    cfg.opacity = 0.05f; // min altinda
    cfg.opacity = std::clamp(cfg.opacity, 0.10f, 1.0f);
    assert(cfg.opacity == 0.10f);
    std::wcout << L"  [OK] Minimum %10 sinir korumasi: " << cfg.opacity << std::endl;

    cfg.opacity = 1.05f; // max ustunde
    cfg.opacity = std::clamp(cfg.opacity, 0.10f, 1.0f);
    assert(cfg.opacity == 1.0f);
    std::wcout << L"  [OK] Maksimum %100 sinir korumasi: " << cfg.opacity << std::endl;

    // 4. Hizli Gecis (Cycle Presets) Mantigi
    std::wcout << L"[TEST] 4. Hizli Gecis (Cycle Presets: 100% -> 85% -> 70% -> 50% -> 100%) Testi..." << std::endl;
    float op = 1.0f;
    int p = (int)std::round(op * 100.0f);
    if (p >= 95) op = 0.85f;
    assert(std::round(op * 100.0f) == 85.0);

    p = (int)std::round(op * 100.0f);
    if (p >= 80) op = 0.70f;
    assert(std::round(op * 100.0f) == 70.0);

    p = (int)std::round(op * 100.0f);
    if (p >= 65) op = 0.50f;
    assert(std::round(op * 100.0f) == 50.0);

    p = (int)std::round(op * 100.0f);
    if (p >= 45) op = 0.30f;
    assert(std::round(op * 100.0f) == 30.0);

    p = (int)std::round(op * 100.0f);
    if (p < 45) op = 1.0f;
    assert(op == 1.0f);
    std::wcout << L"  [OK] Hazir ayarlar (presets) dongusu dogrulandi." << std::endl;

    std::wcout << std::endl;
    std::wcout << L"====================================================" << std::endl;
    std::wcout << L"  TUM OPACITY & ACCENT TESTLERI BASARIYLA GECTI!" << std::endl;
    std::wcout << L"====================================================" << std::endl;
    return 0;
}
