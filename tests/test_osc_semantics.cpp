#include "vt/Screen.h"
#include "vt/VtParser.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace ft;

void TestOsc8Hyperlinks() {
    std::cout << "[TEST] 1. OSC 8 Hyperlinks...\n";
    Screen s;
    s.Resize(80, 24);
    VtParser parser(s);

    // 1.1 Emit OSC 8 hyperlink: \x1b]8;;https://example.com\x1b\ClickHere\x1b]8;;\x1b\NormalText
    std::string stream = "\x1b]8;;https://example.com\x1b\\ClickHere\x1b]8;;\x1b\\NormalText";
    parser.Feed(stream.data(), stream.size());

    // Verify 'ClickHere' has linkId != 0 and GetLinkAt returns URL
    for (int col = 0; col < 9; ++col) {
        std::string link = s.GetLinkAt(col, 0);
        uint16_t id = s.GetLinkIdAt(col, 0);
        assert(id != 0);
        assert(link == "https://example.com");
    }

    // Verify 'NormalText' has linkId == 0 and GetLinkAt returns empty
    for (int col = 9; col < 19; ++col) {
        uint16_t id = s.GetLinkIdAt(col, 0);
        assert(id == 0);
    }

    std::cout << "  ✓ Explicit OSC 8 hyperlink parsed and mapped to cells properly.\n";

    // 1.2 Test BEL terminated OSC 8
    std::string stream2 = "\r\n\x1b]8;id=42;https://google.com\a" "Google\x1b]8;;\a";
    parser.Feed(stream2.data(), stream2.size());
    assert(s.GetLinkAt(0, 1) == "https://google.com");
    assert(s.GetLinkIdAt(0, 1) != 0);
    std::cout << "  ✓ BEL-terminated OSC 8 with parameters parsed successfully.\n";
}

void TestPlainTextUrlAutoDetection() {
    std::cout << "[TEST] 2. Plain-Text URL Auto-Detection...\n";
    Screen s;
    s.Resize(80, 24);
    VtParser parser(s);

    std::string line = "Please visit https://github.com/herdrdev/herdr for info!";
    parser.Feed(line.data(), line.size());

    // 'Please visit ' is cols 0..12 -> no link
    assert(s.GetLinkAt(0, 0).empty());
    assert(s.GetLinkAt(5, 0).empty());

    // 'https://github.com/herdrdev/herdr' starts at col 13
    int urlStart = 13;
    int urlLen = (int)std::string("https://github.com/herdrdev/herdr").size();
    for (int col = urlStart; col < urlStart + urlLen; ++col) {
        std::string found = s.GetLinkAt(col, 0);
        assert(found == "https://github.com/herdrdev/herdr");
    }

    // ' for info!' -> no link
    assert(s.GetLinkAt(urlStart + urlLen + 1, 0).empty());

    std::cout << "  ✓ Plain-text URLs correctly detected when clicking without OSC 8 tag.\n";
}

void TestOsc133SemanticShellIntegration() {
    std::cout << "[TEST] 3. OSC 133 Semantic Shell Integration...\n";
    Screen s;
    s.Resize(80, 24);
    VtParser parser(s);

    // Prompt start: \x1b]133;A\a
    std::string promptStart = "\x1b]133;A\a" "user@box:~$ ";
    parser.Feed(promptStart.data(), promptStart.size());
    assert(s.CommandMarks().size() == 1);

    // Command start: \x1b]133;B\a
    std::string cmdStart = "\x1b]133;B\a" "cargo build\r\n";
    parser.Feed(cmdStart.data(), cmdStart.size());

    // Execution start: \x1b]133;C\a
    std::string execStart = "\x1b]133;C\a";
    parser.Feed(execStart.data(), execStart.size());

    // Command output
    std::string output = "Compiling crate v0.1\r\nerror[E0432]: unresolved import `foo`\r\nerror: could not compile\r\n";
    parser.Feed(output.data(), output.size());

    // Command failure: \x1b]133;D;101\a
    std::string cmdFail = "\x1b]133;D;101\a";
    parser.Feed(cmdFail.data(), cmdFail.size());

    assert(s.LastExitCode() == 101);
    assert(s.LastCommandFailed() == true);
    std::string errOut = s.GetLastFailedCommandOutput();
    assert(errOut.find("unresolved import `foo`") != std::string::npos);
    std::cout << "  ✓ Command failure detected: exit code 101, error captured.\n";

    // Dismiss failure
    s.DismissCommandFailure();
    assert(s.LastCommandFailed() == false);
    std::cout << "  ✓ Failure successfully dismissed.\n";

    // Next successful command: \x1b]133;A\a
    std::string nextCmd = "\x1b]133;A\a" "user@box:~$ \x1b]133;B\a" "echo OK\r\n\x1b]133;C\a" "OK\r\n\x1b]133;D;0\a";
    parser.Feed(nextCmd.data(), nextCmd.size());

    assert(s.LastExitCode() == 0);
    assert(s.LastCommandFailed() == false);
    assert(s.CommandMarks().size() == 2);
    std::cout << "  ✓ Command success detected: exit code 0.\n";

    // Test prompt navigation / jumping
    bool jumpedPrev = s.JumpToPreviousCommand();
    assert(jumpedPrev == true);
    std::cout << "  ✓ Semantic prompt jump previous succeeded.\n";

    bool jumpedNext = s.JumpToNextCommand();
    assert(jumpedNext == true);
    std::cout << "  ✓ Semantic prompt jump next succeeded.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "FullTerminal OSC 8 & OSC 133 Unit Tests\n";
    std::cout << "========================================\n";

    try {
        TestOsc8Hyperlinks();
        TestPlainTextUrlAutoDetection();
        TestOsc133SemanticShellIntegration();
        std::cout << "\n>>> ALL OSC 8 & 133 TESTS PASSED (3/3) <<<\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
