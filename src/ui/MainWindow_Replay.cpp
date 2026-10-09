#include "ui/MainWindowInternal.h"
#include <commdlg.h>
#include <filesystem>

namespace ft {
bool MainWindow::OpenRecording(const std::wstring& path) {
    if (!m_replay.Load(path)) {
        Toast(TrText(L"Recording is invalid, incomplete or too large.", L"Kayıt geçersiz, eksik veya çok büyük."));
        return false;
    }
    m_replayPath = path;
    m_replay.Seek(0, m_replayScreen, m_replayParser);
    m_replay.Play(); m_replayTick = NowTicks();
    m_view = View::Replay; m_ui.SetFocus(0); m_dirty = true;
    return true;
}
void MainWindow::PickRecording() {
    wchar_t path[32768]{};
    OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = m_hwnd;
    ofn.lpstrFile = path; ofn.nMaxFile = 32768;
    ofn.lpstrFilter = L"Terminal recordings\0*.ftrec;*.cast\0\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) OpenRecording(path);
}
void MainWindow::DrawReplayScreen(const D2D1_RECT_F& a) {
    const float s = m_lay.scale, x = a.left + 12 * s, y = a.top + 12 * s;
    m_r.PushClip(a);
    auto label = std::filesystem::path(m_replayPath).filename().wstring();
    m_r.Text(label, D2D1::RectF(x, y, a.right - 120 * s, y + 24 * s), theme::TextHi, 14 * s);
    if (m_ui.Button(14500, D2D1::RectF(a.right - 108 * s, y, a.right - 12 * s, y + 26 * s), TrText(L"Back", L"Geri"))) {
        m_replay.Pause(); m_view = View::Logs; m_logsTab = 1;
    }
    float bx = x;
    auto button = [&](int id, const wchar_t* text, float width) {
        auto r = D2D1::RectF(bx, y + 34 * s, bx + width * s, y + 62 * s);
        bx = r.right + 6 * s; return m_ui.Button(id, r, text);
    };
    if (button(14501, m_replay.IsPlaying() ? TrText(L"Pause", L"Duraklat") : TrText(L"Play", L"Oynat"), 80)) m_replay.TogglePlay();
    if (button(14502, L"|<", 38)) { m_replay.Stop(); m_replay.Seek(0, m_replayScreen, m_replayParser); }
    if (button(14503, L"-5s", 44)) m_replay.StepBackward(5, m_replayScreen, m_replayParser);
    if (button(14504, L"+5s", 44)) m_replay.StepForward(5, m_replayScreen, m_replayParser);
    const float speeds[] = {0.25f, 0.5f, 1.f, 2.f, 4.f};
    int speed = 2;
    for (int i = 0; i < 5; ++i) if (speeds[i] == m_replay.Speed()) speed = i;
    if (m_ui.Choice(14505, D2D1::RectF(bx, y + 34 * s, bx + 220 * s, y + 62 * s),
        {L"0.25x", L"0.5x", L"1x", L"2x", L"4x"}, speed)) m_replay.SetSpeed(speeds[speed]);
    auto track = D2D1::RectF(x, y + 73 * s, a.right - 12 * s, y + 91 * s);
    m_r.FillRound(track, 4 * s, theme::Elevated);
    m_r.FillRound(D2D1::RectF(track.left, track.top, track.left + (track.right - track.left) * m_replay.Progress(), track.bottom), 4 * s, theme::Ac());
    if (m_ui.Clicked(track)) m_replay.Seek(m_replay.TotalDuration() * std::clamp((m_in.mx - track.left) / std::max(1.f, track.right - track.left), 0.f, 1.f), m_replayScreen, m_replayParser);
    wchar_t time[100]; swprintf_s(time, L"%.1fs / %.1fs  |  %dx%d", m_replay.CurrentTime(), m_replay.TotalDuration(), m_replayScreen.Cols(), m_replayScreen.Rows());
    m_r.Text(time, D2D1::RectF(x, y + 96 * s, a.right, y + 118 * s), theme::TextMuted, 11 * s);
    auto viewport = D2D1::RectF(x, y + 122 * s, a.right - 12 * s, a.bottom - 12 * s);
    if (viewport.bottom > viewport.top) {
        m_r.PushClip(viewport);
        m_r.DrawRecording(m_replayScreen, viewport);
        m_r.PopClip();
    }
    m_r.PopClip();
}
} // namespace ft
