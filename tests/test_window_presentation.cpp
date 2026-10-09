#include "ui/MainWindowInternal.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>

namespace ft {
class WindowPresentationTest {
    static inline std::wstring copied;
    static bool Copy(HWND, const std::wstring& value) { copied = value; return true; }
    static uint32_t Pixel(Renderer& renderer, int x, int y) {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer, staging;
        assert(SUCCEEDED(renderer.m_swap->GetBuffer(0, IID_PPV_ARGS(&buffer))));
        D3D11_TEXTURE2D_DESC desc{}; buffer->GetDesc(&desc);
        assert(x >= 0 && y >= 0 && x < (int)desc.Width && y < (int)desc.Height);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        assert(SUCCEEDED(renderer.m_d3d->CreateTexture2D(&desc, nullptr, &staging)));
        renderer.m_d3dCtx->CopyResource(staging.Get(), buffer.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        assert(SUCCEEDED(renderer.m_d3dCtx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)));
        uint32_t pixel; memcpy(&pixel, (char*)map.pData + y * map.RowPitch + x * 4, 4);
        renderer.m_d3dCtx->Unmap(staging.Get(), 0);
        return pixel;
    }
    static void Settle(MainWindow& w) {
        w.UpdateQuakeAnimation();
        w.m_gridResizePending = false; w.SyncGridToArea();
        w.ClearToast(); w.Render(); w.Render();
    }
    static void CheckAlpha(MainWindow& w) {
        const auto& area = w.m_lay.term;
        const auto& fm = w.m_r.Metrics();
        auto alpha = [&](int row) { return Pixel(w.m_r, (int)(area.left + fm.cellW * 2.5f),
                                                       (int)(area.top + fm.cellH * (row + .5f))) >> 24; };
        const int expected = (int)std::lround(w.m_cfg.opacity * 255);
        assert(std::abs((int)alpha(1) - expected) <= 1); // explicit application background
        assert(std::abs((int)alpha(3) - expected) <= 1); // default background
    }
    static void SelectThemeThroughSettings(MainWindow& w, int index) {
        w.m_view = MainWindow::View::Settings;
        w.m_settingsTab = 0;
        const float scale = w.m_lay.scale;
        w.m_mainScroll = std::floor(250 * scale);
        w.Render(); // includes the same clipping/scroll calculation as the real UI
        const float left = w.m_lay.main.left + std::floor(190 * scale);
        const float right = std::min(w.m_lay.main.right - std::floor(22 * scale),
                                     w.m_lay.main.left + std::floor(682 * scale));
        const float pad = std::floor(2 * scale);
        float width = right - left;
        const bool second = index >= 5;
        if (second) width = std::floor(width * 4.f / 5.f);
        const float x = left + pad + (width - 2 * pad) / (second ? 4.f : 5.f) * ((second ? index-5 : index) + .5f);
        const float y = w.m_lay.main.top + std::floor(502 * scale) - w.m_mainScroll +
                        (second ? std::floor(38 * scale) : 0) + std::floor(17 * scale);
        const LPARAM point = MAKELPARAM((int)x, (int)y);
        w.WndProc(WM_LBUTTONDOWN, MK_LBUTTON, point); w.Render();
        w.WndProc(WM_LBUTTONUP, 0, point); w.Render();
        if (w.m_cfg.colorScheme != index) {
            w.SaveScreenshot(L"theme-click-failure.png");
            std::cerr << "Theme click failed at " << x << "," << y << " expected=" << index << " actual=" << w.m_cfg.colorScheme << "\n";
        }
        assert(w.m_cfg.colorScheme == index);
        Settings saved; saved.Load(w.m_dataDir); assert(saved.colorScheme == index);
        if (w.m_dpi == 96 && index == 2) {
            w.Render(); w.Render(); // populate both flip-chain buffers after the click
            assert(w.SaveScreenshot(L"theme-settings.png"));
        }
        w.m_view = MainWindow::View::Terminal;
        w.Render(); w.Render();
        const auto& area = w.m_lay.term; const auto& fm = w.m_r.Metrics();
        const auto pixel = Pixel(w.m_r, (int)(area.left + fm.cellW * 2.5f), (int)(area.top + fm.cellH * 3.5f));
        const uint32_t bg = index < 8 ? theme::Schemes[index].bg : w.m_cfg.customBgColor;
        for (int shift : {0,8,16})
            assert(std::abs(int((pixel >> shift) & 255) - int(std::lround(((bg >> shift) & 255) * w.m_cfg.opacity))) <= 1);
    }
    static void WordTests() {
        Screen s; s.Resize(12, 5); VtParser p(s);
        const std::string text = "abc/def-ghijkl xyz\r\n\xC3\xA7\xE4\xB8\xAD! Z";
        p.Feed(text.data(), text.size());
        int x0, x1; int64_t y0, y1;
        assert(s.WordRangeAt(1, 1, x0, y0, x1, y1));
        assert(s.TextInAbsRange(x0,y0,x1,y1) == "abc/def-ghijkl"); // soft wrap, punctuation kept
        assert(s.WordRangeAt(2, 2, x0, y0, x1, y1)); // second cell of a CJK glyph
        assert(s.TextInAbsRange(x0,y0,x1,y1) == "\xC3\xA7\xE4\xB8\xAD!");
        assert(!s.WordRangeAt(4, 2, x0,y0,x1,y1));
        assert(s.WordRangeAt(5, 2, x0,y0,x1,y1));
        assert(x0 == x1 && s.TextInAbsRange(x0,y0,x1,y1) == "Z");
        // Retained history is selectable, hard newlines still delimit tokens.
        const std::string lines = "\r\nrow3\r\nrow4\r\nrow5";
        p.Feed(lines.data(), lines.size()); s.ScrollViewLines(1);
        assert(s.WordRangeAt(1,1,x0,y0,x1,y1));
        assert(s.TextInAbsRange(x0,y0,x1,y1) == "abc/def-ghijkl");
    }
    static bool HasColor(Renderer& renderer, D2D1_RECT_F area, uint32_t rgb) {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer, staging;
        assert(SUCCEEDED(renderer.m_swap->GetBuffer(0, IID_PPV_ARGS(&buffer))));
        D3D11_TEXTURE2D_DESC desc{}; buffer->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        assert(SUCCEEDED(renderer.m_d3d->CreateTexture2D(&desc, nullptr, &staging)));
        renderer.m_d3dCtx->CopyResource(staging.Get(), buffer.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        assert(SUCCEEDED(renderer.m_d3dCtx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)));
        bool found = false;
        for (int y=(int)area.top; y<std::min((int)desc.Height,(int)area.bottom); ++y)
            for (int x=(int)area.left; x<std::min((int)desc.Width,(int)area.right); ++x) {
                uint32_t px; memcpy(&px, (char*)map.pData+y*map.RowPitch+x*4, 4);
                int delta = 0;
                for (int shift : {0,8,16}) delta += std::abs((int)((px>>shift)&255)-(int)((rgb>>shift)&255));
                if (delta < 25) found = true;
            }
        renderer.m_d3dCtx->Unmap(staging.Get(), 0);
        return found;
    }
    static void SessionNamesAndColors(MainWindow& w) {
        w.m_tabs.clear(); w.m_tabLayouts.clear(); w.m_labelCache.clear();
        w.m_view = MainWindow::View::Terminal; w.m_cfg.opacity = 1.f; w.m_sidebarOpen = true;
        Host host; host.label = L"Raspberry Pi"; host.address = L"fixture.invalid";
        auto make = [&] {
            auto t = std::make_shared<TerminalTab>(); t->SetSshSession(host); t->SetSshStage(SshStage::Connected);
            return t;
        };
        auto first=make(), second=make(), third=make(), split=make();
        for (const auto& tab : {first, second, third}) {
            w.m_tabs.push_back(tab); w.m_tabLayouts.push_back(std::make_unique<PaneLayout>(tab));
        }
        w.m_tabLayouts[2]->SplitActive(SplitDirection::Vertical, split);
        w.ComputeLayout();
        assert(first->Title()==L"Raspberry Pi" && second->Title()==L"Raspberry Pi 1");
        assert(third->Title()==L"Raspberry Pi 2" && split->Title()==L"Raspberry Pi 3");
        second->SetCustomTitle(L"Production");
        const std::string text = "\x1b]0;C:\\Windows\\System32\\OpenSSH\\ssh.exe\x07"
            "Welcome to Ubuntu 24.04.4 LTS\r\n\r\n"
            " * Documentation: https://help.ubuntu.com\r\n\r\n"
            "System information\r\nSystem load: 0.44   Temperature: 51.4 C\r\n"
            "Memory usage: 50%   Processes: 256\r\n\r\n"
            "*** System restart required ***\r\n\r\n"
            "user@raspberrypi5:~$ ";
        for (size_t i=0; i<2; ++i) {
            w.m_active=i; Settle(w);
            VtParser parser(w.m_tabs[i]->screen()); parser.Feed(text.data(),text.size());
        }
        assert(first->Title()==L"Raspberry Pi" && first->screen().title.find(L"ssh.exe")!=std::wstring::npos);
        w.m_cfg.restoreSessions=true; assert(w.SaveSession());
        std::ifstream saved(std::filesystem::path(w.m_dataDir)/"session.json");
        const std::string json((std::istreambuf_iterator<char>(saved)),{});
        assert(json.find("Raspberry Pi 3")!=std::string::npos && json.find("autoTitle")!=std::string::npos);
        assert(json.find("Production")!=std::string::npos);
        // Reorder and reset a manual title without renumbering any automatic title.
        std::swap(w.m_tabs[0],w.m_tabs[1]); std::swap(w.m_tabLayouts[0],w.m_tabLayouts[1]);
        w.EnsureSessionTitles(); assert(second->Title()==L"Production");
        second->SetCustomTitle(L""); assert(second->Title()==L"Raspberry Pi 1");
        std::swap(w.m_tabs[0],w.m_tabs[1]); std::swap(w.m_tabLayouts[0],w.m_tabLayouts[1]);
        for (int scheme : {0,2,5,6}) {
            w.m_cfg.colorScheme=scheme; w.ApplyColorScheme();
            for (size_t i=0; i<2; ++i) {
                w.m_active=i; Settle(w);
                const auto before = w.m_tabs[i]->screen().TextInRange(0,0,79,10);
                const auto& fm=w.m_r.Metrics(); const auto& a=w.m_lay.term;
                const auto prompt = D2D1::RectF(a.left,a.top+10*fm.cellH,a.left+4*fm.cellW,a.top+11*fm.cellH);
                assert(HasColor(w.m_r,prompt,theme::Ansi[2]));
                w.m_cfg.highlightPlainText=false; Settle(w);
                assert(HasColor(w.m_r,prompt,theme::TermFg));
                w.m_cfg.highlightPlainText=true; Settle(w);
                assert(w.m_tabs[i]->screen().TextInRange(0,0,79,10)==before);
            }
        }
        // Fullscreen TUIs keep their default colors; highlights are never applied
        // in the alternate screen. Selection also keeps the terminal's own colors.
        w.m_active=0;
        VtParser alt(first->screen());
        const std::string enterAlt="\x1b[?1049h\x1b[Huser@host:~$ "; alt.Feed(enterAlt.data(),enterAlt.size());
        Settle(w);
        const auto& a=w.m_lay.term; const auto& fm=w.m_r.Metrics();
        assert(HasColor(w.m_r,D2D1::RectF(a.left,a.top,a.left+4*fm.cellW,a.top+fm.cellH),theme::TermFg));
        const std::string leaveAlt="\x1b[?1049l"; alt.Feed(leaveAlt.data(),leaveAlt.size());
        w.m_cfg.colorScheme=2; w.ApplyColorScheme(); w.m_active=0; Settle(w);
        ShowWindow(w.m_hwnd,SW_RESTORE);
        SetWindowPos(w.m_hwnd,nullptr,120,100,1280,820,SWP_NOZORDER|SWP_NOACTIVATE);
        Settle(w);
        assert(w.SaveScreenshot(L"ssh-theme-and-names.png"));
        std::cout << "PASS: Hub names/suffixes/manual overrides/persisted split names; uncolored SSH prompt pixels in 4 themes, 2 sessions\n";
    }
    static void SftpMouseNavigation(MainWindow& w) {
        const auto parent = std::filesystem::absolute("ui-sftp-fixture");
        std::filesystem::create_directories(parent / "child");
        w.m_sftp = std::make_unique<SftpController>(w.m_inv);
        w.m_sftp->SetLocalPath(parent.wstring());
        w.m_view = MainWindow::View::Sftp; w.Render();
        const int hitX = (int)w.m_lay.main.left + 90, hitY = (int)w.m_lay.main.top + 132 + 28 + 14;
        assert(w.SftpItemAtPoint(w.m_sftp.get(), false, hitX, hitY) == L"child");
        assert(w.SftpItemAtPoint(w.m_sftp.get(), false, hitX, (int)w.m_lay.main.top).empty());
        auto doubleClick = [&](int row) {
            const auto p = MAKELPARAM((int)(w.m_lay.main.left + 90), (int)(w.m_lay.main.top + 132 + row*28 + 14));
            w.WndProc(WM_LBUTTONDOWN, MK_LBUTTON, p); w.Render();
            w.WndProc(WM_LBUTTONUP, 0, p); w.Render();
            w.WndProc(WM_LBUTTONDBLCLK, MK_LBUTTON, p); w.Render();
            w.WndProc(WM_LBUTTONUP, 0, p); w.Render();
        };
        doubleClick(1); // '..', then child
        assert(std::filesystem::path(w.m_sftp->LocalPath()) == parent / "child");
        doubleClick(0);
        assert(std::filesystem::path(w.m_sftp->LocalPath()) == parent); // exactly one parent, never two
        w.m_view = MainWindow::View::Terminal;
    }
public:
    static void Run() {
        WordTests();
        MainWindow w;
        w.m_dataDir = PortableDataDir();
        w.m_cfg.opacity = .5f; w.m_cfg.colorScheme = 2;
        w.m_cfg.quakeAnimDurationMs = 0;
        w.m_cfg.quakeHotkeyVk = VK_F24;
        w.m_cfg.quakeHotkeyMods = MOD_CONTROL | MOD_ALT | MOD_SHIFT;
        w.m_clipboardWrite = &Copy; // Exercise UI copy dispatch without touching the user's clipboard.
        w.m_dpi = 96; w.m_sidebarOpen = false; w.m_focused = true;
        w.m_inst = GetModuleHandleW(nullptr);
        WNDCLASSW cls{}; cls.hInstance = w.m_inst;
        cls.lpszClassName = L"FullTerminal.PresentationRegression";
        cls.style = CS_DBLCLKS; cls.lpfnWndProc = MainWindow::WndProcStatic;
        assert(RegisterClassW(&cls));
        assert(CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, cls.lpszClassName, L"FullTerminal regression",
            WS_OVERLAPPEDWINDOW, 100, 100, 960, 640, nullptr, nullptr, w.m_inst, &w));
        w.m_ui.SetOwner(w.m_hwnd);
        std::wstring err; assert(w.m_r.Init(w.m_hwnd, 96, &err));
        EnableWindowTransparency(w.m_hwnd);
        auto tab = std::make_shared<TerminalTab>();
        w.m_tabs.push_back(tab); w.m_tabLayouts.push_back(std::make_unique<PaneLayout>(tab));
        w.m_active = 0; w.m_view = MainWindow::View::Terminal;
        w.ApplyColorScheme(); w.OnResize(); Settle(w);
        VtParser parser(tab->screen());
        const std::string content = "\x1b[32mword/with-punctuation X\r\n\x1b[48;2;40;50;60m      \x1b[0m\r\n\x1b[?25l";
        parser.Feed(content.data(), content.size());
        const auto fg = tab->screen().pen().fg;
        const uint32_t schemeBg = theme::TermBg;
        Settle(w); CheckAlpha(w);
        assert(!tab->screen().CursorVisible());
        // Normal/maximized/Visor/full-height/normal all use the same surface alpha.
        for (int round = 0; round < 2; ++round) {
            ShowWindow(w.m_hwnd, SW_MAXIMIZE); Settle(w); CheckAlpha(w);
            w.EnterQuakeMode(); Settle(w); CheckAlpha(w);
            w.m_quakeFull = true; w.QuakeReapply(); Settle(w); CheckAlpha(w);
            w.ExitQuakeMode(); Settle(w); CheckAlpha(w);
            ShowWindow(w.m_hwnd, SW_RESTORE); Settle(w); CheckAlpha(w);
            assert(theme::TermBg == schemeBg && tab->screen().pen().fg == fg);
            assert(!tab->screen().CursorVisible());
        }
        for (float opacity : {.30f, .85f, 1.f}) {
            w.m_cfg.opacity = opacity; Settle(w); CheckAlpha(w);
        }
        w.m_cfg.opacity = .5f;
        // Select through actual mouse-down/render/mouse-up/render, not by calling ApplyColorScheme.
        for (UINT dpi : {96u, 144u}) {
            w.m_dpi = dpi; w.m_r.SetDpi(dpi); w.OnResize(); Settle(w);
            for (int scheme : {0, 2, 5, 7, 1, 6, 3, 4}) SelectThemeThroughSettings(w, scheme);
        }
        w.m_dpi = 96; w.m_r.SetDpi(96); w.OnResize(); Settle(w);
        auto point = [&](int col, int row) {
            const auto& fm = w.m_r.Metrics();
            return MAKELPARAM((int)(w.m_lay.term.left + (col+.5f)*fm.cellW),
                              (int)(w.m_lay.term.top + (row+.5f)*fm.cellH));
        };
        w.WndProc(WM_LBUTTONDBLCLK, MK_LBUTTON, point(4,0));
        assert(copied == L"word/with-punctuation" && w.m_sel.active);
        w.WndProc(WM_LBUTTONUP, 0, point(4,0));
        assert(w.m_sel.active);
        w.WndProc(WM_LBUTTONDBLCLK, MK_LBUTTON, point(22,0));
        assert(copied == L"X" && w.m_sel.x0 == w.m_sel.x1);
        w.WndProc(WM_LBUTTONUP, 0, point(22,0)); assert(w.m_sel.active);
        w.WndProc(WM_LBUTTONDBLCLK, MK_LBUTTON, point(21,0));
        assert(!w.m_sel.active && copied == L"X");
        w.ClearSelection();
        // A steady block cursor remains solid during the blink-off phase.
        tab->screen().SetCursorVisible(true); tab->screen().SetCursor(8,3);
        w.m_cfg.cursorStyle = 2; w.m_cursorOn = false; w.m_focused = true;
        Settle(w);
        const auto& fm = w.m_r.Metrics();
        assert((Pixel(w.m_r, (int)(w.m_lay.term.left+8.5f*fm.cellW),
                            (int)(w.m_lay.term.top+3.5f*fm.cellH)) >> 24) == 255);
        w.m_cursorOn = false; w.WndProc(WM_SETFOCUS, 0, 0); assert(w.m_cursorOn);
        SftpMouseNavigation(w); Settle(w);
        assert(w.SaveScreenshot(L"window-presentation.png"));
        SessionNamesAndColors(w);
        DestroyWindow(w.m_hwnd);
        std::cout << "PASS: real window modes, GPU alpha, cursor, word selection/copy dispatch, Unicode/wrap/history\n";
    }
};
}
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ft::WindowPresentationTest::Run();
    CoUninitialize();
}
