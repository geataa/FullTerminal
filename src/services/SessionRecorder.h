#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>

namespace ft {

enum class RecordEventType : uint8_t {
    Output = 1, // Terminal çıktısı (Asciinema 'o')
    Input  = 2, // Kullanıcı/Ajan girdisi (Asciinema 'i')
    Resize = 3, // Ekran boyut değişimi (cols, rows)
    Marker = 4  // Denetim / semantik işaret (komut başlangıç/bitiş vb.)
};

struct RecordEvent {
    double timeOffsetSec = 0.0;     // Kayıt başlangıcından itibaren geçen saniye
    RecordEventType type = RecordEventType::Output;
    std::string data;               // Ham baytlar veya UTF-8 metin
    int cols = 0;                   // Resize için
    int rows = 0;                   // Resize için
};

struct RecordingHeader {
    uint32_t magic = 0x43525446;    // 'FTRC' ("FTREC")
    uint32_t version = 1;
    uint16_t cols = 80;
    uint16_t rows = 24;
    int64_t  startTimeUnixMs = 0;
    char     title[64] = {0};
    char     command[64] = {0};
};

class SessionRecorder {
public:
    SessionRecorder();
    ~SessionRecorder();

    // Kayıt başlat / durdur
    bool Start(const std::wstring& filePath, int initialCols, int initialRows,
               const std::string& title = "FullTerminal Session",
               const std::string& command = "");
    bool Stop();

    bool IsRecording() const { return m_recording; }

    // Olay kayıt metodları (I/O)
    void RecordOutput(const char* data, size_t len);
    void RecordInput(const char* data, size_t len);
    void RecordResize(int cols, int rows);
    void RecordMarker(const std::string& label);

    // Dışa aktarma ve dosya kaydı
    bool SaveBinary(const std::wstring& ftrecPath) const;
    bool SaveAsciinema(const std::wstring& castPath) const;

    // Durum ve istatistik sorguları
    double   DurationSeconds() const;
    size_t   EventCount() const;
    uint64_t TotalBytes() const { return m_totalBytes; }
    const std::wstring& FilePath() const { return m_filePath; }
    const std::vector<RecordEvent>& Events() const { return m_events; }
    int InitialCols() const { return m_header.cols; }
    int InitialRows() const { return m_header.rows; }

    static std::string EscapeJsonString(const std::string& raw);

private:
    double CurrentOffsetSeconds() const;

    mutable std::mutex m_mtx;
    bool m_recording = false;
    std::wstring m_filePath;
    std::chrono::steady_clock::time_point m_startTimePoint;
    RecordingHeader m_header{};
    std::vector<RecordEvent> m_events;
    uint64_t m_totalBytes = 0;
};

} // namespace ft
