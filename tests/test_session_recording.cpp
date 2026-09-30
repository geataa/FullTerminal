#include "services/SessionRecorder.h"
#include "services/SessionReplayer.h"
#include "vt/Screen.h"
#include "vt/VtParser.h"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <thread>

using namespace ft;

void TestSessionRecorder() {
    std::cout << "[TEST] 1. SessionRecorder (.ftrec & .cast)...\n";
    std::wstring testFtrec = L"bin\\test_session.ftrec";
    std::wstring testCast = L"bin\\test_session.cast";

    std::filesystem::remove(testFtrec);
    std::filesystem::remove(testCast);

    SessionRecorder recorder;
    bool ok = recorder.Start(testFtrec, 80, 24, "Test Audit Session", "pwsh.exe");
    assert(ok);
    assert(recorder.IsRecording());

    // Olayları simüle et
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    recorder.RecordOutput("\x1b[32mHello from FullTerminal\x1b[0m\r\n", 37);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    recorder.RecordInput("cargo test\r", 11);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    recorder.RecordResize(100, 30);

    recorder.RecordMarker("audit:command_started");

    assert(recorder.EventCount() == 4);
    assert(recorder.TotalBytes() > 0);

    recorder.Stop();
    assert(!recorder.IsRecording());

    assert(std::filesystem::exists(testFtrec));
    assert(std::filesystem::exists(testCast));
    std::cout << "  ✓ Binary .ftrec and JSONL .cast recordings generated successfully.\n";
}

void TestSessionReplayerBinary() {
    std::cout << "[TEST] 2. SessionReplayer from .ftrec...\n";
    std::wstring testFtrec = L"bin\\test_session.ftrec";

    SessionReplayer replayer;
    bool ok = replayer.Load(testFtrec);
    assert(ok);
    assert(replayer.IsLoaded());
    assert(replayer.EventCount() == 4);
    assert(replayer.InitialCols() == 80);
    assert(replayer.InitialRows() == 24);

    Screen s;
    s.Resize(80, 24);
    VtParser parser(s);

    // Başlangıçta oynatılmamış
    assert(!replayer.IsPlaying());
    assert(replayer.Progress() == 0.0f);

    // Hedefe seek yap
    replayer.Seek(1.0, s, parser);
    assert(replayer.CurrentEventIndex() == 4);

    std::cout << "  ✓ Binary .ftrec successfully parsed, loaded, and replayed into Screen.\n";
}

void TestSessionReplayerAsciinema() {
    std::cout << "[TEST] 3. SessionReplayer from Asciinema .cast...\n";
    std::wstring testCast = L"bin\\test_session.cast";

    SessionReplayer replayer;
    bool ok = replayer.Load(testCast);
    assert(ok);
    assert(replayer.IsLoaded());
    assert(replayer.EventCount() == 4);

    Screen s;
    s.Resize(80, 24);
    VtParser parser(s);

    // Oynatma güncelleme simülasyonu
    replayer.Play();
    assert(replayer.IsPlaying());

    // 0.5 saniyelik adım
    replayer.Update(0.5, s, parser);
    assert(replayer.CurrentEventIndex() > 0);

    // Hız değiştirme
    replayer.SetSpeed(2.0f);
    assert(replayer.Speed() == 2.0f);

    std::cout << "  ✓ Asciinema v2 .cast JSONL successfully parsed and step-replayed.\n";
}

int main() {
    std::cout << "=====================================================\n";
    std::cout << "FullTerminal Session Recording & Replay Unit Tests\n";
    std::cout << "=====================================================\n";

    try {
        TestSessionRecorder();
        TestSessionReplayerBinary();
        TestSessionReplayerAsciinema();
        std::cout << "\n>>> ALL SESSION RECORDING TESTS PASSED (3/3) <<<\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
