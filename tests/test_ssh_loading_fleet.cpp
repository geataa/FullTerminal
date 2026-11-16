#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "ui/TerminalTab.h"
#include "model/Inventory.h"
#include "services/KnownHostsService.h"
#include "core/Utf8.h"

namespace ft {

class SshLoadingTest {
public:
    static void TestTallLoginPrompt() {
        for (int rows : {24, 40, 80}) {
            for (const auto* prompt : {"user@fixture.invalid's password: ",
                    "Are you sure you want to continue connecting (yes/no/[fingerprint])? ",
                    "Enter passphrase for key 'fixture': ", "Verification code: "}) {
                TerminalTab tab; Host host; host.address = L"fixture.invalid"; host.username = L"user";
                tab.SetSshSession(host); tab.Resize(120, rows);
                tab.m_queue = std::string("\x1b[2J\x1b[H") + prompt; tab.Pump();
                assert(tab.GetSshStage() == SshStage::AwaitingInput);
                assert(!tab.ShowSshLoading() && "login prompt at top of a tall terminal must be visible");
                tab.Write("answer\r");
                assert(tab.GetSshStage() == SshStage::Connecting && tab.ShowSshLoading());
                tab.m_queue = "\r\nWelcome to Ubuntu\r\nPlease wait while your environment is prepared...\r\n"; tab.Pump();
                assert(tab.GetSshStage() == SshStage::Connecting && tab.ShowSshLoading());
                tab.m_queue = "user@fixture:~/passwords$ "; tab.Pump();
                assert(tab.GetSshStage() == SshStage::Connected && !tab.ShowSshLoading());
            }
            TerminalTab failed; Host host; host.address = L"fixture.invalid";
            failed.SetSshSession(host); failed.Resize(120, rows);
            failed.m_queue = "ssh: connect to host fixture.invalid port 22: Connection refused\r\n"; failed.Pump();
            assert(failed.GetSshStage() == SshStage::Failed);
        }
        TerminalTab custom; Host host; host.address = L"fixture.invalid"; custom.SetSshSession(host);
        custom.m_queue = "Welcome\r\nLoading tools\r\n"; custom.Pump();
        assert(custom.ShowSshLoading());
        custom.m_queue = "System information:"; custom.Pump();
        assert(custom.ShowSshLoading() && "a partial MOTD heading must not be mistaken for an input prompt");
        custom.RevealSshTerminal();
        assert(!custom.ShowSshLoading() && custom.GetSshStage() == SshStage::Connecting);
        custom.m_queue = "Permission denied (publickey).\r\n"; custom.Pump();
        assert(custom.ShowSshLoading() && custom.GetSshStage() == SshStage::Failed);
    }
    static void TestHashedKnownHostsImport() {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::absolute("hashed-known-hosts-fixture");
        fs::remove_all(dir, ec);
        fs::create_directories(dir);

        {
            std::ofstream kh(dir / "known_hosts", std::ios::binary);
            kh << "|1|MDEyMzQ1Njc4OTAxMjM0NTY3ODk=|BZhkK6YVB9g7cUwZXEZKPcSDk8I= ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIA==\n";
            kh << "|1|YWJjZGVmZ2hpamtsbW5vcHFyc3Q=|qpt17Xe5qnzxqm9ufeTmbZYfNmc= ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIB==\n";
        }
        {
            std::ofstream khold(dir / "known_hosts.old", std::ios::binary);
            khold << "alpha.example.invalid ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIA==\n";
        }
        {
            std::ofstream hist(dir / ".bash_history", std::ios::binary);
            hist << "ssh -p 2222 deploy@beta.example.invalid\n";
        }
        {
            std::ofstream cfg(dir / "config", std::ios::binary);
            cfg << "Host gamma-prod\n  HostName gamma.example.invalid\n  User ubuntu\n  Port 2200\n";
        }

        KnownHostsService khs;
        assert(khs.Load((dir / "known_hosts").wstring()));
        assert(khs.Entries().size() == 2);
        assert(khs.Entries()[0].isHashed && khs.Entries()[0].resolvedHost == L"alpha.example.invalid");
        assert(khs.Entries()[1].isHashed && khs.Entries()[1].resolvedHost == L"[beta.example.invalid]:2222");
        assert(khs.Entries()[1].resolvedUser == L"deploy");

        Inventory inv;
        const size_t n = inv.ImportKnownHostsAndConfig(khs);
        assert(n >= 3);

        bool foundAlpha = false, foundBeta = false, foundGamma = false;
        for (const auto& h : inv.hosts()) {
            if (h.address == L"alpha.example.invalid" && h.port == 22) foundAlpha = true;
            if (h.address == L"beta.example.invalid" && h.port == 2222 && h.username == L"deploy") foundBeta = true;
            if (h.address == L"gamma.example.invalid" && h.port == 2200 && h.username == L"ubuntu") foundGamma = true;
        }
        assert(foundAlpha && foundBeta && foundGamma);

        // Hashed kayit acik sunucu adiyla da silinebilmeli
        assert(khs.DeleteHost(L"alpha.example.invalid"));
        assert(khs.Entries().size() == 1);
        assert(khs.Entries()[0].resolvedHost == L"[beta.example.invalid]:2222");

        fs::remove_all(dir, ec);
        std::cout << "  [OK] Ubuntu HashKnownHosts HMAC-SHA1 cozumleme ve Import dogrulandi." << std::endl;
    }
    static void RunAll() {
        TestTallLoginPrompt();
        TestHashedKnownHostsImport();
        std::cout << "[TEST] 1. SSH Debug Bastirma ve Loading Ekrani Gecis Testi..." << std::endl;

        TerminalTab tab;
        Host h;
        h.id = L"rpi5";
        h.label = L"Raspberry Pi 5 SkyCortex";
        h.address = L"192.168.1.135";
        h.port = 22;
        h.username = L"pi";
        h.kind = AuthKind::Key;

        tab.SetSshSession(h);
        assert(tab.GetSshStage() == SshStage::Connecting);
        std::cout << "  [OK] Baslangicta SshStage::Connecting dogrulandi." << std::endl;

        // Login uses the same VT stream as ConPTY: no invented banner or recoloring.
        Screen reference; VtParser parser(reference);
        auto feed = [&](const std::string& chunk) {
            tab.m_queue = chunk; tab.Pump(); parser.Feed(chunk.data(), chunk.size());
            assert(tab.screen().CursorX() == reference.CursorX());
            assert(tab.screen().CursorY() == reference.CursorY());
            assert(tab.screen().CursorVisible() == reference.CursorVisible());
            std::vector<Cell> actual(tab.screen().Cols()), expected(reference.Cols());
            for (int y = 0; y < reference.Rows(); ++y) {
                tab.screen().CopyViewRow(y, actual.data()); reference.CopyViewRow(y, expected.data());
                for (int x = 0; x < reference.Cols(); ++x)
                    assert(actual[x].ch == expected[x].ch && actual[x].SameStyle(expected[x]));
            }
        };
        feed("\x1b[?25l\x1b[2J\x1b[HWelcome to fixture\r\n\x1b[38;2;");
        assert(tab.ShowSshLoading());
        feed("32;80;120muser@host:~$ ");
        assert(tab.GetSshStage() == SshStage::Connected);
        tab.ApplyTheme(); tab.Resize(140,40); reference.Resize(140,40);
        feed("\x1b[H\x1b[2J\x1b[32mWelcome to fixture\r\nuser@host:~$ \x1b[0m\x1b[?25h");
        feed("\r\ndebug1: this is user data\r\nOpenSSH documentation\r\n");
        Inventory inv;
        assert(inv.BuildSshCommand(h).find(L" -v") == std::wstring::npos);

        // Theme/geometry must not reset the live VT stream or replay an old welcome.
        tab.m_screen.Reset(); tab.m_parser.Reset();
        const std::string fixture = "\x1b[32mLIVE\x1b[?25l\x1b[6 q\x1b[38;2;";
        tab.m_parser.Feed(fixture.data(), fixture.size());
        tab.ClearBeforeThemeApply(); tab.ApplyTheme();
        tab.Resize(120, 40); tab.Resize(80, 24);
        assert(!tab.screen().CursorVisible());
        assert(tab.CursorStyle() == 6);
        assert(tab.screen().CursorX() == 4 && tab.screen().CursorY() == 0);
        tab.m_queue = "10;20;30m-OUTPUT";
        tab.Pump();
        assert(tab.screen().CursorLineText() == "LIVE-OUTPUT");
        assert(!tab.screen().CursorVisible());
        assert(tab.screen().pen().fg == 0x0A141E);
        assert(tab.m_cols == 80 && tab.m_rows == 24);
        tab.Write("x"); // sending input must respect an application's hidden cursor
        assert(!tab.screen().CursorVisible());
        std::cout << "  [OK] Resize/theme preserve live output, split SGR, cursor visibility/style/position.\n";
    }
};

} // namespace ft

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "  FullTerminal - SSH Loading & Fleet Engine Testi" << std::endl;
    std::cout << "====================================================" << std::endl;

    ft::SshLoadingTest::RunAll();

    std::cout << "\n====================================================" << std::endl;
    std::cout << "  TUM TESTLER EKSIKSIZ BASARIYLA GECTI! (VERIFIED)" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
