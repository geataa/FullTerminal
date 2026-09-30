#include "services/SwarmWorkspace.h"
#include <algorithm>
#include <cctype>

namespace ft {

SwarmWorkspaceManager& SwarmWorkspaceManager::Instance() {
    static SwarmWorkspaceManager instance;
    return instance;
}

const std::vector<SwarmPresetDesc>& SwarmWorkspaceManager::GetPresets() const {
    static const std::vector<SwarmPresetDesc> kPresets = {
        {
            SwarmPreset::PairProgramming,
            "pair_programming",
            L"AI Pair Programming",
            L"Sol: Ajan (%50), Sag-Ust: Test (%25), Sag-Alt: Git/Log (%25)",
            3
        },
        {
            SwarmPreset::DualAgent,
            "dual_agent",
            L"Dual Agent Review",
            L"Sol: Birincil Ajan (%50), Sag: Kod Denetleyici Ajan (%50)",
            2
        },
        {
            SwarmPreset::QuadGrid,
            "quad_grid",
            L"DevOps Quad Grid",
            L"2x2 Esit Dortlu Izgara (Her panel %25)",
            4
        },
        {
            SwarmPreset::TriagePipeline,
            "triage",
            L"Triage & Debug Pipeline",
            L"Ust: Ana Terminal (%60), Sol-Alt: Test (%20), Sag-Alt: Takip (%20)",
            3
        }
    };
    return kPresets;
}

SwarmPresetDesc SwarmWorkspaceManager::GetPresetDesc(SwarmPreset preset) const {
    const auto& list = GetPresets();
    for (const auto& item : list) {
        if (item.preset == preset) return item;
    }
    return list.front();
}

SwarmPreset SwarmWorkspaceManager::ParsePreset(const std::string& name) const {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "pair_programming" || lower == "pair" || lower == "swarm3") return SwarmPreset::PairProgramming;
    if (lower == "dual_agent" || lower == "dual" || lower == "review") return SwarmPreset::DualAgent;
    if (lower == "quad_grid" || lower == "quad" || lower == "grid4") return SwarmPreset::QuadGrid;
    if (lower == "triage" || lower == "pipeline") return SwarmPreset::TriagePipeline;

    return SwarmPreset::PairProgramming;
}

std::string SwarmWorkspaceManager::PresetToString(SwarmPreset preset) const {
    switch (preset) {
    case SwarmPreset::PairProgramming: return "pair_programming";
    case SwarmPreset::DualAgent:       return "dual_agent";
    case SwarmPreset::QuadGrid:        return "quad_grid";
    case SwarmPreset::TriagePipeline:  return "triage";
    }
    return "pair_programming";
}

} // namespace ft
