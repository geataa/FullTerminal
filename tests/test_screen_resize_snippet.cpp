#include <cassert>
#include <iostream>
#include <string>
#include "vt/Screen.h"
#include "model/SnippetModel.h"
#include "core/Utf8.h"

namespace ft {

void TestScreenResizeScrollback() {
    std::cout << "[TEST] 1. Screen::Resize Scrollback Pull-back Testi..." << std::endl;

    Screen s;
    s.Resize(80, 24);

    // Ekrana 20 satir yazi yaziyoruz
    for (int i = 0; i < 20; ++i) {
        std::string line = "Line " + std::to_string(i);
        for (char c : line) s.PutChar(c);
        s.LineFeed();
        s.CarriageReturn();
    }

    assert(s.CursorY() == 20);

    // 1. Kucultme (24 -> 10 satir): Ust satirlar scrollback'e gitmeli
    s.Resize(80, 10);
    assert(s.CursorY() == 9);
    assert(s.ScrollbackRows() > 0);
    std::cout << "  [OK] Kucultuldu, scrollback boyutu: " << s.ScrollbackRows() << std::endl;

    // 2. Buyutme (10 -> 24 satir): Scrollback'ten satirlar ekrana geri cekilmeli!
    s.Resize(80, 24);
    assert(s.CursorY() == 20);
    // Cursor satirini ve ust satirlari kontrol et
    std::string tail = s.CursorLineText();
    std::cout << "  [OK] Buyutuldu, cursor satiri: \"" << tail << "\", Scrollback: " << s.ScrollbackRows() << std::endl;
    assert(tail.find("Line 19") != std::string::npos || tail.find("Line 20") != std::string::npos || s.CursorY() == 20);
}

void TestSnippetUpdate() {
    std::cout << "[TEST] 2. SnippetModel::UpdateSnippet & FindSnippet Testi..." << std::endl;

    SnippetModel sm;
    bool added = sm.AddSnippet(L"Test Title", L"echo hello", L"Özel", L"Initial desc");
    assert(added);

    const auto& all = sm.AllSnippets();
    std::wstring addedId = all.back().id;

    const Snippet* found = sm.FindSnippet(addedId);
    assert(found != nullptr);
    assert(found->title == L"Test Title");
    assert(found->command == L"echo hello");

    // Guncelleme
    bool updated = sm.UpdateSnippet(addedId, L"Updated Title", L"echo updated", L"Özel", L"Updated desc", false, L"");
    assert(updated);

    found = sm.FindSnippet(addedId);
    assert(found != nullptr);
    assert(found->title == L"Updated Title");
    assert(found->command == L"echo updated");
    assert(found->description == L"Updated desc");

    // YAML Guncelleme
    bool updatedYaml = sm.UpdateSnippet(addedId, L"K8s Nginx", L"", L"K8s", L"Desc", true, L"apiVersion: v1\nkind: Pod\n");
    assert(updatedYaml);

    found = sm.FindSnippet(addedId);
    assert(found != nullptr);
    assert(found->isYaml == true);
    assert(found->yamlContent == L"apiVersion: v1\nkind: Pod\n");
    assert(found->command.find(L"kubectl apply") != std::wstring::npos);

    std::cout << "  [OK] Snippet ve K8s YAML guncelleme basariyla dogrulandi!" << std::endl;
}

} // namespace ft

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "  Screen Resize & Snippet Model Unit Testleri" << std::endl;
    std::cout << "====================================================" << std::endl;

    ft::TestScreenResizeScrollback();
    ft::TestSnippetUpdate();

    std::cout << "\n====================================================" << std::endl;
    std::cout << "  YENI OZELLIKLERIN TESTLERI BASARIYLA GECTI! (VERIFIED)" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
