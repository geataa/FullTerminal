#include "services/KeyGenService.h"
#include "core/Utf8.h"
#include "core/Settings.h"

#include <windows.h>
#include <shlwapi.h>
#include <vector>

namespace ft {

std::wstring KeyGenService::FindSshKeygenExe() {
    wchar_t sysDir[MAX_PATH]{};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring path = std::wstring(sysDir) + L"\\OpenSSH\\ssh-keygen.exe";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;

    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"ssh-keygen.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
        return found;
    }
    return {};
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
        result.error = L"ssh-keygen.exe bulunamadi. OpenSSH kurulu olmali.";
        return result;
    }

    std::wstring outDir = targetDir;
    if (outDir.empty()) {
        outDir = PortableDataDir() + L"\\keys";
    }
    CreateDirectoryW(outDir.c_str(), nullptr);

    std::wstring name = keyName.empty() ? L"id_ed25519" : keyName;
    std::wstring keyPath = outDir + L"\\" + name;

    // Komut satiri
    std::wstring cmd = L"\"" + exe + L"\"";
    if (algo == KeyAlgorithm::Ed25519) {
        cmd += L" -t ed25519";
    } else if (algo == KeyAlgorithm::Rsa4096) {
        cmd += L" -t rsa -b 4096";
    } else if (algo == KeyAlgorithm::Ecdsa256) {
        cmd += L" -t ecdsa -b 256";
    }

    cmd += L" -f \"" + keyPath + L"\"";
    cmd += L" -N \"" + passphrase + L"\"";
    if (!comment.empty()) {
        cmd += L" -C \"" + comment + L"\"";
    } else {
        cmd += L" -C \"FullTerminal\"";
    }

    // Process olustur
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
        result.error = L"ssh-keygen sureci baslatilamadi (kod " + std::to_wstring(GetLastError()) + L").";
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

    result.success = true;
    result.privateKeyPath = keyPath;
    result.publicKeyPath = keyPath + L".pub";
    return result;
}

} // namespace ft
