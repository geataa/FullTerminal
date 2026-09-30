#include "model/SftpModel.h"
#include "transport/pty/ConPty.h"
#include "core/Utf8.h"
#include "core/Settings.h"
#include "core/ShellProfiles.h"

#include <windows.h>
#include <shlwapi.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <chrono>

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

std::string StripAnsi(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\x1b') {
            if (i + 1 < str.size() && (str[i + 1] == '[' || str[i + 1] == ']')) {
                i += 2;
                while (i < str.size() && str[i] >= 0x20 && str[i] <= 0x3F) ++i;
                while (i < str.size() && str[i] >= 0x40 && str[i] <= 0x7E) {
                    if (str[i] == 'm' || str[i] == 'h' || str[i] == 'l' || str[i] == 'r' ||
                        str[i] == 'H' || str[i] == 'J' || str[i] == 'K' || str[i] == '\a') {
                        break;
                    }
                    ++i;
                }
            }
        } else if (str[i] != '\r') {
            out.push_back(str[i]);
        }
    }
    return out;
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
    std::wstring p = path.empty() ? DefaultPath() : path;

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
        if (err) *err = L"Dizin acilamadi: " + path;
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
        if (a.name == L"..") return true;
        if (b.name == L"..") return false;
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    return items;
}

bool LocalFileSystem::MakeDirectory(const std::wstring& path, std::wstring* err) {
    if (::CreateDirectoryW(path.c_str(), nullptr)) return true;
    DWORD gle = GetLastError();
    if (gle == ERROR_ALREADY_EXISTS) return true;
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

int RemoteSftpFileSystem::RunSftpBatch(const std::string& batchCommands, std::string& output, std::wstring* err) const {
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

    pty.onOutput = [&](const char* data, size_t len) {
        std::string s(data, len);
        fullOutput.append(s);

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
            } else if (fullOutput.find("sftp>") != std::string::npos) {
                pwSent.store(true);
                batchSent.store(true);
                std::string toSend = batchCommands;
                if (toSend.empty() || toSend.back() != '\n') toSend += "\n";
                toSend += "bye\n";
                std::string crlf;
                for (char c : toSend) {
                    if (c == '\n') crlf += "\r\n";
                    else if (c != '\r') crlf += c;
                }
                pty.Write(crlf.c_str(), crlf.size());
            }
        } else if (!batchSent.load()) {
            if (fullOutput.find("sftp>") != std::string::npos) {
                batchSent.store(true);
                std::string toSend = batchCommands;
                if (toSend.empty() || toSend.back() != '\n') toSend += "\n";
                toSend += "bye\n";
                std::string crlf;
                for (char c : toSend) {
                    if (c == '\n') crlf += "\r\n";
                    else if (c != '\r') crlf += c;
                }
                pty.Write(crlf.c_str(), crlf.size());
            }
        }
    };

    pty.onExit = [&](DWORD code) {
        exitCode.store(code);
        done.store(true);
    };

    std::wstring ptyErr;
    if (!pty.Start(cmd, L"", {}, 160, 40, &ptyErr)) {
        if (err) *err = L"ConPTY SFTP baslatilamadi: " + ptyErr;
        return -1;
    }

    auto start = std::chrono::steady_clock::now();
    while (!done.load()) {
        if (pty.ProcessExited()) {
            exitCode.store(pty.ExitCode());
            done.store(true);
            break;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= 35) {
            pty.Close();
            if (err) *err = L"SFTP islemi zaman asimina ugradi (35 sn).";
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    pty.Close();

    output = StripAnsi(fullOutput);

    if (output.find("Permission denied") != std::string::npos) {
        if (err) *err = L"SFTP erisim engellendi (Kullanici adi veya parola/anahtar hatali).";
        return -1;
    }
    if (output.find("Connection refused") != std::string::npos) {
        if (err) *err = L"SFTP baglantisi reddedildi (Port 22 kapali veya erisilemiyor).";
        return -1;
    }

    if (exitCode.load() != 0 && err && err->empty() && !output.empty()) {
        *err = Utf8ToWide(output);
    }

    return (int)exitCode.load();
}

std::vector<FileItem> RemoteSftpFileSystem::List(const std::wstring& path, std::wstring* err) {
    std::vector<FileItem> items;
    std::wstring p = path;
    if (p.empty() || p == L".") p = L"";

    // Hizli onbellek: geri ve ileri gezinmelerde 0 ms aninda sonuclari don
    {
        std::lock_guard<std::mutex> lock(m_cacheMtx);
        auto it = m_cache.find(p);
        if (it != m_cache.end()) {
            int64_t now = GetTickCount64();
            if (now - it->second.first < 20000) { // 20 saniye cache
                return it->second.second;
            }
        }
    }

    std::string batch;
    if (p.empty()) {
        batch = "pwd\nls -la\n";
    } else {
        batch = "cd \"" + WideToUtf8(p) + "\"\npwd\nls -la\n";
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
            if (arrow != std::string::npos) {
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
        if (a.name == L"..") return true;
        if (b.name == L"..") return false;
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    {
        std::lock_guard<std::mutex> lock(m_cacheMtx);
        m_cache[p] = { (int64_t)GetTickCount64(), items };
    }

    return items;
}

bool RemoteSftpFileSystem::MakeDirectory(const std::wstring& path, std::wstring* err) {
    InvalidateCache();
    std::string batch = "mkdir \"" + WideToUtf8(path) + "\"";
    std::string out;
    int code = RunSftpBatch(batch, out, err);
    return code == 0;
}

bool RemoteSftpFileSystem::DeleteItem(const std::wstring& path, bool isDir, std::wstring* err) {
    InvalidateCache();
    std::string batch = (isDir ? "rmdir \"" : "rm \"") + WideToUtf8(path) + "\"";
    std::string out;
    int code = RunSftpBatch(batch, out, err);
    return code == 0;
}

bool RemoteSftpFileSystem::Rename(const std::wstring& oldPath, const std::wstring& newPath, std::wstring* err) {
    InvalidateCache();
    std::string batch = "rename \"" + WideToUtf8(oldPath) + "\" \"" + WideToUtf8(newPath) + "\"";
    std::string out;
    int code = RunSftpBatch(batch, out, err);
    return code == 0;
}

bool RemoteSftpFileSystem::Download(const std::wstring& remoteFile, const std::wstring& localDest, std::wstring* err) {
    std::string remoteP = WideToUtf8(remoteFile);
    std::string localP = WideToUtf8(localDest);
    for (char& c : localP) if (c == '\\') c = '/';
    std::string batch = "get \"" + remoteP + "\" \"" + localP + "\"";
    std::string out;
    int code = RunSftpBatch(batch, out, err);
    return code == 0;
}

bool RemoteSftpFileSystem::Upload(const std::wstring& localFile, const std::wstring& remoteDest, std::wstring* err) {
    InvalidateCache();
    std::string localP = WideToUtf8(localFile);
    for (char& c : localP) if (c == '\\') c = '/';
    std::string remoteP = WideToUtf8(remoteDest);
    std::string batch = "put \"" + localP + "\" \"" + remoteP + "\"";
    std::string out;
    int code = RunSftpBatch(batch, out, err);
    return code == 0;
}

// ------------------------------------------------------------- SftpController

SftpController::SftpController(const Inventory& inv) : m_inv(inv) {
    m_localPath = LocalFileSystem::DefaultPath();
    RefreshLocal();
}

SftpController::~SftpController() = default;

void SftpController::SetLocalPath(const std::wstring& path) {
    m_localPath = path;
    RefreshLocal();
}

void SftpController::RefreshLocal() {
    std::wstring err;
    auto items = m_localFs.List(m_localPath, &err);
    if (m_localPath.size() > 3) {
        bool hasDotDot = false;
        for (const auto& item : items) {
            if (item.name == L"..") { hasDotDot = true; break; }
        }
        if (!hasDotDot) {
            FileItem dotDot;
            dotDot.name = L"..";
            dotDot.isDir = true;
            dotDot.kind = L"Üst Klasör";
            items.insert(items.begin(), dotDot);
        }
    }
    m_localItems = std::move(items);
    if (!err.empty()) SetStatusMessage(err);
}

void SftpController::LocalNavigateUp() {
    while (m_localPath.size() > 3 && (m_localPath.back() == L'\\' || m_localPath.back() == L'/')) {
        m_localPath.pop_back();
    }
    if (m_localPath.size() <= 3) return; // "C:\"
    size_t slash = m_localPath.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        if (slash == 2 && m_localPath[1] == L':') m_localPath = m_localPath.substr(0, 3);
        else m_localPath = m_localPath.substr(0, slash);
        RefreshLocal();
    }
}

void SftpController::LocalNavigateDown(const std::wstring& folderName) {
    if (folderName == L"..") {
        LocalNavigateUp();
        return;
    }
    while (m_localPath.size() > 3 && (m_localPath.back() == L'\\' || m_localPath.back() == L'/')) {
        m_localPath.pop_back();
    }
    if (m_localPath.back() != L'\\') m_localPath += L"\\";
    m_localPath += folderName;
    RefreshLocal();
}

void SftpController::ConnectRemote(const Host& host) {
    m_remoteFs = std::make_unique<RemoteSftpFileSystem>(host, m_inv);
    m_remotePath = L".";
    m_remoteState = SftpConnectionState::Connecting;
    m_remoteError.clear();
    SetStatusMessage(L"SFTP ile bağlanılıyor: " + host.Display());

    m_busy = true;
    std::thread([this, host] {
        std::wstring err;
        auto items = m_remoteFs->List(m_remotePath, &err);
        if (err.empty()) {
            m_remotePath = m_remoteFs->CurrentPath();
            if (m_remotePath.empty()) m_remotePath = L"/";
            if (m_remotePath != L"/" && !m_remotePath.empty()) {
                bool hasDotDot = false;
                for (const auto& item : items) {
                    if (item.name == L"..") { hasDotDot = true; break; }
                }
                if (!hasDotDot) {
                    FileItem dotDot;
                    dotDot.name = L"..";
                    dotDot.isDir = true;
                    dotDot.kind = L"Üst Klasör";
                    items.insert(items.begin(), dotDot);
                }
            }
            m_remoteItems = std::move(items);
            m_remoteState = SftpConnectionState::Connected;
            SetStatusMessage(L"SFTP bağlandı: " + host.Display());
        } else {
            m_remoteState = SftpConnectionState::Failed;
            m_remoteError = err;
            SetStatusMessage(L"SFTP bağlantı hatası: " + err);
        }
        m_busy = false;
    }).detach();
}

void SftpController::DisconnectRemote() {
    m_remoteFs.reset();
    m_remoteState = SftpConnectionState::Disconnected;
    m_remoteItems.clear();
    SetStatusMessage(L"SFTP bağlantısı kesildi.");
}

void SftpController::RefreshRemote() {
    if (!m_remoteFs || m_busy) return;
    m_busy = true;
    m_remoteFs->InvalidateCache();
    std::thread([this] {
        std::wstring err;
        auto items = m_remoteFs->List(m_remotePath, &err);
        if (err.empty()) {
            m_remotePath = m_remoteFs->CurrentPath();
            if (m_remotePath.empty()) m_remotePath = L"/";
            if (m_remotePath != L"/" && !m_remotePath.empty()) {
                bool hasDotDot = false;
                for (const auto& item : items) {
                    if (item.name == L"..") { hasDotDot = true; break; }
                }
                if (!hasDotDot) {
                    FileItem dotDot;
                    dotDot.name = L"..";
                    dotDot.isDir = true;
                    dotDot.kind = L"Üst Klasör";
                    items.insert(items.begin(), dotDot);
                }
            }
            m_remoteItems = std::move(items);
            SetStatusMessage(L"Uzak dizin yenilendi.");
        } else {
            SetStatusMessage(L"Uzak yenileme hatası: " + err);
        }
        m_busy = false;
    }).detach();
}

void SftpController::SetRemotePath(const std::wstring& path) {
    m_remotePath = path;
    if (m_remotePath.empty()) m_remotePath = L"/";
    for (wchar_t& c : m_remotePath) if (c == L'\\') c = L'/';
    RefreshRemote();
}

void SftpController::RemoteNavigateUp() {
    while (m_remotePath.size() > 1 && m_remotePath.back() == L'/') {
        m_remotePath.pop_back();
    }
    if (m_remotePath == L"/" || m_remotePath.empty()) return;
    size_t slash = m_remotePath.rfind(L'/');
    if (slash != std::wstring::npos) {
        if (slash == 0) m_remotePath = L"/";
        else m_remotePath = m_remotePath.substr(0, slash);
        RefreshRemote();
    }
}

void SftpController::RemoteNavigateDown(const std::wstring& folderName) {
    if (folderName == L"..") {
        RemoteNavigateUp();
        return;
    }
    while (m_remotePath.size() > 1 && m_remotePath.back() == L'/') {
        m_remotePath.pop_back();
    }
    if (m_remotePath.empty()) m_remotePath = L"/";
    if (m_remotePath.back() != L'/') m_remotePath += L"/";
    m_remotePath += folderName;
    RefreshRemote();
}

bool SftpController::UploadSelected(const std::wstring& localFileName, std::wstring* err) {
    if (!m_remoteFs || m_remoteState != SftpConnectionState::Connected) {
        if (err) *err = L"Uzak sunucuya bagli degil.";
        return false;
    }
    std::wstring localFull = m_localPath;
    if (localFull.back() != L'\\') localFull += L"\\";
    localFull += localFileName;

    std::wstring remoteFull = m_remotePath;
    if (remoteFull.back() != L'/') remoteFull += L"/";
    remoteFull += localFileName;

    SetStatusMessage(L"Yukleniyor: " + localFileName);
    m_busy = true;
    std::thread([this, localFull, remoteFull, localFileName] {
        std::wstring err;
        bool ok = m_remoteFs->Upload(localFull, remoteFull, &err);
        if (ok) {
            SetStatusMessage(L"Yukleme tamamlandi: " + localFileName);
            RefreshRemote();
        } else {
            SetStatusMessage(L"Yukleme basarisiz: " + err);
        }
        m_busy = false;
    }).detach();
    return true;
}

bool SftpController::DownloadSelected(const std::wstring& remoteFileName, std::wstring* err) {
    if (!m_remoteFs || m_remoteState != SftpConnectionState::Connected) {
        if (err) *err = L"Uzak sunucuya bagli degil.";
        return false;
    }
    std::wstring remoteFull = m_remotePath;
    if (remoteFull.back() != L'/') remoteFull += L"/";
    remoteFull += remoteFileName;

    std::wstring localFull = m_localPath;
    if (localFull.back() != L'\\') localFull += L"\\";
    localFull += remoteFileName;

    SetStatusMessage(L"Indiriliyor: " + remoteFileName);
    m_busy = true;
    std::thread([this, remoteFull, localFull, remoteFileName] {
        std::wstring err;
        bool ok = m_remoteFs->Download(remoteFull, localFull, &err);
        if (ok) {
            SetStatusMessage(L"Indirme tamamlandi: " + remoteFileName);
            RefreshLocal();
        } else {
            SetStatusMessage(L"Indirme basarisiz: " + err);
        }
        m_busy = false;
    }).detach();
    return true;
}

bool SftpController::DeleteLocalItem(const std::wstring& name, bool isDir, std::wstring* err) {
    std::wstring full = m_localPath + (m_localPath.back() == L'\\' ? L"" : L"\\") + name;
    bool ok = m_localFs.DeleteItem(full, isDir, err);
    if (ok) RefreshLocal();
    return ok;
}

bool SftpController::DeleteRemoteItem(const std::wstring& name, bool isDir, std::wstring* err) {
    if (!m_remoteFs) return false;
    std::wstring full = m_remotePath + (m_remotePath.back() == L'/' ? L"" : L"/") + name;
    bool ok = m_remoteFs->DeleteItem(full, isDir, err);
    if (ok) RefreshRemote();
    return ok;
}

bool SftpController::CreateLocalFolder(const std::wstring& name, std::wstring* err) {
    std::wstring full = m_localPath + (m_localPath.back() == L'\\' ? L"" : L"\\") + name;
    bool ok = m_localFs.MakeDirectory(full, err);
    if (ok) RefreshLocal();
    return ok;
}

bool SftpController::CreateRemoteFolder(const std::wstring& name, std::wstring* err) {
    if (!m_remoteFs) return false;
    std::wstring full = m_remotePath + (m_remotePath.back() == L'/' ? L"" : L"/") + name;
    bool ok = m_remoteFs->MakeDirectory(full, err);
    if (ok) RefreshRemote();
    return ok;
}

bool SftpController::RenameRemoteItem(const std::wstring& oldName, const std::wstring& newName, std::wstring* err) {
    if (!m_remoteFs || m_remoteState != SftpConnectionState::Connected) {
        if (err) *err = L"Uzak sunucuya bagli degil.";
        return false;
    }
    std::wstring oldFull = m_remotePath + (m_remotePath.back() == L'/' ? L"" : L"/") + oldName;
    std::wstring newFull = m_remotePath + (m_remotePath.back() == L'/' ? L"" : L"/") + newName;
    bool ok = m_remoteFs->Rename(oldFull, newFull, err);
    if (ok) RefreshRemote();
    return ok;
}

bool SftpController::RenameLocalItem(const std::wstring& oldName, const std::wstring& newName, std::wstring* err) {
    std::wstring oldFull = m_localPath + (m_localPath.back() == L'\\' ? L"" : L"\\") + oldName;
    std::wstring newFull = m_localPath + (m_localPath.back() == L'\\' ? L"" : L"\\") + newName;
    bool ok = m_localFs.Rename(oldFull, newFull, err);
    if (ok) RefreshLocal();
    return ok;
}

std::vector<FileItem> SftpController::GetFilteredLocal(const std::wstring& filter) const {
    if (filter.empty()) return m_localItems;
    std::vector<FileItem> res;
    for (const auto& item : m_localItems) {
        if (item.name == L".." || StrStrIW(item.name.c_str(), filter.c_str()) != nullptr) {
            res.push_back(item);
        }
    }
    return res;
}

std::vector<FileItem> SftpController::GetFilteredRemote(const std::wstring& filter) const {
    if (filter.empty()) return m_remoteItems;
    std::vector<FileItem> res;
    for (const auto& item : m_remoteItems) {
        if (item.name == L".." || StrStrIW(item.name.c_str(), filter.c_str()) != nullptr) {
            res.push_back(item);
        }
    }
    return res;
}

} // namespace ft
