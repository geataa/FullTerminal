#pragma once
//
// FullTerminal — Agent State Detection Engine
// Terminal cikti tamponunu ve pencere basliklarini tarayarak AI kodlama
// ajanlarinin (Antigravity, Claude Code, Codex vb.) durumunu tespit eder.
//

#include <string>
#include <vector>
#include <cstdint>

namespace ft {

enum class AgentKind {
    Unknown,
    Antigravity,
    Claude,
    Codex,
    Cursor,
    Gemini,
    Generic
};

enum class AgentState {
    Unknown, // Siradan kabuk veya taninmayan surec
    Idle,    // Ajan turunu tamamladi, kullanici komut isteminde bekliyor
    Working, // Ajan dusunuyor, kod uretiyor veya arac calistiriyor (spinner aktif)
    Blocked  // Ajan durdu: kullanicidan izin, soru veya onay bekliyor!
};

struct AgentDetectionResult {
    AgentKind   kind = AgentKind::Unknown;
    AgentState  state = AgentState::Unknown;
    std::string ruleId;         // Ornek: "permission_prompt", "spinner_working"
    std::string matchedPattern; // Eslesen kural/desen tanimi
    std::string detail;         // Eslesen satir veya kisim

    bool isBlocked() const { return state == AgentState::Blocked; }
    bool isWorking() const { return state == AgentState::Working; }
    bool isIdle()    const { return state == AgentState::Idle; }
    bool isKnown()   const { return state != AgentState::Unknown; }
};

class AgentDetector {
public:
    static AgentDetector& Instance();

    // Terminal ekraninin son satirlarini ve basligini analiz eder
    AgentDetectionResult Detect(const std::vector<std::string>& tailLines,
                                const std::wstring& windowTitle = L"",
                                bool isAltBuffer = false) const;

    // Tek bir ham metin blogunu analiz eder
    AgentDetectionResult DetectFromText(const std::string& bufferText,
                                        const std::wstring& windowTitle = L"",
                                        bool isAltBuffer = false) const;

    static const char* StateToString(AgentState st);
    static const char* KindToString(AgentKind k);

private:
    AgentDetector() = default;
    ~AgentDetector() = default;
    AgentDetector(const AgentDetector&) = delete;
    AgentDetector& operator=(const AgentDetector&) = delete;

    static bool Contains(const std::string& haystack, const std::string& needle);
    static bool ContainsAny(const std::string& haystack, const std::vector<std::string>& needles);
    static bool HasBrailleSpinner(const std::string& text);
};

} // namespace ft
