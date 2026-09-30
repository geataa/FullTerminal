#include "services/SwarmWorkspace.h"
#include "ui/PaneLayout.h"
#include "mcp/McpServer.h"
#include "mcp/Json.h"
#include "core/Settings.h"
#include <iostream>
#include <cassert>
#include <string>

#include "ui/MainWindow.h"

using namespace ft;

namespace ft {
const wchar_t* MainWindow::ClassName() {
    return L"FullTerminalWindow";
}
}

void TestSwarmWorkspace() {
    std::cout << "[TEST] Starting Multi-Agent Swarm Workspace unit tests...\n";

    auto& mgr = SwarmWorkspaceManager::Instance();

    // 1. Preset list verification
    const auto& presets = mgr.GetPresets();
    assert(presets.size() == 4);

    assert(presets[0].preset == SwarmPreset::PairProgramming);
    assert(presets[0].paneCount == 3);
    assert(presets[0].id == "pair_programming");

    assert(presets[1].preset == SwarmPreset::DualAgent);
    assert(presets[1].paneCount == 2);
    assert(presets[1].id == "dual_agent");

    assert(presets[2].preset == SwarmPreset::QuadGrid);
    assert(presets[2].paneCount == 4);
    assert(presets[2].id == "quad_grid");

    assert(presets[3].preset == SwarmPreset::TriagePipeline);
    assert(presets[3].paneCount == 3);
    assert(presets[3].id == "triage");

    std::cout << "Test 1 (Swarm Preset Enumeration): PASSED\n";

    // 2. Preset parsing and serialization
    assert(mgr.ParsePreset("pair_programming") == SwarmPreset::PairProgramming);
    assert(mgr.ParsePreset("PAIR") == SwarmPreset::PairProgramming);
    assert(mgr.ParsePreset("swarm3") == SwarmPreset::PairProgramming);
    assert(mgr.ParsePreset("dual_agent") == SwarmPreset::DualAgent);
    assert(mgr.ParsePreset("REVIEW") == SwarmPreset::DualAgent);
    assert(mgr.ParsePreset("quad_grid") == SwarmPreset::QuadGrid);
    assert(mgr.ParsePreset("grid4") == SwarmPreset::QuadGrid);
    assert(mgr.ParsePreset("triage") == SwarmPreset::TriagePipeline);
    assert(mgr.ParsePreset("pipeline") == SwarmPreset::TriagePipeline);

    assert(mgr.PresetToString(SwarmPreset::PairProgramming) == "pair_programming");
    assert(mgr.PresetToString(SwarmPreset::DualAgent) == "dual_agent");
    assert(mgr.PresetToString(SwarmPreset::QuadGrid) == "quad_grid");
    assert(mgr.PresetToString(SwarmPreset::TriagePipeline) == "triage");

    std::cout << "Test 2 (Preset String Parsing & Serialization): PASSED\n";

    // 3. MCP Tools/List contains ft_swarm_spawn
    {
        Settings cfg;
        cfg.mcpEnabled = true;
        cfg.mcpStdio = true;
        cfg.Save(PortableDataDir());

        std::string req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}";
        std::string res = McpServer::ProcessMessage(req);
        bool ok = false;
        json::Value v = json::Value::parse(res, &ok);
        assert(ok);
        const auto& tools = v["result"]["tools"];
        assert(tools.is_array());

        bool hasSwarm = false;
        for (const auto& t : tools.arrVal) {
            if (t["name"].as_string() == "ft_swarm_spawn") {
                hasSwarm = true;
                const auto& props = t["inputSchema"]["properties"];
                assert(props.has("preset"));
                assert(props.has("cwd"));
                assert(props.has("commands"));
            }
        }
        assert(hasSwarm);
        std::cout << "Test 3 (ft_swarm_spawn MCP Tool Registration): PASSED\n";
    }

    // 4. Simulated BSP Tree layout for PairProgramming (3 panes)
    {
        // Simulate root tab
        auto rootTab = std::make_shared<TerminalTab>();
        PaneLayout layout(rootTab);
        assert(layout.PaneCount() == 1);

        // Split Vertical -> 2 panes
        auto sub1 = std::make_shared<TerminalTab>();
        bool ok = layout.SplitActive(SplitDirection::Vertical, sub1);
        assert(ok);
        assert(layout.PaneCount() == 2);

        // Split Horizontal on right pane -> 3 panes
        auto sub2 = std::make_shared<TerminalTab>();
        ok = layout.SplitActive(SplitDirection::Horizontal, sub2);
        assert(ok);
        assert(layout.PaneCount() == 3);

        D2D1_RECT_F totalArea = D2D1::RectF(0.0f, 0.0f, 1000.0f, 600.0f);
        auto panes = layout.ComputeLayout(totalArea);
        assert(panes.size() == 3);

        // Left pane should take approximately left half (~500px)
        assert(panes[0].area.left == 0.0f);
        assert(panes[0].area.right < 500.0f);

        // Right panes should be on right half
        assert(panes[1].area.left > 490.0f);
        assert(panes[2].area.left > 490.0f);
        // And stacked vertically
        assert(panes[1].area.bottom < panes[2].area.top + 10.0f);

        std::cout << "Test 4 (PairProgramming BSP Tree Geometry Simulation): PASSED\n";
    }

    // 5. Simulated BSP Tree layout for QuadGrid (4 panes)
    {
        auto rootTab = std::make_shared<TerminalTab>();
        PaneLayout layout(rootTab);
        uint32_t rootId = layout.GetFocusedPaneId();

        // 1. Split Vertical
        auto p1 = std::make_shared<TerminalTab>();
        layout.SplitActive(SplitDirection::Vertical, p1);

        // 2. Split Right Horizontal
        auto p2 = std::make_shared<TerminalTab>();
        layout.SplitActive(SplitDirection::Horizontal, p2);

        // 3. Return to left and split Horizontal
        layout.SetFocusedPane(rootId);
        auto p3 = std::make_shared<TerminalTab>();
        layout.SplitActive(SplitDirection::Horizontal, p3);

        assert(layout.PaneCount() == 4);

        D2D1_RECT_F totalArea = D2D1::RectF(0.0f, 0.0f, 1000.0f, 600.0f);
        auto panes = layout.ComputeLayout(totalArea);
        assert(panes.size() == 4);

        std::cout << "Test 5 (QuadGrid 2x2 Layout Simulation): PASSED\n";
    }

    std::cout << "\n============================================\n";
    std::cout << "  ALL SWARM WORKSPACE TESTS PASSED!\n";
    std::cout << "============================================\n";
}

int main() {
    TestSwarmWorkspace();
    return 0;
}
