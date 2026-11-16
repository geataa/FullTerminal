#include "transport/daemon/SessionDaemon.h"
#include "transport/daemon/PtySessionClient.h"
#include "ui/TerminalTab.h"
#include "mcp/Json.h"
#include <cassert>
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace ft;
static bool WaitUntil(const std::function<bool()>& check) {
    for (int i = 0; i < 150; ++i) {
        if (check()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}
static int VtFixture() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE), in = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(out, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleMode(in, 0);
    auto emit = [&](const std::string& text) { DWORD n; WriteFile(out, text.data(), (DWORD)text.size(), &n, nullptr); };
    emit("\x1b[2J\x1b[H\x1b[31mRED\x1b[0m \x1b[32mGREEN\x1b[0m\r\n\x1b[48;2;40;50;60mBACKGROUND\x1b[0m\r\nREADY\r\ntester@fixture:~$ ");
    char key; DWORD n = 0;
    while (ReadFile(in, &key, 1, &n, nullptr) && n) {
        CONSOLE_SCREEN_BUFFER_INFO info{}; GetConsoleScreenBufferInfo(out, &info);
        emit("\x1b[6;1H\x1b[2KWIDTH:" + std::to_string(info.srWindow.Right-info.srWindow.Left+1) +
             " HEIGHT:" + std::to_string(info.srWindow.Bottom-info.srWindow.Top+1) + "\x1b[7;1H");
        if (key == 'q') break;
    }
    return 0;
}
static int AuthFixture() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE), in = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(out, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleMode(in, 0);
    auto emit = [&](const std::string& text) { DWORD n; WriteFile(out, text.data(), (DWORD)text.size(), &n, nullptr); };
    emit("\x1b[2J\x1b[Htester@auth-fixture.invalid's password: ");
    std::string answer; char key; DWORD n;
    while (ReadFile(in, &key, 1, &n, nullptr) && n && key != '\r') answer += key;
    if (answer != "fixture-password") return 7;
    emit("\r\nAUTHENTICATED\r\nENVIRONMENT_PENDING\r\n");
    while (ReadFile(in, &key, 1, &n, nullptr) && n && key != 'r') {}
    emit("tester@fixture:~$ ");
    while (ReadFile(in, &key, 1, &n, nullptr) && n && key != 'q') {}
    return 0;
}
static void TestTallAuthenticationAndRetry() {
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
    ShellProfile profile; profile.exe = exe; profile.args = L"--auth-fixture";
    Host host; host.address = L"auth-fixture.invalid"; host.username = L"tester";
    host.password = L"fixture-password"; host.kind = AuthKind::Password;
    TerminalTab tab; tab.SetSshSession(host);
    tab.SetAutoPassword(host.password, host.username, host.address);
    std::wstring error; assert(tab.Start(profile, 120, 80, nullptr, 0, &error));
    auto authenticated = [&] {
        assert(WaitUntil([&] { tab.Pump(); return tab.screen().TextInRange(0,2,79,2) == "ENVIRONMENT_PENDING"; }));
        // Authentication and an arbitrarily delayed MOTD must not dismiss loading.
        assert(tab.GetSshStage() == SshStage::Connecting && tab.ShowSshLoading());
        tab.Write("r");
        assert(WaitUntil([&] { tab.Pump(); return tab.GetSshStage() == SshStage::Connected; }));
    };
    authenticated();
    tab.Write("q"); assert(WaitUntil([&] { tab.Pump(); return tab.Exited(); }));
    assert(tab.RestartSsh(&error));
    authenticated(); // restarting must re-arm the saved target password
    tab.Close();
}
static void TestVtResize(bool powershell = false) {
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
    ShellProfile profile; profile.exe = exe; profile.args = L"--vt-fixture";
    if (powershell) {
        wchar_t system[32768]{}; GetSystemDirectoryW(system, 32768);
        profile.exe = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        auto script = std::filesystem::absolute("resize-fixture.ps1");
        std::ofstream(script) << R"ps($e = [char]27
[Console]::Write("${e}[2J${e}[H${e}[31mRED${e}[0m ${e}[32mGREEN${e}[0m`r`n${e}[48;2;40;50;60mBACKGROUND${e}[0m`r`nREADY`r`ntester@fixture:~$ ")
while ($true) {
    $k = [Console]::ReadKey($true).KeyChar
    [Console]::Write("${e}[6;1H${e}[2KWIDTH:$([Console]::WindowWidth) HEIGHT:$([Console]::WindowHeight)${e}[7;1H")
    if ($k -eq 'q') { break }
}
)ps";
        profile.args = L"-NoLogo -NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() + L"\"";
    }
    TerminalTab tab; std::wstring error;
    Host fixtureHost; fixtureHost.address = L"local-resize-fixture";
    tab.SetSshSession(fixtureHost); // also exercise the SSH observer with real ConPTY repainting
    assert(tab.Start(profile, 80, powershell ? 80 : 24, nullptr, 0, &error));
    assert(WaitUntil([&] { tab.Pump(); return tab.screen().TextInRange(0,2,20,2).find("READY") != std::string::npos; }));
    assert(tab.GetSshStage() == SshStage::Connected);
    std::vector<Cell> original(80); tab.screen().CopyViewRow(0, original.data());
    for (const auto& dims : {std::pair{140,45}, std::pair{90,20}, std::pair{160,50}, std::pair{80,24}}) {
        tab.Resize(dims.first, dims.second); tab.Write("r");
        const std::string expected = "WIDTH:" + std::to_string(dims.first) + " HEIGHT:" + std::to_string(dims.second);
        assert(WaitUntil([&] { tab.Pump(); return tab.screen().TextInRange(0,5,60,5) == expected; }));
        std::vector<Cell> actual(dims.first); tab.screen().CopyViewRow(0, actual.data());
        for (int x = 0; x < 9; ++x) {
            assert(actual[x].ch == original[x].ch);
            assert(actual[x].SameStyle(original[x]));
        }
        assert(tab.screen().CursorX() == 0 && tab.screen().CursorY() == 6);
    }
    tab.Write("q"); assert(WaitUntil([&] { tab.Pump(); return tab.Exited() || tab.Dead(); })); tab.Close();
}
static int TestRealSsh(int port) {
    Host host; host.label=L"Loopback SSH"; host.address=L"127.0.0.1"; host.port=port;
    host.username=L"tester"; host.password=L"fixture-password"; host.kind=AuthKind::Password;
    Inventory inventory; std::wstring err;
    ShellProfile profile; profile.name=host.Display(); profile.rawCommand=inventory.BuildSshCommand(host,&err);
    assert(!profile.rawCommand.empty());
    const auto options = profile.rawCommand.find(L" -o LogLevel=ERROR");
    assert(options!=std::wstring::npos);
    profile.rawCommand.insert(options,L" -F NUL -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL -o GlobalKnownHostsFile=NUL");
    auto exercise = [](TerminalTab& tab) {
        auto contains = [&](const std::string& text) {
            tab.Pump();
            for (const auto& row : tab.screen().GetTailLines(tab.screen().Rows()))
                if (row.find(text)!=std::string::npos) return true;
            return false;
        };
        assert(WaitUntil([&] { return contains("ENVIRONMENT_PENDING"); }));
        assert(tab.GetSshStage()==SshStage::Connecting && tab.ShowSshLoading());
        tab.Write("r");
        assert(WaitUntil([&] { tab.Pump(); return tab.GetSshStage()==SshStage::Connected; }));
        assert(tab.Title()==L"Loopback SSH"); // remote OSC title must not replace the Hub name
        std::vector<Cell> row(tab.screen().Cols()); bool green=false;
        for (int y=0; y<tab.screen().Rows(); ++y) {
            tab.screen().CopyViewRow(y,row.data());
            if (row[0].ch=='G' && row[1].ch=='R')
                green=(row[0].flags & CF_FgIndexed) && row[0].fg==2;
        }
        assert(green);
        for (const auto size : {std::pair{140,40}, std::pair{120,80}}) {
            tab.Resize(size.first,size.second);
            const auto expected="SIZE:"+std::to_string(size.first)+"x"+std::to_string(size.second);
            assert(WaitUntil([&] { tab.Write("s"); return contains(expected); }));
        }
        tab.Write("q"); assert(WaitUntil([&] { tab.Pump(); return tab.Exited(); }));
    };
    TerminalTab tab; tab.SetSshSession(host);
    assert(tab.Start(profile,120,80,nullptr,0,&err));
    assert(WaitUntil([&] { tab.Pump(); return tab.GetSshStage()==SshStage::AwaitingInput; }));
    assert(!tab.ShowSshLoading()); tab.Write("wrong-fixture-password\r");
    assert(WaitUntil([&] { tab.Pump(); return tab.GetSshStage()==SshStage::AwaitingInput; }));
    tab.Write("fixture-password\r"); exercise(tab);
    assert(tab.RestartSsh(&err)); exercise(tab); tab.Close();
    TerminalTab automatic; automatic.SetSshSession(host);
    automatic.SetAutoPassword(host.password,host.username,host.address);
    assert(automatic.Start(profile,120,80,nullptr,0,&err)); exercise(automatic); automatic.Close();
    auto request=json::Value::Object(); json::Value response; request["action"]="stop";
    assert(SessionDaemon::Request(request,response));
    return 0;
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--daemon") return SessionDaemon::RunStandalone();
    if (argc > 1 && std::string(argv[1]) == "--vt-fixture") return VtFixture();
    if (argc > 1 && std::string(argv[1]) == "--auth-fixture") return AuthFixture();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc>2 && std::string(argv[1])=="--ssh-loopback") return TestRealSsh(std::stoi(argv[2]));
    ShellProfile profile; profile.id = L"cmd"; profile.name = L"CMD"; profile.badge = L"CMD";
    wchar_t system[32768]{}; GetSystemDirectoryW(system, 32768);
    profile.exe = std::wstring(system) + L"\\cmd.exe"; profile.args = L"/q";
    profile.env.emplace_back(L"FT_DAEMON_TEST", L"retained-value-731");
    TerminalTab first;
    std::wstring err;
    assert(first.Start(profile, 80, 24, nullptr, 0, &err));
    first.Write("echo %FT_DAEMON_TEST%\r");
    assert(WaitUntil([&] {
        first.Pump();
        for (auto& line : first.screen().GetTailLines(24)) if (line.find("retained-value-731") != std::string::npos) return true;
        return false;
    }));
    auto id = first.SessionId();
    assert(!id.empty());
    first.Write("set FT_DAEMON_TEST=survived-detach-842\r");
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    first.Detach();
    TerminalTab second;
    assert(second.Resume(profile, id, 80, 24, nullptr, 0, &err));
    assert(second.profile().CommandLine() == profile.CommandLine());
    assert(!second.profile().env.empty());
    second.Write("echo %FT_DAEMON_TEST%\r");
    assert(WaitUntil([&] {
        second.Pump();
        for (auto& line : second.screen().GetTailLines(24)) if (line.find("survived-detach-842") != std::string::npos) return true;
        return false;
    }));
    second.Resize(100, 30);
    second.Write("exit /b 7\r");
    assert(WaitUntil([&] { second.Pump(); return second.Dead(); }));
    assert(second.ExitCode() == 7);
    second.Close();
    // Joining a reader that already finished must also be safe.
    PtySessionClient missing; assert(!missing.Attach("../bad", &err));
    json::Value request = json::Value::Object(), response;
    request["action"] = "kill"; request["id"] = id;
    SessionDaemon::Request(request, response);
    // A command that exits before the UI attaches must keep its final output/exit code.
    request["action"] = "create"; request["id"] = "fast-exit-test";
    request["command"] = "cmd.exe /d /c \"echo fast-command-output & exit /b 9\"";
    request["cols"] = 80; request["rows"] = 24;
    assert(SessionDaemon::Request(request, response) && response["success"].as_bool());
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    TerminalTab fast;
    assert(fast.Resume(profile, "fast-exit-test", 80, 24, nullptr, 0, &err));
    assert(WaitUntil([&] { fast.Pump(); return fast.Dead(); }));
    assert(fast.ExitCode() == 9);
    bool sawFinalOutput = false;
    for (const auto& line : fast.screen().GetTailLines(24))
        if (line.find("fast-command-output") != std::string::npos) sawFinalOutput = true;
    assert(sawFinalOutput);
    fast.Close();
    TestVtResize();
    TestVtResize(true);
    TestTallAuthenticationAndRetry();
    request["action"] = "stop";
    assert(SessionDaemon::Request(request, response));
    assert(response["success"].as_bool());
    std::cout << "PASS: separate daemon process, TerminalTab roundtrip, detach/reattach state, resize, exit code, cleanup\n";
}
