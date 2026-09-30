#include "services/SessionRecorder.h"
#include <iomanip>
#include <sstream>
#include <cstring>

namespace ft {

SessionRecorder::SessionRecorder() = default;

SessionRecorder::~SessionRecorder() {
    if (m_recording) {
        Stop();
    }
}

bool SessionRecorder::Start(const std::wstring& filePath, int initialCols, int initialRows,
                            const std::string& title, const std::string& command) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_filePath = filePath;
    m_recording = true;
    m_events.clear();
    m_totalBytes = 0;

    m_header.magic = 0x43525446; // 'FTRC'
    m_header.version = 1;
    m_header.cols = static_cast<uint16_t>(initialCols > 0 ? initialCols : 80);
    m_header.rows = static_cast<uint16_t>(initialRows > 0 ? initialRows : 24);

    auto now = std::chrono::system_clock::now();
    m_header.startTimeUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    strncpy_s(m_header.title, sizeof(m_header.title), title.c_str(), _TRUNCATE);
    strncpy_s(m_header.command, sizeof(m_header.command), command.c_str(), _TRUNCATE);

    m_startTimePoint = std::chrono::steady_clock::now();
    return true;
}

bool SessionRecorder::Stop() {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (!m_recording) return false;
    m_recording = false;

    if (!m_filePath.empty()) {
        SaveBinary(m_filePath);
        // Otomatik asciinema .cast dosyasını da yanına çıkar
        std::wstring castPath = m_filePath;
        size_t dot = castPath.rfind(L'.');
        if (dot != std::wstring::npos) {
            castPath = castPath.substr(0, dot) + L".cast";
        } else {
            castPath += L".cast";
        }
        SaveAsciinema(castPath);
    }
    return true;
}

double SessionRecorder::CurrentOffsetSeconds() const {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = now - m_startTimePoint;
    return diff.count();
}

void SessionRecorder::RecordOutput(const char* data, size_t len) {
    if (!m_recording || !data || len == 0) return;
    std::lock_guard<std::mutex> lock(m_mtx);

    RecordEvent ev;
    ev.timeOffsetSec = CurrentOffsetSeconds();
    ev.type = RecordEventType::Output;
    ev.data.assign(data, len);
    m_events.push_back(std::move(ev));
    m_totalBytes += len;
}

void SessionRecorder::RecordInput(const char* data, size_t len) {
    if (!m_recording || !data || len == 0) return;
    std::lock_guard<std::mutex> lock(m_mtx);

    RecordEvent ev;
    ev.timeOffsetSec = CurrentOffsetSeconds();
    ev.type = RecordEventType::Input;
    ev.data.assign(data, len);
    m_events.push_back(std::move(ev));
    m_totalBytes += len;
}

void SessionRecorder::RecordResize(int cols, int rows) {
    if (!m_recording) return;
    std::lock_guard<std::mutex> lock(m_mtx);

    RecordEvent ev;
    ev.timeOffsetSec = CurrentOffsetSeconds();
    ev.type = RecordEventType::Resize;
    ev.cols = cols;
    ev.rows = rows;
    m_events.push_back(std::move(ev));
}

void SessionRecorder::RecordMarker(const std::string& label) {
    if (!m_recording) return;
    std::lock_guard<std::mutex> lock(m_mtx);

    RecordEvent ev;
    ev.timeOffsetSec = CurrentOffsetSeconds();
    ev.type = RecordEventType::Marker;
    ev.data = label;
    m_events.push_back(std::move(ev));
}

double SessionRecorder::DurationSeconds() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_events.empty()) return 0.0;
    return m_events.back().timeOffsetSec;
}

size_t SessionRecorder::EventCount() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_events.size();
}

bool SessionRecorder::SaveBinary(const std::wstring& ftrecPath) const {
    std::ofstream out(ftrecPath, std::ios::binary);
    if (!out.is_open()) return false;

    // 1. Header yaz
    out.write(reinterpret_cast<const char*>(&m_header), sizeof(m_header));

    // 2. Olayları yaz
    for (const auto& ev : m_events) {
        uint32_t deltaMs = static_cast<uint32_t>(ev.timeOffsetSec * 1000.0);
        uint8_t type = static_cast<uint8_t>(ev.type);
        out.write(reinterpret_cast<const char*>(&deltaMs), sizeof(deltaMs));
        out.write(reinterpret_cast<const char*>(&type), sizeof(type));

        if (ev.type == RecordEventType::Resize) {
            uint32_t len = 4;
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            uint16_t c = static_cast<uint16_t>(ev.cols);
            uint16_t r = static_cast<uint16_t>(ev.rows);
            out.write(reinterpret_cast<const char*>(&c), sizeof(c));
            out.write(reinterpret_cast<const char*>(&r), sizeof(r));
        } else {
            uint32_t len = static_cast<uint32_t>(ev.data.size());
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            if (len > 0) {
                out.write(ev.data.data(), len);
            }
        }
    }
    return true;
}

std::string SessionRecorder::EscapeJsonString(const std::string& raw) {
    std::ostringstream ss;
    for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        switch (c) {
        case '"':  ss << "\\\""; break;
        case '\\': ss << "\\\\"; break;
        case '\b': ss << "\\b";  break;
        case '\f': ss << "\\f";  break;
        case '\n': ss << "\\n";  break;
        case '\r': ss << "\\r";  break;
        case '\t': ss << "\\t";  break;
        default:
            if (c < 0x20) {
                ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
            } else {
                ss << c;
            }
            break;
        }
    }
    return ss.str();
}

bool SessionRecorder::SaveAsciinema(const std::wstring& castPath) const {
    std::ofstream out(castPath);
    if (!out.is_open()) return false;

    // Asciinema v2 header
    int64_t startSec = m_header.startTimeUnixMs / 1000;
    out << "{\"version\": 2, \"width\": " << m_header.cols
        << ", \"height\": " << m_header.rows
        << ", \"timestamp\": " << startSec
        << ", \"title\": \"" << EscapeJsonString(m_header.title) << "\""
        << ", \"env\": {\"SHELL\": \"" << EscapeJsonString(m_header.command) << "\", \"TERM\": \"xterm-256color\"}}\n";

    // Olay satırları (JSON array)
    out << std::fixed << std::setprecision(6);
    for (const auto& ev : m_events) {
        if (ev.type == RecordEventType::Output) {
            out << "[" << ev.timeOffsetSec << ", \"o\", \"" << EscapeJsonString(ev.data) << "\"]\n";
        } else if (ev.type == RecordEventType::Input) {
            out << "[" << ev.timeOffsetSec << ", \"i\", \"" << EscapeJsonString(ev.data) << "\"]\n";
        } else if (ev.type == RecordEventType::Resize) {
            out << "[" << ev.timeOffsetSec << ", \"r\", \"" << ev.cols << "x" << ev.rows << "\"]\n";
        } else if (ev.type == RecordEventType::Marker) {
            out << "[" << ev.timeOffsetSec << ", \"m\", \"" << EscapeJsonString(ev.data) << "\"]\n";
        }
    }
    return true;
}

} // namespace ft
