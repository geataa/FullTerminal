#pragma once
//
// FullTerminal — Multi-Agent Swarm Workspace Presets
// Herdr swarm & multi-agent is birligi mimarisinden ilham alinan,
// tek tusla veya MCP ile coklu panel orkestrasyonu saglayan sablon motoru.
//

#include <string>
#include <vector>

namespace ft {

enum class SwarmPreset {
    PairProgramming, // 3 Panelli: Sol Ajan (%50), Sag-Ust Test (%25), Sag-Alt Git/Log (%25)
    DualAgent,       // 2 Panelli: Sol Ajan A (%50), Sag Ajan B (%50)
    QuadGrid,        // 4 Panelli: 2x2 Izgara (Her biri %25)
    TriagePipeline   // 3 Panelli: Ust Ana Panel (%60), Alt-Sol Derleme (%20), Alt-Sag Takip (%20)
};

struct SwarmPresetDesc {
    SwarmPreset preset = SwarmPreset::PairProgramming;
    std::string id;             // "pair_programming", "dual_agent", "quad_grid", "triage"
    std::wstring title;         // L"AI Pair Programming (3 Panel)"
    std::wstring description;   // L"Sol: Ajan, Sag-Ust: Test/Build, Sag-Alt: Git/Log"
    int paneCount = 3;
};

class SwarmWorkspaceManager {
public:
    static SwarmWorkspaceManager& Instance();

    const std::vector<SwarmPresetDesc>& GetPresets() const;
    SwarmPresetDesc GetPresetDesc(SwarmPreset preset) const;
    SwarmPreset ParsePreset(const std::string& name) const;
    std::string PresetToString(SwarmPreset preset) const;
};

} // namespace ft
