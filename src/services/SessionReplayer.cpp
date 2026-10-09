#include "services/SessionReplayer.h"
#include "mcp/Json.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace ft {
namespace {
constexpr size_t MaxBytes = 256 * 1024 * 1024, MaxEvent = 4 * 1024 * 1024, MaxEvents = 1000000;
bool Dimensions(int cols, int rows) { return cols > 0 && rows > 0 && cols <= 1000 && rows <= 1000; }
bool SmallFile(const std::wstring& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    return !ec && size <= MaxBytes;
}
}
SessionReplayer::SessionReplayer() = default;
SessionReplayer::~SessionReplayer() = default;
void SessionReplayer::Clear() {
    m_header = {}; m_events.clear(); m_currentEventIdx = 0;
    m_currentTimeSec = m_totalDurationSec = 0; m_playing = m_loaded = false; m_needsReset = true;
}
bool SessionReplayer::Load(const std::wstring& path) {
    auto ext = std::filesystem::path(path).extension().wstring();
    return _wcsicmp(ext.c_str(), L".cast") == 0 ? LoadAsciinema(path) : LoadBinary(path);
}
bool SessionReplayer::LoadBinary(const std::wstring& path) {
    Clear();
    if (!SmallFile(path)) return false;
    std::ifstream in(path, std::ios::binary);
    RecordingHeader h{};
    if (!in.read(reinterpret_cast<char*>(&h), sizeof(h)) || h.magic != 0x43525446 || h.version != 1 ||
        !Dimensions(h.cols, h.rows)) return false;
    h.title[sizeof(h.title) - 1] = 0; h.command[sizeof(h.command) - 1] = 0;
    std::vector<RecordEvent> events;
    double previous = 0;
    while (in.peek() != EOF) {
        uint32_t ms = 0, len = 0; uint8_t type = 0;
        if (!in.read(reinterpret_cast<char*>(&ms), 4) || !in.read(reinterpret_cast<char*>(&type), 1) ||
            !in.read(reinterpret_cast<char*>(&len), 4) || len > MaxEvent || events.size() >= MaxEvents) return false;
        if (type > static_cast<uint8_t>(RecordEventType::Marker) ||
            type < static_cast<uint8_t>(RecordEventType::Output)) return false;
        RecordEvent ev; ev.timeOffsetSec = ms / 1000.0; ev.type = static_cast<RecordEventType>(type);
        if (ev.timeOffsetSec < previous) return false;
        previous = ev.timeOffsetSec;
        if (ev.type == RecordEventType::Resize) {
            uint16_t dims[2]{};
            if (len != 4 || !in.read(reinterpret_cast<char*>(dims), 4) || !Dimensions(dims[0], dims[1])) return false;
            ev.cols = dims[0]; ev.rows = dims[1];
        } else {
            ev.data.resize(len);
            if (len && !in.read(ev.data.data(), len)) return false;
        }
        events.push_back(std::move(ev));
    }
    m_header = h; m_events = std::move(events); m_totalDurationSec = previous; m_loaded = true;
    return true;
}
bool SessionReplayer::LoadAsciinema(const std::wstring& path) {
    Clear();
    if (!SmallFile(path)) return false;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    if (!std::getline(in, line) || line.size() > MaxEvent) return false;
    bool ok = false;
    auto header = json::Value::parse(line, &ok);
    int64_t cols = header["width"].as_int(), rows = header["height"].as_int();
    if (!ok || header["version"].as_int() != 2 || cols < 1 || rows < 1 || cols > 1000 || rows > 1000 ||
        !Dimensions(static_cast<int>(cols), static_cast<int>(rows))) return false;
    RecordingHeader h{}; h.cols = static_cast<uint16_t>(cols); h.rows = static_cast<uint16_t>(rows);
    strncpy_s(h.title, header["title"].as_string().c_str(), _TRUNCATE);
    strncpy_s(h.command, header["command"].as_string().c_str(), _TRUNCATE);
    std::vector<RecordEvent> events;
    double previous = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line == "\r") continue;
        if (line.size() > MaxEvent || events.size() >= MaxEvents) return false;
        auto val = json::Value::parse(line, &ok);
        if (!ok || !val.is_array() || val.size() != 3 || !val[0].is_number() || !val[1].is_string() || !val[2].is_string()) return false;
        RecordEvent ev; ev.timeOffsetSec = val[0].as_double();
        if (!std::isfinite(ev.timeOffsetSec) || ev.timeOffsetSec < previous) return false;
        previous = ev.timeOffsetSec;
        const auto type = val[1].as_string();
        if (type == "o") ev.type = RecordEventType::Output;
        else if (type == "i") ev.type = RecordEventType::Input;
        else if (type == "m") ev.type = RecordEventType::Marker;
        else if (type == "r") ev.type = RecordEventType::Resize;
        else continue; // Future event types are allowed by asciinema.
        ev.data = val[2].as_string();
        if (ev.type == RecordEventType::Resize) {
            char extra;
            if (sscanf_s(ev.data.c_str(), "%dx%d%c", &ev.cols, &ev.rows, &extra, 1u) != 2 || !Dimensions(ev.cols, ev.rows)) return false;
        }
        events.push_back(std::move(ev));
    }
    m_header = h; m_events = std::move(events); m_totalDurationSec = previous; m_loaded = true;
    return true;
}
void SessionReplayer::Play() {
    if (!m_loaded) return;
    if (IsFinished()) { m_currentTimeSec = 0; m_currentEventIdx = 0; m_needsReset = true; }
    m_playing = true;
}
void SessionReplayer::Pause() { m_playing = false; }
void SessionReplayer::TogglePlay() { if (m_playing) Pause(); else Play(); }
void SessionReplayer::Stop() { m_playing = false; m_currentTimeSec = 0; m_currentEventIdx = 0; m_needsReset = true; }
void SessionReplayer::SetSpeed(float speed) { if (std::isfinite(speed)) m_speed = std::clamp(speed, 0.25f, 4.0f); }
float SessionReplayer::Progress() const {
    return m_totalDurationSec > 0 ? static_cast<float>(m_currentTimeSec / m_totalDurationSec) : (IsFinished() ? 1.0f : 0.0f);
}
void SessionReplayer::ApplyEvent(const RecordEvent& ev, Screen& screen, VtParser& parser) {
    if (ev.type == RecordEventType::Output) parser.Feed(ev.data.data(), ev.data.size());
    else if (ev.type == RecordEventType::Resize) screen.Resize(ev.cols, ev.rows);
}
bool SessionReplayer::Seek(double target, Screen& screen, VtParser& parser) {
    if (!m_loaded || !std::isfinite(target)) return false;
    target = std::clamp(target, 0.0, m_totalDurationSec);
    if (m_needsReset || target < m_currentTimeSec) {
        screen.Reset(); screen.Resize(m_header.cols, m_header.rows); parser.Reset();
        m_currentEventIdx = 0; m_needsReset = false;
    }
    while (m_currentEventIdx < m_events.size() && m_events[m_currentEventIdx].timeOffsetSec <= target)
        ApplyEvent(m_events[m_currentEventIdx++], screen, parser);
    m_currentTimeSec = target;
    return true;
}
bool SessionReplayer::StepForward(double seconds, Screen& s, VtParser& p) { return Seek(m_currentTimeSec + seconds, s, p); }
bool SessionReplayer::StepBackward(double seconds, Screen& s, VtParser& p) { return Seek(m_currentTimeSec - seconds, s, p); }
bool SessionReplayer::Update(double dt, Screen& screen, VtParser& parser) {
    if (!m_playing || !std::isfinite(dt) || dt < 0) return false;
    Seek(m_currentTimeSec + dt * m_speed, screen, parser);
    if (m_currentTimeSec >= m_totalDurationSec) m_playing = false;
    return true;
}
} // namespace ft
