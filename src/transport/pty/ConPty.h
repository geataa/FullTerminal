#pragma once
//
// ConPTY sarmalayicisi (Windows 10 1809+).
//
// CreatePseudoConsole ailesi kernel32'den GetProcAddress ile cozuluyor.
// Statik import edilseydi eski Windows'ta exe hic acilmazdi; boylece net bir
// hata mesaji verebiliyoruz.
//
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>
#include <utility>

namespace ft {

class ConPty {
public:
    ConPty() = default;
    ~ConPty();
    ConPty(const ConPty&) = delete;
    ConPty& operator=(const ConPty&) = delete;

    // Bu Windows surumu ConPTY destekliyor mu.
    static bool Available();

    using OutputFn = std::function<void(const char*, size_t)>;
    using ExitFn   = std::function<void(DWORD)>;

    // onOutput ve onExit okuyucu is parcacigindan cagrilir.
    // Cagiran taraf kendi senkronizasyonundan sorumlu. Okuyucu calisirken
    // degistirilmemeli: once Close() (okuyucuyu birlestirir), sonra sifirla.
    OutputFn onOutput;
    ExitFn   onExit;

    bool Start(const std::wstring& commandLine,
               const std::wstring& startDir,
               const std::vector<std::pair<std::wstring, std::wstring>>& envOverrides,
               short cols, short rows,
               std::wstring* errorOut = nullptr);

    void Write(const char* data, size_t len);
    void Resize(short cols, short rows);
    void Close();

    // Okuyucu calisiyor mu. false ise onOutput'a verilecek her sey verilmistir.
    bool  Running()  const { return m_running.load(std::memory_order_acquire); }
    DWORD ExitCode() const { return m_exitCode.load(std::memory_order_relaxed); }

    // Istemci sureci bitti mi (ana is parcacigindan yoklanir, beklemez).
    // ConPTY cikis borusunu ClosePseudoConsole'a kadar acik tutabildigi icin
    // okuyucunun bitmesini beklemek yetmez; surec handle'ina bakariz.
    bool ProcessExited();

private:
    void ReaderLoop();

    HPCON  m_hPC     = nullptr;
    HANDLE m_inWrite = nullptr;
    HANDLE m_outRead = nullptr;
    HANDLE m_process = nullptr;
    HANDLE m_thread  = nullptr;

    std::thread       m_reader;
    std::atomic<bool>  m_running{ false };
    std::atomic<bool>  m_closing{ false };
    std::atomic<DWORD> m_exitCode{ 0 };
    bool               m_procExited = false;
};

} // namespace ft
