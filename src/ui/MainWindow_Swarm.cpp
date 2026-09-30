#include "ui/MainWindowInternal.h"
#include "services/SwarmWorkspace.h"
#include "mcp/Json.h"

namespace ft {

bool MainWindow::LaunchSwarmPreset(SwarmPreset preset, const std::wstring& customCwd,
                                   const std::vector<std::string>& initialCommands) {
    if (m_profiles.empty() || !m_r.Ready()) return false;

    // Her swarm calisma alani icin temiz, yeni bir sekme aciyoruz
    const size_t defProfIdx = DefaultProfileIndex();
    if (!NewTab(defProfIdx)) return false;

    const size_t tabIdx = m_active;
    if (tabIdx >= m_tabLayouts.size() || !m_tabLayouts[tabIdx]) return false;
    auto& layout = m_tabLayouts[tabIdx];

    ShellProfile baseProfile = m_profiles[defProfIdx];
    if (!customCwd.empty()) {
        baseProfile.startDir = customCwd;
    }

    auto rootTab = layout->GetFocusedTab();
    const uint32_t rootPaneId = layout->GetFocusedPaneId();

    if (rootTab && !initialCommands.empty() && !initialCommands[0].empty()) {
        std::string cmd = initialCommands[0];
        if (cmd.back() != '\n') cmd += "\r\n";
        rootTab->Write(cmd.data(), cmd.size());
    }

    auto spawnSubTab = [&](const std::string& initCmd) -> std::shared_ptr<TerminalTab> {
        const FontMetrics& fm = m_r.Metrics();
        const int cols = std::max(8, static_cast<int>((m_lay.term.right - m_lay.term.left) / (fm.cellW * 2)));
        const int rows = std::max(2, static_cast<int>((m_lay.term.bottom - m_lay.term.top) / (fm.cellH * 2)));

        auto t = std::make_shared<TerminalTab>();
        std::wstring err;
        if (t->Start(baseProfile, cols, rows, m_hwnd, WM_PTY_DATA, &err)) {
            if (!initCmd.empty()) {
                std::string cmd = initCmd;
                if (cmd.back() != '\n') cmd += "\r\n";
                t->Write(cmd.data(), cmd.size());
            }
        }
        return t;
    };

    switch (preset) {
    case SwarmPreset::PairProgramming: {
        // Sol Panel: Ajan (Root)
        // Sag-Ust: Test/Derleme
        std::string cmd1 = (initialCommands.size() > 1) ? initialCommands[1] : "";
        auto sub1 = spawnSubTab(cmd1);
        layout->SplitActive(SplitDirection::Vertical, sub1);

        // Sag-Alt: Git/Log
        std::string cmd2 = (initialCommands.size() > 2) ? initialCommands[2] : "";
        auto sub2 = spawnSubTab(cmd2);
        layout->SplitActive(SplitDirection::Horizontal, sub2);

        // Odagi tekrar sol panele (Orchestrator Ajan) al
        layout->SetFocusedPane(rootPaneId);
        break;
    }

    case SwarmPreset::DualAgent: {
        // Sol Panel: Ajan A (Root)
        // Sag Panel: Ajan B (Reviewer)
        std::string cmd1 = (initialCommands.size() > 1) ? initialCommands[1] : "";
        auto sub1 = spawnSubTab(cmd1);
        layout->SplitActive(SplitDirection::Vertical, sub1);

        layout->SetFocusedPane(rootPaneId);
        break;
    }

    case SwarmPreset::QuadGrid: {
        // 2x2 Izgara
        // 1. Sol ve Sag olarak ikiye bol
        std::string cmd1 = (initialCommands.size() > 1) ? initialCommands[1] : "";
        auto rightTop = spawnSubTab(cmd1);
        layout->SplitActive(SplitDirection::Vertical, rightTop);

        // 2. Sag paneli yatay bol (Sag-Alt olustur)
        std::string cmd2 = (initialCommands.size() > 2) ? initialCommands[2] : "";
        auto rightBottom = spawnSubTab(cmd2);
        layout->SplitActive(SplitDirection::Horizontal, rightBottom);

        // 3. Sol panele don ve yatay bol (Sol-Alt olustur)
        layout->SetFocusedPane(rootPaneId);
        std::string cmd3 = (initialCommands.size() > 3) ? initialCommands[3] : "";
        auto leftBottom = spawnSubTab(cmd3);
        layout->SplitActive(SplitDirection::Horizontal, leftBottom);

        // Odagi Sol-Ust panele al
        layout->SetFocusedPane(rootPaneId);
        break;
    }

    case SwarmPreset::TriagePipeline: {
        // Ust: Ana Terminal (Root)
        // Alt-Sol: Test
        std::string cmd1 = (initialCommands.size() > 1) ? initialCommands[1] : "";
        auto bottomMain = spawnSubTab(cmd1);
        layout->SplitActive(SplitDirection::Horizontal, bottomMain);

        // Alt-Sag: Takip/Log
        std::string cmd2 = (initialCommands.size() > 2) ? initialCommands[2] : "";
        auto bottomAux = spawnSubTab(cmd2);
        layout->SplitActive(SplitDirection::Vertical, bottomAux);

        layout->SetFocusedPane(rootPaneId);
        break;
    }
    }

    m_tabs[m_active] = layout->GetFocusedTab();
    SyncGridToArea();
    ComputeLayout();
    SetView(View::Terminal);
    m_dirty = true;

    auto desc = SwarmWorkspaceManager::Instance().GetPresetDesc(preset);
    Toast(L"Swarm alani hazirlandi: " + desc.title);
    return true;
}

void MainWindow::ShowSwarmMenu(POINT screenPt) {
    std::vector<std::wstring> items;
    const auto& presets = SwarmWorkspaceManager::Instance().GetPresets();
    for (const auto& p : presets) {
        items.push_back(p.title + L" \u2014 " + p.description);
    }

    const int sel = ShowListMenu(screenPt, items);
    if (sel >= 0 && static_cast<size_t>(sel) < presets.size()) {
        LaunchSwarmPreset(presets[sel].preset);
    }
}

void MainWindow::HandleMcpSwarm(const json::Value& req, json::Value& resp) {
    std::string presetStr = "pair_programming";
    if (req.has("preset") && req["preset"].is_string()) {
        presetStr = req["preset"].as_string();
    }

    std::wstring cwd;
    if (req.has("cwd") && req["cwd"].is_string()) {
        cwd = Utf8ToWide(req["cwd"].as_string());
    }

    std::vector<std::string> commands;
    if (req.has("commands") && req["commands"].is_array()) {
        for (const auto& c : req["commands"].arrVal) {
            if (c.is_string()) {
                commands.push_back(c.as_string());
            }
        }
    }

    const SwarmPreset preset = SwarmWorkspaceManager::Instance().ParsePreset(presetStr);
    const bool ok = LaunchSwarmPreset(preset, cwd, commands);

    resp["ok"] = json::Value(ok);
    resp["preset"] = json::Value(presetStr);
    if (m_active < m_tabLayouts.size() && m_tabLayouts[m_active]) {
        resp["paneCount"] = json::Value(static_cast<int>(m_tabLayouts[m_active]->PaneCount()));
        resp["tabIndex"] = json::Value(static_cast<int>(m_active));
    }
}

} // namespace ft
