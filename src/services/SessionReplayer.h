#pragma once

#include "services/SessionRecorder.h"
#include "vt/Screen.h"
#include "vt/VtParser.h"
#include <string>
#include <vector>
#include <memory>

namespace ft {

class SessionReplayer {
public:
    SessionReplayer();
    ~SessionReplayer();

    // Kayıt dosyasını yükle (.ftrec veya .cast)
    bool Load(const std::wstring& path);
    bool LoadBinary(const std::wstring& ftrecPath);
    bool LoadAsciinema(const std::wstring& castPath);

    // Oynatma kontrolleri
    void Play();
    void Pause();
    void TogglePlay();
    void Stop();
    void SetSpeed(float speed); // 0.25x, 0.5x, 1x, 2x, 4x vb.

    // Zamanda atlama / arama (Seek)
    bool Seek(double targetSec, Screen& screen, VtParser& parser);
    bool StepForward(double deltaSec, Screen& screen, VtParser& parser);
    bool StepBackward(double deltaSec, Screen& screen, VtParser& parser);

    // Frame güncelleme (her UI döngüsünde delta zaman ile çağrılır)
    bool Update(double dtSec, Screen& screen, VtParser& parser);

    // Durum sorguları
    bool    IsLoaded() const { return !m_events.empty(); }
    bool    IsPlaying() const { return m_playing; }
    bool    IsFinished() const { return m_currentTimeSec >= m_totalDurationSec && m_totalDurationSec > 0.0; }
    double  CurrentTime() const { return m_currentTimeSec; }
    double  TotalDuration() const { return m_totalDurationSec; }
    float   Progress() const;
    float   Speed() const { return m_speed; }
    size_t  CurrentEventIndex() const { return m_currentEventIdx; }
    size_t  EventCount() const { return m_events.size(); }
    int     InitialCols() const { return m_header.cols; }
    int     InitialRows() const { return m_header.rows; }
    std::string Title() const { return m_header.title; }
    std::string Command() const { return m_header.command; }

private:
    void ApplyEvent(const RecordEvent& ev, Screen& screen, VtParser& parser);

    RecordingHeader m_header{};
    std::vector<RecordEvent> m_events;
    size_t m_currentEventIdx = 0;
    double m_currentTimeSec = 0.0;
    double m_totalDurationSec = 0.0;
    float  m_speed = 1.0f;
    bool   m_playing = false;
};

} // namespace ft
