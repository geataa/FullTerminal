#include "services/AgentDetector.h"
#include <iostream>
#include <cassert>

using namespace ft;

void TestAgentDetector() {
    std::cout << "[TEST] Starting AgentDetector unit tests...\n";

    // 1. Antigravity Blocked Test
    {
        std::vector<std::string> lines = {
            "Running task step 3/5",
            "Antigravity CLI v2.0",
            "requesting permission for: run command 'git push origin main'",
            "do you want to proceed? [y/N]"
        };
        auto res = AgentDetector::Instance().Detect(lines);
        std::cout << "Test 1 (Antigravity Blocked): state=" << AgentDetector::StateToString(res.state)
                  << ", kind=" << AgentDetector::KindToString(res.kind)
                  << ", rule=" << res.ruleId << "\n";
        assert(res.state == AgentState::Blocked);
        assert(res.kind == AgentKind::Antigravity);
    }

    // 2. Antigravity Working (Braille Spinner) Test
    {
        // 0xE2 0xA0 0x8B is '⠋'
        std::string spinnerLine = "\xE2\xA0\x8B Thinking and analyzing code structure...";
        std::vector<std::string> lines = {
            "agy> /plan",
            spinnerLine
        };
        auto res = AgentDetector::Instance().Detect(lines);
        std::cout << "Test 2 (Braille Spinner Working): state=" << AgentDetector::StateToString(res.state)
                  << ", rule=" << res.ruleId << "\n";
        assert(res.state == AgentState::Working);
    }

    // 3. Claude Code Blocked Test
    {
        std::vector<std::string> lines = {
            "Claude Code v1.5",
            "File edit proposed for src/main.cpp",
            "esc to cancel",
            "enter to confirm"
        };
        auto res = AgentDetector::Instance().Detect(lines);
        std::cout << "Test 3 (Claude Code Blocked): state=" << AgentDetector::StateToString(res.state)
                  << ", kind=" << AgentDetector::KindToString(res.kind)
                  << ", rule=" << res.ruleId << "\n";
        assert(res.state == AgentState::Blocked);
        assert(res.kind == AgentKind::Claude);
    }

    // 4. Claude Code Working Test
    {
        std::vector<std::string> lines = {
            "Claude Code",
            "Reading files and generating response...",
            "esc to interrupt"
        };
        auto res = AgentDetector::Instance().Detect(lines);
        std::cout << "Test 4 (Claude Code Working): state=" << AgentDetector::StateToString(res.state)
                  << ", kind=" << AgentDetector::KindToString(res.kind)
                  << ", rule=" << res.ruleId << "\n";
        assert(res.state == AgentState::Working);
        assert(res.kind == AgentKind::Claude);
    }

    // 5. Codex Blocked Test
    {
        std::vector<std::string> lines = {
            "Codex CLI",
            "Approval required to write to disk",
            "Do you want to run this? (y/n)"
        };
        auto res = AgentDetector::Instance().Detect(lines);
        std::cout << "Test 5 (Codex Blocked): state=" << AgentDetector::StateToString(res.state)
                  << ", kind=" << AgentDetector::KindToString(res.kind)
                  << ", rule=" << res.ruleId << "\n";
        assert(res.state == AgentState::Blocked);
        assert(res.kind == AgentKind::Codex);
    }

    // 6. Generic Confirmation Blocked Test
    {
        std::vector<std::string> lines = {
            "apt install build-essential",
            "After this operation, 120 MB of additional disk space will be used.",
            "Do you want to continue? [Y/n]"
        };
        auto res = AgentDetector::Instance().Detect(lines);
        std::cout << "Test 6 (Generic [Y/n] Blocked): state=" << AgentDetector::StateToString(res.state)
                  << ", rule=" << res.ruleId << "\n";
        assert(res.state == AgentState::Blocked);
    }

    // 7. Idle Prompt Test
    {
        std::vector<std::string> lines = {
            "Task completed successfully.",
            "claude> "
        };
        auto res = AgentDetector::Instance().Detect(lines);
        std::cout << "Test 7 (Idle Prompt): state=" << AgentDetector::StateToString(res.state)
                  << ", rule=" << res.ruleId << "\n";
        assert(res.state == AgentState::Idle);
    }

    std::cout << "[SUCCESS] ALL 7 AgentDetector unit tests PASSED cleanly!\n";
}

int main() {
    TestAgentDetector();
    return 0;
}
