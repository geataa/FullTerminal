#include "model/SftpModel.h"
#include "transport/pty/ConPty.h"
#include "core/Utf8.h"
#include "core/Settings.h"
#include "core/ShellProfiles.h"
#include "core/I18n.h"
#include "vt/VtParser.h"

#include <windows.h>
#include <shlwapi.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <stdexcept>

namespace ft {

namespace {

// Dosya boyutunu "66.52 KB", "109.56 KB", "190.00 Bytes" seklinde bicimlendirir
std::wstring FormatBytes(uint64_t bytes) {
    wchar_t buf[64];
    if (bytes < 1024) {
        swprintf_s(buf, L"%.2f Bytes", (double)bytes);
    } else if (bytes < 1024 * 1024) {
        swprintf_s(buf, L"%.2f KB", (double)bytes / 1024.0);
    } else if (bytes < 1024ULL * 1024 * 1024) {
        swprintf_s(buf, L"%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        swprintf_s(buf, L"%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
    return buf;
}

std::wstring TrimWs(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}

// Terminal-control stripping is only for diagnostics; consume exactly one CSI final.
std::string StripAnsi(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = text[i];
        if (c == 0x1b && i + 1 < text.size()) {
            char kind = text[++i];
            if (kind == '[') {
                while (i + 1 < text.size()) { c = text[++i]; if (c >= 0x40 && c <= 0x7e) break; }
            } else if (kind == ']' || kind == 'P') {
                while (i + 1 < text.size()) {
                    c = text[++i];
                    if (c == 7) break;
                    if (c == 0x1b && i + 1 < text.size() && text[i+1] == '\\') { ++i; break; }
                }
            }
        } else if (c != '\r') out += (char)c;
    }
    return out;
}
std::string QuoteSftp(const std::wstring& path, bool glob = false) {
    std::string result = "\"";
    for (char c : WideToUtf8(path)) {
        if (c == '\r' || c == '\n' || c == '\0') throw std::invalid_argument("SFTP paths cannot contain line breaks");
        if (glob && (c == '*' || c == '?')) throw std::invalid_argument("Windows OpenSSH cannot safely address literal wildcard filenames");
        if (c == '\\' || c == '\"') result += '\\';
        result += c;
    }
    return result + "\"";
}

std::string SftpError(const std::string& output) {
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        // A filename (or an echoed command containing one) is not an error.
        if (line.starts_with("sftp>") || line.starts_with("Remote working directory:") ||
            line.starts_with("Fetching ") || line.starts_with("Uploading ") ||
            (line.size() >= 10 && std::string("-dlbcps").find(line[0]) != std::string::npos &&
             (line[1] == 'r' || line[1] == '-'))) continue;
        std::string lower = line;
        for (auto& c : lower) c = (char)tolower((unsigned char)c);
        if (lower.starts_with("couldn't ") || lower.starts_with("cannot ") ||
            lower.starts_with("can't ") || lower.starts_with("invalid command") ||
            lower.starts_with("fatal:") || lower.starts_with("permission denied") ||
            lower.starts_with("connection refused") || lower.starts_with("file \"") ||
            lower.find(": no such file") != std::string::npos ||
            lower.find(": permission denied") != std::string::npos ||
            lower.find(": failure") != std::string::npos ||
            lower.find(": not a directory") != std::string::npos) return line;
    }
    return {};
}

// Tarihi "9/24/2026, 12:05 AM" seklinde bicimlendirir
std::wstring FormatFileTime(const FILETIME& ft) {
    SYSTEMTIME stUTC, stLocal;
    FileTimeToSystemTime(&ft, &stUTC);
    SystemTimeToTzSpecificLocalTime(nullptr, &stUTC, &stLocal);

    int hour = stLocal.wHour;
    const wchar_t* ampm = (hour >= 12) ? L"PM" : L"AM";
    if (hour == 0) hour = 12;
    else if (hour > 12) hour -= 12;

    wchar_t buf[64];
    swprintf_s(buf, L"%d/%d/%d, %d:%02d %s",
              stLocal.wMonth, stLocal.wDay, stLocal.wYear,
              hour, stLocal.wMinute, ampm);
    return buf;
}

} // namespace

// ------------------------------------------------------------- LocalFileSystem

std::wstring LocalFileSystem::DefaultPath() {
    wchar_t buf[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, buf);
    if (buf[0] != L'\0') return buf;
    return L"C:\\";
}

std::wstring LocalFileSystem::FormatFileSize(uint64_t bytes) {
    return FormatBytes(bytes);
}

std::wstring LocalFileSystem::GetFileKind(const std::wstring& name, bool isDir) {
    if (isDir) return L"folder";
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos && dot + 1 < name.size()) {
        std::wstring ext = name.substr(dot + 1);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        return ext;
    }
    return L"file";
}

std::vector<std::wstring> LocalFileSystem::GetDrives() {
    std::vector<std::wstring> drives;
    wchar_t buf[512]{};
    DWORD len = GetLogicalDriveStringsW(512, buf);
    if (len > 0 && len < 512) {
        wchar_t* p = buf;
        while (*p) {
            drives.push_back(p);
            p += wcslen(p) + 1;
        }
    }
    if (drives.empty()) drives.push_back(L"C:\\");
    return drives;
}

std::vector<FileItem> LocalFileSystem::List(const std::wstring& path, std::wstring* err) {
    std::vector<FileItem> items;
    if (err) err->clear();
    std::wstring p = path.empty() ? DefaultPath() : path;
    const DWORD attributes = GetFileAttributesW(p.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        if (err) *err = L"Directory is unavailable: " + p;
        return items;
    }

    while (p.size() > 1 && (p.back() == L'\\' || p.back() == L'/')) {
        if (p.size() == 3 && p[1] == L':') break; // "C:\" korunur
        p.pop_back();
    }

    std::wstring pattern = p;
    if (pattern.back() != L'\\') pattern += L"\\*";
    else pattern += L"*";

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND && err) *err = L"Dizin acilamadi: " + path;
        return items;
    }

    do {
        if (wcscmp(fd.cFileName, L".") == 0) continue;
        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (wcscmp(fd.cFileName, L"..") == 0) {
            // Ust dizin elemani
            FileItem up;
            up.name = L"..";
            up.path = p;
            up.isDir = true;
            up.kind = L"folder";
            up.modified = L"--";
            items.insert(items.begin(), up);
            continue;
        }

        FileItem item;
        item.name = fd.cFileName;
        item.path = p + (p.back() == L'\\' ? L"" : L"\\") + item.name;
        item.isDir = isDir;
        item.kind = GetFileKind(item.name, isDir);
        item.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        item.modified = FormatFileTime(fd.ftLastWriteTime);

        items.push_back(item);
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    // Siralama: ".." en basta, sonra klasorler, sonra dosyalar alfabetik
    std::stable_sort(items.begin(), items.end(), [](const FileItem& a, const FileItem& b) {
        if (a.name == L"..") return b.name != L"..";
        if (b.name == L"..") return false;
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    return items;
}

bool LocalFileSystem::MakeDirectory(const std::wstring& path, std::wstring* err) {
    if (::CreateDirectoryW(path.c_str(), nullptr)) return true;
    DWORD gle = GetLastError();
    if (gle == ERROR_ALREADY_EXISTS && (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY)) return true;
    if (err) *err = L"Klasor olusturulamadi (hata " + std::to_wstring(gle) + L")";
    return false;
}

bool LocalFileSystem::DeleteItem(const std::wstring& path, bool isDir, std::wstring* err) {
    if (isDir) {
        if (::RemoveDirectoryW(path.c_str())) return true;
    } else {
        if (::DeleteFileW(path.c_str())) return true;
    }
    if (err) *err = L"Silinemedi (hata " + std::to_wstring(GetLastError()) + L")";
    return false;
}

bool LocalFileSystem::Rename(const std::wstring& oldPath, const std::wstring& newPath, std::wstring* err) {
    if (::MoveFileW(oldPath.c_str(), newPath.c_str())) return true;
    if (err) *err = L"Yeniden adlandirilamadi (hata " + std::to_wstring(GetLastError()) + L")";
    return false;
}

// ------------------------------------------------------- RemoteSftpFileSystem

RemoteSftpFileSystem::RemoteSftpFileSystem(const Host& host, const Inventory& inv)
    : m_host(host), m_inv(inv)
{
    m_sshExe = FindSshExe();
    m_sftpExe = FindSftpExe();
}

RemoteSftpFileSystem::~RemoteSftpFileSystem() = default;

std::wstring RemoteSftpFileSystem::BuildSftpCommand() const {
    const std::wstring sftp = m_sftpExe.empty() ? ft::FindSftpExe() : m_sftpExe;
    if (sftp.empty()) return {};

    std::wstring cmd = L"\"" + sftp + L"\"";
    if (m_host.port > 0 && m_host.port != 22) {
        cmd += L" -P " + std::to_wstring(m_host.port);
    }

    std::wstring user = TrimWs(m_host.username);
    std::wstring key = m_host.keyPath;
    std::wstring cert = m_host.certPath;
    std::wstring pass = m_host.password;
    AuthKind kind = m_host.kind;

    if (!m_host.identityId.empty()) {
        for (const auto& id : m_inv.identities()) {
            if (id.id == m_host.identityId) {
                if (user.empty()) user = TrimWs(id.username);
                if (key.empty()) key = id.keyPath;
                if (cert.empty()) cert = id.certPath;
                if (pass.empty()) pass = id.password;
                if (pass.empty() && !id.passphrase.empty()) pass = id.passphrase;
                kind = id.kind;
                break;
            }
        }
    }

    // KULLANICININ SECTIGI AUTH METHODUNA KESIN BAGLILIK:
    if (kind == AuthKind::Password) {
        // Key denenmesini tamamen kapat:
        cmd += L" -o PubkeyAuthentication=no";
        cmd += L" -o PreferredAuthentications=password,keyboard-interactive";
        cmd += L" -o PasswordAuthentication=yes";
        cmd += L" -o KbdInteractiveAuthentication=yes";
    }
    else if (kind == AuthKind::Key) {
        cmd += L" -o PasswordAuthentication=no";
        cmd += L" -o PreferredAuthentications=publickey";
        if (!key.empty()) {
            key = m_inv.ResolveKeyPath(key, m_host.id.empty() ? m_host.Display() : m_host.id);
            cmd += L" -i \"" + key + L"\"";
            cmd += L" -o IdentitiesOnly=yes";
        }
        if (!cert.empty()) {
            cert = m_inv.ResolveCertPath(cert, m_host.id.empty() ? m_host.Display() : m_host.id);
            cmd += L" -o \"CertificateFile=" + cert + L"\"";
        }
    }
    else if (kind == AuthKind::Agent) {
        cmd += L" -o PasswordAuthentication=no";
        cmd += L" -o PreferredAuthentications=publickey";
    }

    const std::wstring jump = TrimWs(m_host.jumpHost);
    if (!jump.empty()) {
        cmd += L" -J \"" + jump + L"\"";
    }

    cmd += L" -o StrictHostKeyChecking=accept-new";
    cmd += L" -o ServerAliveInterval=15";
    cmd += L" -o ConnectTimeout=10";

    std::wstring target = m_host.address;
    if (!user.empty()) target = user + L"@" + target;
    cmd += L" \"" + target + L"\"";

    return cmd;
}

int RemoteSftpFileSystem::RunSftpBatch(const std::string& batchCommands, std::string& output, std::wstring* err,
                                        std::function<void(int pct)> onProgress) const {
    if (err) err->clear();
    output.clear();
    const std::wstring cmd = BuildSftpCommand();
    if (cmd.empty()) {
        if (err) *err = L"sftp.exe bulunamadi.";
        return -1;
    }

    std::wstring pass = m_host.password;
    if (!m_host.identityId.empty()) {
        for (const auto& id : m_inv.identities()) {
            if (id.id == m_host.identityId) {
                if (pass.empty()) pass = id.password;
                if (pass.empty() && !id.passphrase.empty()) pass = id.passphrase;
                break;
            }
        }
    }
    const std::string pw = WideToUtf8(pass);

    ft::ConPty pty;
    std::string fullOutput;
    std::atomic<bool> pwSent = false;
    std::atomic<bool> batchSent = false;
    std::atomic<bool> done = false;
    std::atomic<DWORD> exitCode = 0;
    std::atomic<ULONGLONG> lastActivity{GetTickCount64()};
    std::vector<std::string> commands;
    std::istringstream commandStream(batchCommands);
    for (std::string command; std::getline(commandStream, command);) {
        if (!command.empty() && command.back() == '\r') command.pop_back();
        if (!command.empty()) commands.push_back(std::move(command));
    }
    size_t nextCommand = 0, consumedPrompt = 0;
    bool quitSent = false;
    Screen promptScreen; promptScreen.Resize(1000, 40);
    VtParser promptParser(promptScreen);

    pty.onOutput = [&](const char* data, size_t len) {
        std::string s(data, len);
        fullOutput.append(s);
        promptParser.Feed(data, len);
        lastActivity = GetTickCount64();

        // Ilerleme yuzdesi ayristirma (ornek: "  45% " veya "100%")
        if (onProgress && batchSent.load()) {
            for (size_t p = 0; p < s.size(); ++p) {
                if (s[p] == '%' && p > 0) {
                    size_t startDigits = p;
                    while (startDigits > 0 && isdigit((unsigned char)s[startDigits - 1])) {
                        startDigits--;
                    }
                    if (startDigits < p) {
                        try {
                            int pct = std::stoi(s.substr(startDigits, p - startDigits));
                            onProgress(pct);
                        } catch (...) {}
                    }
                }
            }
        }

        if (!pwSent.load()) {
            std::string lower = fullOutput;
            for (auto& c : lower) c = (char)tolower((unsigned char)c);
            if (lower.find("password:") != std::string::npos || lower.find("password: ") != std::string::npos ||
                lower.find("passphrase") != std::string::npos) {
                pwSent.store(true);
                if (!pw.empty()) {
                    pty.Write(pw.c_str(), pw.size());
                    pty.Write("\r\n", 2);
                }
            }
        }
        // Never queue an operation behind an unverified cd/lcd. A failed cd used
        // to let put/rm operate in the login directory instead of the chosen one.
        const auto plain = StripAnsi(fullOutput);
        const auto prompt = plain.rfind("sftp>");
        // ConPTY can replace a trailing space with cursor positioning.
        // Check the rendered prompt too, so a filename containing "sftp>" cannot dispatch a command.
        if (!quitSent && prompt != std::string::npos && prompt >= consumedPrompt &&
            promptScreen.CursorLineText() == "sftp>") {
            consumedPrompt = prompt + 5;
            pwSent = true;
            batchSent = true;
            if (!SftpError(plain).empty() || nextCommand == commands.size()) {
                quitSent = true;
                pty.Write("bye\r", 4);
            } else {
                auto command = commands[nextCommand++] + "\r";
                pty.Write(command.data(), command.size());
            }
        }
    };

    pty.onExit = [&](DWORD code) {
        exitCode.store(code);
        done.store(true);
    };

    std::wstring ptyErr;
    if (!pty.Start(cmd, L"", {}, 1000, 40, &ptyErr)) {
        if (err) *err = L"ConPTY SFTP baslatilamadi: " + ptyErr;
        return -1;
    }

    while (!done.load()) {
        if (pty.ProcessExited()) {
            exitCode.store(pty.ExitCode());
            done.store(true);
            break;
        }
        auto idleElapsed = GetTickCount64() - lastActivity.load();
        if (m_cancelled || idleElapsed >= 30000) {
            pty.Close();
            output = StripAnsi(fullOutput);
            if (err) *err = m_cancelled ? L"SFTP operation cancelled." : L"SFTP islemi zaman asimina ugradi (30 sn hareketsizlik).";
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    pty.Close();

    output = StripAnsi(fullOutput);

    if (auto failure = SftpError(output); !failure.empty()) {
        if (err) *err = Utf8ToWide(failure);
        return -1;
    }

    if (exitCode.load() != 0 && err && err->empty() && !output.empty()) {
        *err = Utf8ToWide(output);
    }

    return (int)exitCode.load();
}

std::vector<FileItem> RemoteSftpFileSystem::List(const std::wstring& path, std::wstring* err) {
    std::vector<FileItem> items;
    if (err) err->clear();
    std::wstring p = path;
    if (p.empty() || p == L".") p = L"";

    // Hizli onbellek: geri ve ileri gezinmelerde 0 ms aninda sonuclari don
    {
        std::lock_guard<std::mutex> lock(m_cacheMtx);
        auto it = m_cache.find(p);
        if (it != m_cache.end()) {
            int64_t now = GetTickCount64();
            if (now - it->second.time < 20000) { // 20 saniye cache
                m_currentRemotePath = it->second.path;
                return it->second.items;
            }
        }
    }

    std::string batch;
    if (p.empty()) {
        batch = "pwd\nls -la\n";
    } else {
        batch = "cd " + QuoteSftp(p) + "\npwd\nls -la\n";
    }

    std::string out;
    int code = RunSftpBatch(batch, out, err);

    if (code != 0) {
        if (err && err->empty()) *err = L"SFTP listeleme hatasi (cikis " + std::to_wstring(code) + L"):\n" + Utf8ToWide(out);
        return items;
    }

    // SFTP pwd ve ls ciktisini ayristir
    std::istringstream iss(out);
    std::string line;
    std::wstring currentPath = p;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // "Remote working directory: /root"
        const std::string rwdPrefix = "Remote working directory: ";
        size_t rwdPos = line.find(rwdPrefix);
        if (rwdPos != std::string::npos) {
            std::string actualDir = line.substr(rwdPos + rwdPrefix.size());
            while (!actualDir.empty() && (actualDir.back() == ' ' || actualDir.back() == '\r')) actualDir.pop_back();
            currentPath = Utf8ToWide(actualDir);
            m_currentRemotePath = currentPath;
            continue;
        }

        if (line.rfind("sftp>", 0) == 0) continue;

        // "drwxr-xr-x    2 root     root         4096 Sep 23 21:29 mydir"
        // veya "drwx******    ? root     root         4096 Jun 26 23:52 ."
        // veya "-rw-r--r--    1 root     root         1234 Sep 23 21:30 file.txt"
        std::istringstream lss(line);
        std::string perm, links, owner, group, sizeStr, month, day, timeOrYear, name;
        if (lss >> perm >> links >> owner >> group >> sizeStr >> month >> day >> timeOrYear) {
            std::getline(lss, name);
            size_t nStart = name.find_first_not_of(" \t");
            if (nStart != std::string::npos) name = name.substr(nStart);

            // Symlink ise "link -> target" kismini kirp
            size_t arrow = name.find(" -> ");
            if (!perm.empty() && perm[0] == 'l' && arrow != std::string::npos) {
                name = name.substr(0, arrow);
            }

            // Path prefix'i varsa (orn. "/root/file.txt") yalniz dosya adini al
            size_t lastSlash = name.rfind('/');
            if (lastSlash != std::string::npos) {
                name = name.substr(lastSlash + 1);
            }

            if (name == "." || name.empty()) continue;
            const bool isDir = (!perm.empty() && (perm[0] == 'd' || perm[0] == 'l'));

            FileItem item;
            item.name = Utf8ToWide(name);
            item.isDir = isDir;
            item.isLink = !perm.empty() && perm[0] == 'l';
            item.permissions = Utf8ToWide(perm);
            item.kind = LocalFileSystem::GetFileKind(item.name, isDir);
            try {
                item.size = std::stoull(sizeStr);
            } catch (...) {
                item.size = 0;
            }
            item.modified = Utf8ToWide(month + " " + day + " " + timeOrYear);

            if (item.name == L"..") {
                size_t slash = currentPath.rfind(L'/');
                if (slash != std::wstring::npos) {
                    item.path = (slash == 0) ? L"/" : currentPath.substr(0, slash);
                } else {
                    item.path = L"/";
                }
                items.insert(items.begin(), item);
            } else {
                std::wstring normP = currentPath;
                if (normP.empty() || normP.back() != L'/') normP += L"/";
                item.path = normP + item.name;
                items.push_back(item);
            }
        }
    }

    if (m_currentRemotePath.empty()) m_currentRemotePath = currentPath.empty() ? L"/" : currentPath;

    std::stable_sort(items.begin(), items.end(), [](const FileItem& a, const FileItem& b) {
        if (a.name == L"..") return b.name != L"..";
        if (b.name == L"..") return false;
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    {
        std::lock_guard<std::mutex> lock(m_cacheMtx);
        m_cache[p] = { (int64_t)GetTickCount64(), m_currentRemotePath, items };
    }

    return items;
}

bool RemoteSftpFileSystem::MakeDirectory(const std::wstring& path, std::wstring* err) {
    InvalidateCache();
    std::string batch = "mkdir " + QuoteSftp(path);
    std::string out;
    int code = RunSftpBatch(batch, out, err);
    return code == 0;
}

bool RemoteSftpFileSystem::DeleteItem(const std::wstring& path, bool isDir, std::wstring* err) {
    InvalidateCache();
    std::string batch = (isDir ? "rmdir " : "rm ") + QuoteSftp(path, !isDir);
    std::string out;
    int code = RunSftpBatch(batch, out, err);
    return code == 0;
}

bool RemoteSftpFileSystem::Rename(const std::wstring& oldPath, const std::wstring& newPath, std::wstring* err) {
    InvalidateCache();
    std::string batch = "rename " + QuoteSftp(oldPath) + " " + QuoteSftp(newPath);
    std::string out;
    int code = RunSftpBatch(batch, out, err);
    return code == 0;
}

bool RemoteSftpFileSystem::Download(const std::wstring& remoteFile, const std::wstring& localDest, bool isDir, std::wstring* err,
                                    std::function<void(int pct)> onProgress) {
    std::wstring localDir = localDest;
    std::wstring localName;
    size_t lastSep = localDest.find_last_of(L"\\/");
    if (lastSep != std::wstring::npos) {
        localDir = localDest.substr(0, lastSep);
        localName = localDest.substr(lastSep + 1);
    } else {
        localDir = L".";
        localName = localDest;
    }

    std::wstring remoteDir = remoteFile;
    std::wstring remoteName;
    size_t lastSlash = remoteFile.find_last_of(L"/\\");
    if (lastSlash != std::wstring::npos) {
        remoteDir = remoteFile.substr(0, lastSlash);
        if (remoteDir.empty()) remoteDir = L"/";
        remoteName = remoteFile.substr(lastSlash + 1);
    } else {
        remoteDir = L".";
        remoteName = remoteFile;
    }

    std::string batch;
    batch += "lcd " + QuoteSftp(localDir) + "\n";
    if (remoteDir != L".") {
        batch += "cd " + QuoteSftp(remoteDir) + "\n";
        batch += (isDir ? "get -r " : "get ") + QuoteSftp(remoteName, true) + " " + QuoteSftp(localName) + "\n";
    } else {
        batch += (isDir ? "get -r " : "get ") + QuoteSftp(remoteFile, true) + " " + QuoteSftp(localName) + "\n";
    }

    std::string out;
    int code = RunSftpBatch(batch, out, err, onProgress);

    // An already-existing destination is not evidence of a successful download.
    if (code != 0) return false;
    const DWORD attr = GetFileAttributesW(localDest.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || bool(attr & FILE_ATTRIBUTE_DIRECTORY) != isDir) {
        if (err) *err = L"Download completed without the expected destination.";
        return false;
    }
    return true;
}

bool RemoteSftpFileSystem::Upload(const std::wstring& localFile, const std::wstring& remoteDest, bool isDir, std::wstring* err,
                                  std::function<void(int pct)> onProgress) {
    InvalidateCache();
    std::wstring localDir = localFile;
    std::wstring localName;
    size_t lastSep = localFile.find_last_of(L"\\/");
    if (lastSep != std::wstring::npos) {
        localDir = localFile.substr(0, lastSep);
        localName = localFile.substr(lastSep + 1);
    } else {
        localDir = L".";
        localName = localFile;
    }

    std::string batch;
    batch += "lcd " + QuoteSftp(localDir) + "\n";
    batch += "cd " + QuoteSftp(remoteDest) + "\n";
    // Windows local globbing treats brackets literally; '\\[' becomes a path separator.
    // '*' and '?' cannot occur in a Windows filename, unlike remote POSIX names.
    batch += (isDir ? "put -r " : "put ") + QuoteSftp(localName) + "\n";

    std::string out;
    int code = RunSftpBatch(batch, out, err, onProgress);
    return code == 0;
}

} // namespace ft
