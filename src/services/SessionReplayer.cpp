#include "services/SessionReplayer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace ft {

SessionReplayer::SessionReplayer() = default;
SessionReplayer::~SessionReplayer() = default;

bool SessionReplayer::Load(const std::wstring& path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == L".cast") {
        return LoadAsciinema(path);
    }
    return LoadBinary(path);
}

bool SessionReplayer::LoadBinary(const std::wstring& ftrecPath) {
    std::ifstream in(ftrecPath, std::ios::binary);
    if (!in.is_open()) return false;

    // 1. Header oku
    RecordingHeader h{};
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    if (in.gcount() < static_cast<std::streamsize>(sizeof(h))) return false;
    if (h.magic != 0x43525446) return false; // 'FTRC'

    m_header = h;
    m_events.clear();

    // 2. Olayları oku
    while (in.peek() != EOF) {
        uint32_t deltaMs = 0;
        uint8_t type = 0;
        uint32_t len = 0;

        in.read(reinterpret_cast<char*>(&deltaMs), sizeof(deltaMs));
        in.read(reinterpret_cast<char*>(&type), sizeof(type));
        in.read(reinterpret_cast<char*>(&len), sizeof(len));
        if (!in) break;

        RecordEvent ev;
        ev.timeOffsetSec = static_cast<double>(deltaMs) / 1000.0;
        ev.type = static_cast<RecordEventType>(type);

        if (ev.type == RecordEventType::Resize && len >= 4) {
            uint16_t c = 80, r = 24;
            in.read(reinterpret_cast<char*>(&c), sizeof(c));
            in.read(reinterpret_cast<char*>(&r), sizeof(r));
            ev.cols = c;
            ev.rows = r;
        } else if (len > 0) {
            ev.data.resize(len);
            in.read(ev.data.data(), len);
        }
        m_events.push_back(std::move(ev));
    }

    m_currentEventIdx = 0;
    m_currentTimeSec = 0.0;
    m_totalDurationSec = m_events.empty() ? 0.0 : m_events.back().timeOffsetSec;
    m_playing = false;
    return true;
}

// Basit JSON dize açma (unescape)
static std::string UnescapeJson(const std::string& s) {
    std::string res;
    res.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[++i];
            switch (next) {
            case '"':  res.push_back('"'); break;
            case '\\': res.push_back('\\'); break;
            case '/':  res.push_back('/'); break;
            case 'b':  res.push_back('\b'); break;
            case 'f':  res.push_back('\f'); break;
            case 'n':  res.push_back('\n'); break;
            case 'r':  res.push_back('\r'); break;
            case 't':  res.push_back('\t'); break;
            case 'u': {
                if (i + 4 < s.size()) {
                    std::string hexStr = s.substr(i + 1, 4);
                    int val = std::stoi(hexStr, nullptr, 16);
                    res.push_back(static_cast<char>(val & 0xFF));
                    i += 4;
                }
                break;
            }
            default: res.push_back(next); break;
            }
        } else {
            res.push_back(s[i]);
        }
    }
    return res;
}

bool SessionReplayer::LoadAsciinema(const std::wstring& castPath) {
    std::ifstream in(castPath);
    if (!in.is_open()) return false;

    std::string line;
    if (!std::getline(in, line)) return false;

    // Header parse: width, height
    m_header = RecordingHeader{};
    m_header.magic = 0x43525446;
    m_header.cols = 80;
    m_header.rows = 24;

    size_t wPos = line.find("\"width\":");
    if (wPos != std::string::npos) {
        m_header.cols = static_cast<uint16_t>(std::atoi(line.c_str() + wPos + 8));
    }
    size_t hPos = line.find("\"height\":");
    if (hPos != std::string::npos) {
        m_header.rows = static_cast<uint16_t>(std::atoi(line.c_str() + hPos + 9));
    }

    m_events.clear();

    // Satır satır olayları oku: [time, "type", "data"]
    while (std::getline(in, line)) {
        if (line.empty() || line[0] != '[') continue;

        size_t comma1 = line.find(',');
        if (comma1 == std::string::npos) continue;

        double timeOffset = std::atof(line.substr(1, comma1 - 1).c_str());

        size_t q1 = line.find('"', comma1 + 1);
        if (q1 == std::string::npos) continue;
        size_t q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;

        std::string typeStr = line.substr(q1 + 1, q2 - q1 - 1);

        size_t q3 = line.find('"', q2 + 1);
        if (q3 == std::string::npos) continue;
        size_t q4 = line.rfind('"');
        if (q4 <= q3) continue;

        std::string rawData = line.substr(q3 + 1, q4 - q3 - 1);
        std::string data = UnescapeJson(rawData);

        RecordEvent ev;
        ev.timeOffsetSec = timeOffset;
        if (typeStr == "o") {
            ev.type = RecordEventType::Output;
            ev.data = std::move(data);
        } else if (typeStr == "i") {
            ev.type = RecordEventType::Input;
            ev.data = std::move(data);
        } else if (typeStr == "r") {
            ev.type = RecordEventType::Resize;
            size_t x = data.find('x');
            if (x != std::string::npos) {
                ev.cols = std::atoi(data.substr(0, x).c_str());
                ev.rows = std::atoi(data.substr(x + 1).c_str());
            }
        } else if (typeStr == "m") {
            ev.type = RecordEventType::Marker;
            ev.data = std::move(data);
        }

        m_events.push_back(std::move(ev));
    }

    m_currentEventIdx = 0;
    m_currentTimeSec = 0.0;
    m_totalDurationSec = m_events.empty() ? 0.0 : m_events.back().timeOffsetSec;
    m_playing = false;
    return !m_events.empty();
}

void SessionReplayer::Play() {
    if (IsFinished()) {
        m_currentTimeSec = 0.0;
        m_currentEventIdx = 0;
    }
    m_playing = true;
}

void SessionReplayer::Pause() {
    m_playing = false;
}

void SessionReplayer::TogglePlay() {
    if (m_playing) Pause();
    else Play();
}

void SessionReplayer::Stop() {
    m_playing = false;
    m_currentTimeSec = 0.0;
    m_currentEventIdx = 0;
}

void SessionReplayer::SetSpeed(float speed) {
    if (speed > 0.05f) {
        m_speed = speed;
    }
}

float SessionReplayer::Progress() const {
    if (m_totalDurationSec <= 0.0001) return 0.0f;
    float p = static_cast<float>(m_currentTimeSec / m_totalDurationSec);
    return std::clamp(p, 0.0f, 1.0f);
}

void SessionReplayer::ApplyEvent(const RecordEvent& ev, Screen& screen, VtParser& parser) {
    if (ev.type == RecordEventType::Output) {
        parser.Feed(ev.data.data(), ev.data.size());
    } else if (ev.type == RecordEventType::Resize && ev.cols > 0 && ev.rows > 0) {
        screen.Resize(ev.cols, ev.rows);
    }
}

bool SessionReplayer::Seek(double targetSec, Screen& screen, VtParser& parser) {
    if (m_events.empty()) return false;
    targetSec = std::clamp(targetSec, 0.0, m_totalDurationSec);

    if (targetSec < m_currentTimeSec) {
        // Geriye sarmada ekran sıfırlanır ve baştan hedef zamana kadar oynatılır
        screen.Reset();
        screen.Resize(m_header.cols, m_header.rows);
        m_currentEventIdx = 0;
    }

    while (m_currentEventIdx < m_events.size() &&
           m_events[m_currentEventIdx].timeOffsetSec <= targetSec) {
        ApplyEvent(m_events[m_currentEventIdx++], screen, parser);
    }

    m_currentTimeSec = targetSec;
    return true;
}

bool SessionReplayer::StepForward(double deltaSec, Screen& screen, VtParser& parser) {
    return Seek(m_currentTimeSec + deltaSec, screen, parser);
}

bool SessionReplayer::StepBackward(double deltaSec, Screen& screen, VtParser& parser) {
    return Seek(m_currentTimeSec - deltaSec, screen, parser);
}

bool SessionReplayer::Update(double dtSec, Screen& screen, VtParser& parser) {
    if (!m_playing || m_events.empty()) return false;

    m_currentTimeSec += dtSec * m_speed;
    bool touched = false;

    while (m_currentEventIdx < m_events.size() &&
           m_events[m_currentEventIdx].timeOffsetSec <= m_currentTimeSec) {
        ApplyEvent(m_events[m_currentEventIdx++], screen, parser);
        touched = true;
    }

    if (m_currentTimeSec >= m_totalDurationSec) {
        m_currentTimeSec = m_totalDurationSec;
        m_playing = false;
    }

    return touched;
}

} // namespace ft
