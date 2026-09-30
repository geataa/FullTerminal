#include "services/AgentDetector.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <regex>

namespace ft {

namespace {

std::string ToLower(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (char c : sv) {
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return s;
}

std::string WStringToUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string res(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), res.data(), size, nullptr, nullptr);
    return res;
}

} // namespace

AgentDetector& AgentDetector::Instance() {
    static AgentDetector instance;
    return instance;
}

bool AgentDetector::Contains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    return haystack.find(needle) != std::string::npos;
}

bool AgentDetector::ContainsAny(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (Contains(haystack, needle)) return true;
    }
    return false;
}

bool AgentDetector::HasBrailleSpinner(const std::string& text) {
    // UTF-8 Braille Patterns U+2800..U+28FF are 3 bytes: 0xE2 0xA0 0x80 .. 0xE2 0xA3 0xBF
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text.data());
    const size_t len = text.size();
    for (size_t i = 0; i + 2 < len; ++i) {
        if (p[i] == 0xE2) {
            uint8_t b1 = p[i + 1];
            uint8_t b2 = p[i + 2];
            // Braille range 0xE2, 0xA0..0xA3, 0x80..0xBF
            if (b1 >= 0xA0 && b1 <= 0xA3 && (b2 >= 0x80 && b2 <= 0xBF)) {
                return true;
            }
            // Half circle spinners: U+25D0..U+25D3 (0xE2, 0x97, 0x90..0x93)
            if (b1 == 0x97 && (b2 >= 0x90 && b2 <= 0x93)) {
                return true;
            }
        }
    }
    return false;
}

AgentDetectionResult AgentDetector::Detect(const std::vector<std::string>& tailLines,
                                          const std::wstring& windowTitle,
                                          bool isAltBuffer) const {
    AgentDetectionResult res;
    if (tailLines.empty() && windowTitle.empty()) {
        return res;
    }

    // Baslik incelemesi (OSC title)
    std::string titleUtf8 = ToLower(WStringToUtf8(windowTitle));
    if (HasBrailleSpinner(WStringToUtf8(windowTitle))) {
        res.state = AgentState::Working;
        res.ruleId = "osc_title_spinner";
        res.matchedPattern = "OSC title braille spinner active";
        if (Contains(titleUtf8, "claude")) res.kind = AgentKind::Claude;
        else if (Contains(titleUtf8, "antigravity") || Contains(titleUtf8, "agy")) res.kind = AgentKind::Antigravity;
        else if (Contains(titleUtf8, "codex")) res.kind = AgentKind::Codex;
        else res.kind = AgentKind::Generic;
        return res;
    }

    // Tampon satirlarini birlestir ve kucuk harfe cevir
    std::string combinedLower;
    std::vector<std::string> linesLower;
    linesLower.reserve(tailLines.size());

    for (const auto& l : tailLines) {
        std::string low = ToLower(l);
        linesLower.push_back(low);
        combinedLower.append(low);
        combinedLower.push_back('\n');
    }

    // Ajan kimligi belirleme (Kind)
    if (Contains(combinedLower, "antigravity") || Contains(combinedLower, "agy") || Contains(titleUtf8, "antigravity")) {
        res.kind = AgentKind::Antigravity;
    } else if (Contains(combinedLower, "claude") || Contains(titleUtf8, "claude")) {
        res.kind = AgentKind::Claude;
    } else if (Contains(combinedLower, "codex") || Contains(titleUtf8, "codex")) {
        res.kind = AgentKind::Codex;
    } else if (Contains(combinedLower, "cursor") || Contains(titleUtf8, "cursor")) {
        res.kind = AgentKind::Cursor;
    } else if (Contains(combinedLower, "gemini") || Contains(titleUtf8, "gemini")) {
        res.kind = AgentKind::Gemini;
    }

    // =========================================================================
    // 1. ASAMA: BLOCKED DURUMU (En Yuksek Oncelik - Kullanici onayi / sorusu)
    // =========================================================================

    // Antigravity Blocked
    if (Contains(combinedLower, "requesting permission for:") ||
        Contains(combinedLower, "do you want to proceed?") ||
        (Contains(combinedLower, "tab amend") && Contains(combinedLower, "edit command"))) {
        res.state = AgentState::Blocked;
        res.kind = AgentKind::Antigravity;
        res.ruleId = "agy_permission_prompt";
        res.matchedPattern = "Antigravity permission prompt waiting for confirmation";
        return res;
    }

    // Claude Code Blocked
    if (Contains(combinedLower, "esc to cancel") &&
        (Contains(combinedLower, "enter to confirm") || Contains(combinedLower, "enter to select"))) {
        res.state = AgentState::Blocked;
        res.kind = AgentKind::Claude;
        res.ruleId = "claude_live_blocked_form";
        res.matchedPattern = "Claude Code selection/confirmation dialog active";
        return res;
    }

    if (Contains(combinedLower, "waiting for permission") ||
        Contains(combinedLower, "do you want to allow this connection?") ||
        Contains(combinedLower, "approve this action?")) {
        res.state = AgentState::Blocked;
        res.kind = AgentKind::Claude;
        res.ruleId = "claude_permission_prompt";
        res.matchedPattern = "Claude Code waiting for user approval";
        return res;
    }

    // Codex Blocked
    if (Contains(combinedLower, "approval required") ||
        Contains(combinedLower, "do you want to run this?")) {
        res.state = AgentState::Blocked;
        res.kind = AgentKind::Codex;
        res.ruleId = "codex_approval_prompt";
        res.matchedPattern = "Codex CLI approval required";
        return res;
    }

    // Genel Etkilesimli Onay / Soru kaliplari (Generic Blocked)
    for (const auto& l : linesLower) {
        if (Contains(l, "(y/n)") || Contains(l, "[y/n]") || Contains(l, "[y/n]?") ||
            Contains(l, "[y/n]:") || Contains(l, "[yes/no]") ||
            Contains(l, "are you sure you want to continue?") ||
            Contains(l, "press enter to continue") ||
            Contains(l, "password:") || Contains(l, "[sudo] password for")) {
            res.state = AgentState::Blocked;
            if (res.kind == AgentKind::Unknown) res.kind = AgentKind::Generic;
            res.ruleId = "generic_prompt_blocked";
            res.matchedPattern = "Interactive user confirmation or password requested";
            res.detail = l;
            return res;
        }
    }

    // =========================================================================
    // 2. ASAMA: WORKING DURUMU (Ajan calisiyor, kod uretiyor veya dusunuyor)
    // =========================================================================

    // Braille spinner satirlari
    for (size_t i = 0; i < tailLines.size(); ++i) {
        if (HasBrailleSpinner(tailLines[i])) {
            res.state = AgentState::Working;
            res.ruleId = "spinner_working";
            res.matchedPattern = "Braille spinner in terminal output";
            res.detail = tailLines[i];
            if (res.kind == AgentKind::Unknown) res.kind = AgentKind::Generic;
            return res;
        }
    }

    // Claude Calisiyor: "esc to interrupt"
    if (Contains(combinedLower, "esc to interrupt")) {
        res.state = AgentState::Working;
        res.kind = AgentKind::Claude;
        res.ruleId = "claude_esc_interrupt";
        res.matchedPattern = "Claude Code turn active (esc to interrupt)";
        return res;
    }

    // Claude / Ajan gorevleri calisiyor: "MCP tasks? still running" veya "Waiting for background agent"
    if (Contains(combinedLower, "mcp tasks") && Contains(combinedLower, "still running")) {
        res.state = AgentState::Working;
        res.kind = AgentKind::Claude;
        res.ruleId = "claude_mcp_running";
        res.matchedPattern = "Background MCP tasks running";
        return res;
    }

    if (Contains(combinedLower, "waiting for") && Contains(combinedLower, "background agent")) {
        res.state = AgentState::Working;
        res.kind = AgentKind::Claude;
        res.ruleId = "claude_bg_agents_running";
        res.matchedPattern = "Waiting for background agents to finish";
        return res;
    }

    // Genel calisiyor metinleri
    if (Contains(combinedLower, "thinking...") ||
        Contains(combinedLower, "generating...") ||
        Contains(combinedLower, "executing...") ||
        Contains(combinedLower, "running tests...")) {
        res.state = AgentState::Working;
        res.ruleId = "generic_status_working";
        res.matchedPattern = "Explicit working status text found";
        if (res.kind == AgentKind::Unknown) res.kind = AgentKind::Generic;
        return res;
    }

    // =========================================================================
    // 3. ASAMA: IDLE DURUMU (Ajan veya kabuk komut isteminde bekliyor)
    // =========================================================================
    if (!linesLower.empty()) {
        const std::string& lastLine = linesLower.back();
        // Basit komut istemi sonlari
        if (lastLine.ends_with("> ") || lastLine.ends_with("$ ") ||
            lastLine.ends_with("% ") || lastLine.ends_with("# ") ||
            lastLine.ends_with("? ") || Contains(lastLine, "claude>") ||
            Contains(lastLine, "agy>") || Contains(lastLine, "codex>")) {
            res.state = AgentState::Idle;
            res.ruleId = "prompt_idle";
            res.matchedPattern = "Interactive prompt line detected at bottom";
            return res;
        }
    }

    // Taninmayan surec veya normal kabuk
    res.state = AgentState::Unknown;
    return res;
}

AgentDetectionResult AgentDetector::DetectFromText(const std::string& bufferText,
                                                  const std::wstring& windowTitle,
                                                  bool isAltBuffer) const {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < bufferText.size()) {
        size_t end = bufferText.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(bufferText.substr(start));
            break;
        }
        lines.push_back(bufferText.substr(start, end - start));
        start = end + 1;
    }
    return Detect(lines, windowTitle, isAltBuffer);
}

const char* AgentDetector::StateToString(AgentState st) {
    switch (st) {
        case AgentState::Idle:    return "IDLE";
        case AgentState::Working: return "WORKING";
        case AgentState::Blocked: return "BLOCKED";
        case AgentState::Unknown:
        default:                  return "UNKNOWN";
    }
}

const char* AgentDetector::KindToString(AgentKind k) {
    switch (k) {
        case AgentKind::Antigravity: return "Antigravity";
        case AgentKind::Claude:      return "Claude Code";
        case AgentKind::Codex:       return "Codex";
        case AgentKind::Cursor:      return "Cursor";
        case AgentKind::Gemini:      return "Gemini";
        case AgentKind::Generic:     return "Generic Agent";
        case AgentKind::Unknown:
        default:                     return "Unknown";
    }
}

} // namespace ft
