#include "services/KeyGenService.h"
#include "core/Utf8.h"
#include "core/Settings.h"

#include <vector>
#include <filesystem>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#include <shlwapi.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#endif

namespace ft {

std::wstring KeyGenService::FindSshKeygenExe() {
#if defined(_WIN32)
    wchar_t sysDir[MAX_PATH]{};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring path = std::wstring(sysDir) + L"\\OpenSSH\\ssh-keygen.exe";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;

    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"ssh-keygen.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
        return found;
    }
    return {};
#else
    for (const char* p : { "/usr/bin/ssh-keygen", "/bin/ssh-keygen", "/usr/local/bin/ssh-keygen" }) {
        if (std::filesystem::exists(p)) return Utf8ToWide(p);
    }
    return L"ssh-keygen";
#endif
}

KeyGenResult KeyGenService::GenerateKey(
    KeyAlgorithm algo,
    const std::wstring& keyName,
    const std::wstring& passphrase,
    const std::wstring& comment,
    const std::wstring& targetDir)
{
    KeyGenResult result;
    const std::wstring exe = FindSshKeygenExe();
    if (exe.empty()) {
        result.error = L"ssh-keygen bulunamadi. OpenSSH kurulu olmali.";
        return result;
    }

    std::wstring outDir = targetDir;
    wchar_t sep = (outDir.find(L'/') != std::wstring::npos) ? L'/' : L'\\';
    if (outDir.empty()) {
        outDir = PortableDataDir() + sep + L"keys";
    }
    std::filesystem::create_directories(WideToUtf8(outDir));

    std::wstring name = keyName.empty() ? L"id_ed25519" : keyName;
    std::wstring keyPath = outDir + sep + name;

    std::vector<std::string> args;
    args.push_back(WideToUtf8(exe));

    if (algo == KeyAlgorithm::Ed25519) {
        args.push_back("-t"); args.push_back("ed25519");
    } else if (algo == KeyAlgorithm::Rsa4096) {
        args.push_back("-t"); args.push_back("rsa");
        args.push_back("-b"); args.push_back("4096");
    } else if (algo == KeyAlgorithm::Ecdsa256) {
        args.push_back("-t"); args.push_back("ecdsa");
        args.push_back("-b"); args.push_back("256");
    }

    args.push_back("-f"); args.push_back(WideToUtf8(keyPath));
    args.push_back("-N"); args.push_back(WideToUtf8(passphrase));
    args.push_back("-C"); args.push_back(comment.empty() ? "FullTerminal" : WideToUtf8(comment));

#if defined(_WIN32)
    std::wstring cmd = L"\"" + exe + L"\"";
    for (size_t i = 1; i < args.size(); ++i) {
        cmd += L" \"" + Utf8ToWide(args[i]) + L"\"";
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hStdOutRead = nullptr, hStdOutWrite = nullptr;
    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) {
        result.error = L"Boru (pipe) olusturulamadi.";
        return result;
    }
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdOutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr, cmdBuf.data(),
        nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr,
        &si, &pi);

    CloseHandle(hStdOutWrite);

    if (!ok) {
        CloseHandle(hStdOutRead);
        result.error = L"ssh-keygen sureci baslatilamadi.";
        return result;
    }

    std::string output;
    char buffer[512];
    DWORD bytesRead = 0;
    while (ReadFile(hStdOutRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer, bytesRead);
    }
    CloseHandle(hStdOutRead);

    WaitForSingleObject(pi.hProcess, 10000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        result.error = L"ssh-keygen hata kodu: " + std::to_wstring(exitCode) + L"\n" + Utf8ToWide(output);
        return result;
    }
#else
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        result.error = L"pipe failed";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        result.error = L"fork failed";
        return result;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::vector<char*> c_args;
        for (auto& a : args) c_args.push_back(a.data());
        c_args.push_back(nullptr);

        execvp(c_args[0], c_args.data());
        _exit(127);
    }

    close(pipefd[1]);
    std::string output;
    char buf[512];
    ssize_t n = 0;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        output.append(buf, (size_t)n);
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

    if (exitCode != 0) {
        result.error = L"ssh-keygen error " + std::to_wstring(exitCode) + L": " + Utf8ToWide(output);
        return result;
    }

    chmod(WideToUtf8(keyPath).c_str(), 0600);
#endif

    result.success = true;
    result.privateKeyPath = keyPath;
    result.publicKeyPath = keyPath + L".pub";
    return result;
}

} // namespace ft
