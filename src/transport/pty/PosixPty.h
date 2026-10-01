#pragma once
//
// FullTerminal — POSIX PTY Wrapper for Linux
// Implements openpty, termios, TIOCSWINSZ, and child process execution.
//

#include "core/Platform.h"
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>
#include <utility>

namespace ft {

class PosixPty {
public:
    PosixPty();
    ~PosixPty();
    PosixPty(const PosixPty&) = delete;
    PosixPty& operator=(const PosixPty&) = delete;

    static bool Available() { return true; }

    using OutputFn = std::function<void(const char*, size_t)>;
    using ExitFn   = std::function<void(DWORD)>;

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

    bool  Running()  const { return m_running.load(std::memory_order_acquire); }
    DWORD ExitCode() const { return m_exitCode.load(std::memory_order_relaxed); }
    bool  ProcessExited();

private:
    void ReaderLoop();

    int m_masterFd = -1;
    pid_t m_pid = -1;

    std::thread       m_reader;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_closing{ false };
    std::atomic<DWORD> m_exitCode{ 0 };
    bool              m_procExited = false;
};

} // namespace ft
