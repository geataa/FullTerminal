#include "transport/pty/PosixPty.h"
#include "core/Utf8.h"

#include <pty.h>
#include <utmp.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <signal.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <sstream>

namespace ft {

PosixPty::PosixPty() = default;

PosixPty::~PosixPty() {
    Close();
}

bool PosixPty::Start(const std::wstring& commandLine,
                     const std::wstring& startDir,
                     const std::vector<std::pair<std::wstring, std::wstring>>& envOverrides,
                     short cols, short rows,
                     std::wstring* errorOut)
{
    Close();

    struct winsize ws{};
    ws.ws_col = cols > 0 ? cols : 80;
    ws.ws_row = rows > 0 ? rows : 24;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    int masterFd = -1, slaveFd = -1;
    if (openpty(&masterFd, &slaveFd, nullptr, nullptr, &ws) < 0) {
        if (errorOut) *errorOut = L"openpty failed: " + Utf8ToWide(strerror(errno));
        return false;
    }

    std::string cmd = WideToUtf8(commandLine);
    std::string dir = WideToUtf8(startDir);

    pid_t pid = fork();
    if (pid < 0) {
        close(masterFd);
        close(slaveFd);
        if (errorOut) *errorOut = L"fork failed: " + Utf8ToWide(strerror(errno));
        return false;
    }

    if (pid == 0) {
        // --- Child Process ---
        close(masterFd);
        login_tty(slaveFd);

        if (!dir.empty()) {
            chdir(dir.c_str());
        }

        // Set default terminal environment
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);

        for (const auto& [k, v] : envOverrides) {
            setenv(WideToUtf8(k).c_str(), WideToUtf8(v).c_str(), 1);
        }

        // Determine shell or command to execute
        if (cmd.empty()) {
            const char* shell = getenv("SHELL");
            if (!shell || shell[0] == '\0') shell = "/bin/bash";
            execlp(shell, shell, "-l", nullptr);
            _exit(127);
        } else {
            // Check if cmd has flags or run via sh -c or parse arguments
            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            _exit(127);
        }
    }

    // --- Parent Process ---
    close(slaveFd);
    m_masterFd = masterFd;
    m_pid = pid;
    m_running.store(true, std::memory_order_release);
    m_closing.store(false, std::memory_order_release);
    m_procExited = false;
    m_exitCode.store(0, std::memory_order_relaxed);

    m_reader = std::thread(&PosixPty::ReaderLoop, this);
    return true;
}

void PosixPty::ReaderLoop() {
    char buf[16384];
    struct pollfd pfd{};
    pfd.fd = m_masterFd;
    pfd.events = POLLIN;

    while (m_running.load(std::memory_order_acquire)) {
        int r = poll(&pfd, 1, 100);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) {
            // Check if process exited
            if (ProcessExited()) break;
            continue;
        }

        if (pfd.revents & (POLLIN | POLLPRI)) {
            ssize_t n = read(m_masterFd, buf, sizeof(buf));
            if (n > 0) {
                if (onOutput) {
                    onOutput(buf, (size_t)n);
                }
            } else if (n <= 0) {
                break;
            }
        }

        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }
    }

    ProcessExited();

    if (onExit) {
        onExit(m_exitCode.load(std::memory_order_relaxed));
    }
}

void PosixPty::Write(const char* data, size_t len) {
    if (m_masterFd < 0 || !data || len == 0) return;
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(m_masterFd, data + written, len - written);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            break;
        }
        written += (size_t)n;
    }
}

void PosixPty::Resize(short cols, short rows) {
    if (m_masterFd < 0) return;
    struct winsize ws{};
    ws.ws_col = cols > 0 ? cols : 80;
    ws.ws_row = rows > 0 ? rows : 24;
    ioctl(m_masterFd, TIOCSWINSZ, &ws);
}

void PosixPty::Close() {
    bool expected = true;
    if (m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        m_closing.store(true, std::memory_order_release);

        if (m_masterFd >= 0) {
            close(m_masterFd);
            m_masterFd = -1;
        }

        if (m_pid > 0) {
            kill(m_pid, SIGHUP);
            kill(m_pid, SIGTERM);
        }

        if (m_reader.joinable()) {
            m_reader.join();
        }

        if (m_pid > 0) {
            int status = 0;
            waitpid(m_pid, &status, WNOHANG);
            m_pid = -1;
        }
    }
}

bool PosixPty::ProcessExited() {
    if (m_procExited) return true;
    if (m_pid <= 0) return true;

    int status = 0;
    pid_t res = waitpid(m_pid, &status, WNOHANG);
    if (res == m_pid) {
        m_procExited = true;
        if (WIFEXITED(status)) {
            m_exitCode.store((DWORD)WEXITSTATUS(status), std::memory_order_relaxed);
        } else if (WIFSIGNALED(status)) {
            m_exitCode.store((DWORD)(128 + WTERMSIG(status)), std::memory_order_relaxed);
        }
        return true;
    }
    return false;
}

} // namespace ft
