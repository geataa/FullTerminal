#include "services/AgentDetector.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

using namespace ft;

// Simule edilmis ApproveAgent komut esleme mantigi (MainWindow_AgentRibbon ile ayni mantik)
std::string DetermineApprovalKeystrokes(const AgentDetectionResult& st, bool approve) {
    if (!st.isBlocked()) return {};

    if (st.ruleId == "claude_live_blocked_form") {
        return approve ? "\r\n" : "\x1b";
    } else if (st.ruleId == "generic_prompt_blocked" && st.detail.find("press enter") != std::string::npos) {
        return approve ? "\r\n" : "\x03";
    } else {
        return approve ? "y\r\n" : "n\r\n";
    }
}

// Simule edilmis Ribbon Geometri Hesaplayicisi
struct RectF {
    float left, top, right, bottom;
};

RectF ComputeRibbonRect(const RectF& paneArea, float scale) {
    const float s = (scale > 0.1f) ? scale : 1.0f;
    const float ribbonH = 38.0f * s;
    const float margin = 6.0f * s;

    float top = paneArea.bottom - ribbonH - margin;
    if (top < paneArea.top + 20.0f * s) {
        top = paneArea.top + 2.0f * s;
    }
    return RectF{ paneArea.left + margin, top, paneArea.right - margin, paneArea.bottom - margin };
}

bool HitRibbon(const RectF& ribbon, float x, float y) {
    return (x >= ribbon.left && x <= ribbon.right && y >= ribbon.top && y <= ribbon.bottom);
}

void TestAgentApprovalRibbon() {
    std::cout << "[TEST] Starting Agent Approval Ribbon / HUD unit tests...\n";

    // 1. Test Claude Code live form (Enter to confirm, Esc to cancel)
    {
        std::vector<std::string> lines = {
            "Select an option to proceed:",
            "> [1] Run bash command",
            "  [2] Skip step",
            "Esc to cancel, Enter to confirm"
        };
        auto res = AgentDetector::Instance().Detect(lines);
        assert(res.isBlocked());
        assert(res.kind == AgentKind::Claude);
        assert(res.ruleId == "claude_live_blocked_form");

        std::string approveKey = DetermineApprovalKeystrokes(res, true);
        std::string denyKey = DetermineApprovalKeystrokes(res, false);
        assert(approveKey == "\r\n");
        assert(denyKey == "\x1b");
        std::cout << "Test 1 (Claude Code Form Approval Keystrokes): PASSED\n";
    }

    // 2. Test Antigravity CLI permission prompt (y/n)
    {
        std::vector<std::string> lines = {
            "Antigravity Agent Request",
            "Requesting permission for: git push origin main",
            "Do you want to proceed? [y/n]"
        };
        auto res = AgentDetector::Instance().Detect(lines);
        assert(res.isBlocked());
        assert(res.kind == AgentKind::Antigravity);
        assert(res.ruleId == "agy_permission_prompt");

        std::string approveKey = DetermineApprovalKeystrokes(res, true);
        std::string denyKey = DetermineApprovalKeystrokes(res, false);
        assert(approveKey == "y\r\n");
        assert(denyKey == "n\r\n");
        std::cout << "Test 2 (Antigravity Permission Keystrokes): PASSED\n";
    }

    // 3. Test Generic press enter prompt
    {
        std::vector<std::string> lines = {
            "Deployment completed with warnings.",
            "Press Enter to continue..."
        };
        auto res = AgentDetector::Instance().Detect(lines);
        assert(res.isBlocked());
        assert(res.ruleId == "generic_prompt_blocked");

        std::string approveKey = DetermineApprovalKeystrokes(res, true);
        std::string denyKey = DetermineApprovalKeystrokes(res, false);
        assert(approveKey == "\r\n");
        assert(denyKey == "\x03"); // Ctrl+C to abort
        std::cout << "Test 3 (Generic Press Enter Keystrokes): PASSED\n";
    }

    // 4. Test Ribbon Geometry Calculation & Clamping
    {
        RectF paneArea{ 0.0f, 0.0f, 800.0f, 600.0f };
        float scale = 1.0f;
        RectF ribbon = ComputeRibbonRect(paneArea, scale);

        // Ribbon height should be exactly 38px, margin 6px
        assert(ribbon.left == 6.0f);
        assert(ribbon.right == 794.0f);
        assert(ribbon.bottom == 594.0f);
        assert(ribbon.top == 594.0f - 38.0f);

        // Test Hit testing
        assert(HitRibbon(ribbon, 400.0f, 570.0f) == true);  // Inside
        assert(HitRibbon(ribbon, 400.0f, 200.0f) == false); // Far above (terminal text)
        assert(HitRibbon(ribbon, 2.0f, 570.0f) == false);   // Left of ribbon
        assert(HitRibbon(ribbon, 796.0f, 570.0f) == false);  // Right of ribbon
        assert(HitRibbon(ribbon, 400.0f, 598.0f) == false);  // Below ribbon

        std::cout << "Test 4 (Ribbon Geometry & Hit-Testing): PASSED\n";
    }

    // 5. Test Tiny Pane Clamping
    {
        RectF tinyPane{ 0.0f, 0.0f, 300.0f, 50.0f }; // Very short pane
        float scale = 1.0f;
        RectF ribbon = ComputeRibbonRect(tinyPane, scale);
        // Clamped top must not exceed upper bound
        assert(ribbon.top >= tinyPane.top);
        assert(ribbon.bottom <= tinyPane.bottom);
        std::cout << "Test 5 (Tiny Pane Geometry Clamping): PASSED\n";
    }

    // 6. Test Non-blocked state does not generate keystrokes
    {
        std::vector<std::string> lines = {
            "Agent working...",
            "\xE2\xA0\x8B Thinking and analyzing files..." // braille spinner
        };
        auto res = AgentDetector::Instance().Detect(lines);
        assert(res.isWorking());
        assert(!res.isBlocked());

        std::string key = DetermineApprovalKeystrokes(res, true);
        assert(key.empty());
        std::cout << "Test 6 (Working State Does Not Generate Approval): PASSED\n";
    }

    std::cout << "\n============================================\n";
    std::cout << "  ALL AGENT APPROVAL RIBBON TESTS PASSED!\n";
    std::cout << "============================================\n";
}

int main() {
    TestAgentApprovalRibbon();
    return 0;
}
