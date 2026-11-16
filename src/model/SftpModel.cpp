#include "model/SftpModel.h"
#include "transport/pty/ConPty.h"
#include "core/Platform.h"
#include "core/Utf8.h"
#include "core/Settings.h"
#include "core/ShellProfiles.h"
#include "core/I18n.h"
#include "vt/VtParser.h"

#if defined(_WIN32)
#include <windows.h>
#include <shlwapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>
#include <ctime>
#endif
#include <filesystem>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <condition_variable>
#include <stdexcept>

namespace ft {

namespace {

// Dosya boyutunu "66.52 KB", "109.56 KB", "190.00 Bytes" seklinde bicimlendirir
std::wstring FormatBytes(uint64_t bytes) {
    wchar_t buf[64];
    if (bytes < 1024) {
        swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.2f Bytes", (double)bytes);
    } else if (bytes < 1024 * 1024) {
        swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.2f KB", (double)bytes / 1024.0);
    } else if (bytes < 1024ULL * 1024 * 1024) {
        swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
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

#if defined(_WIN32)
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
#endif

// Tarihi "9/24/2026, 12:05 AM" seklinde bicimlendirir
std::wstring FormatTimeT(time_t t) {
    tm ltm{};
#if defined(_WIN32)
    localtime_s(&ltm, &t);
#else
    localtime_r(&t, &ltm);
#endif
    int hour = ltm.tm_hour;
    const wchar_t* ampm = (hour >= 12) ? L"PM" : L"AM";
    if (hour == 0) hour = 12;
    else if (hour > 12) hour -= 12;

    wchar_t buf[64];
    swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"%d/%d/%d, %d:%02d %ls",
             ltm.tm_mon + 1, ltm.tm_mday, ltm.tm_year + 1900,
             hour, ltm.tm_min, ampm);
    return buf;
}

} // namespace

// ------------------------------------------------------------- LocalFileSystem

std::wstring LocalFileSystem::DefaultPath() {
#if defined(_WIN32)
    wchar_t buf[32768]{};
    DWORD len = GetCurrentDirectoryW(32768, buf);
    if (len > 0 && len < 32768) {
        std::wstring cwd(buf, len);
        std::wstring low = cwd;
        for (auto& c : low) c = (wchar_t)towlower(c);
        while (low.size() > 3 && (low.back() == L'\\' || low.back() == L'/')) low.pop_back();
        // Windows baslangicindan (HKCU\...\Run) acildiginda CWD System32 olur; kullanici klasorunu tercih et.
        if (low.ends_with(L"\\windows\\system32") || low.ends_with(L"\\windows\\syswow64")) {
            wchar_t prof[32768]{};
            DWORD plen = GetEnvironmentVariableW(L"USERPROFILE", prof, 32768);
            if (plen > 0 && plen < 32768 && GetFileAttributesW(prof) != INVALID_FILE_ATTRIBUTES) {
                return std::wstring(prof, plen);
            }
        }
        return cwd;
    }
    return L"C:\\";
#else
    const std::wstring home = UserHomeDir();
    if (!home.empty()) return home;
    return L"/";
#endif
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
#if defined(_WIN32)
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
#else
    drives.push_back(L"/");
    const std::wstring home = UserHomeDir();
    if (!home.empty()) drives.push_back(home);
    drives.push_back(L"/tmp");
    drives.push_back(L"/mnt");
    drives.push_back(L"/media");
#endif
    return drives;
}

std::vector<FileItem> LocalFileSystem::List(const std::wstring& path, std::wstring* err) {
#if defined(_WIN32)
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
        ULARGE_INTEGER uli{};
        uli.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        item.rawModified = static_cast<int64_t>(uli.QuadPart);
        item.modified = FormatFileTime(fd.ftLastWriteTime);

        items.push_back(item);
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    SortFileItems(items, SftpSortField::Name, true, false);

    return items;
#else
    std::vector<FileItem> items;
    if (err) err->clear();
    std::wstring p = path.empty() ? DefaultPath() : path;

    std::filesystem::path dirPath(p);
    std::error_code ec;
    if (!std::filesystem::exists(dirPath, ec) || !std::filesystem::is_directory(dirPath, ec)) {
        if (err) *err = L"Dizin acilamadi: " + path;
        return items;
    }

    // Ust dizin elemani ".."
    if (dirPath.has_parent_path() && dirPath.parent_path() != dirPath) {
        FileItem up;
        up.name = L"..";
        up.path = dirPath.parent_path().wstring();
        up.isDir = true;
        up.kind = L"folder";
        up.modified = L"--";
        items.push_back(up);
    }

    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        FileItem item;
        item.name = entry.path().filename().wstring();
        item.path = entry.path().wstring();
        item.isDir = entry.is_directory(ec);
        item.kind = GetFileKind(item.name, item.isDir);
        if (!item.isDir) {
            item.size = entry.file_size(ec);
        }
        auto ftime = entry.last_write_time(ec);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
        item.rawModified = cftime;
        item.modified = FormatTimeT(cftime);

        items.push_back(item);
    }

    SortFileItems(items, SftpSortField::Name, true, false);

    return items;
#endif
}

void SortFileItems(std::vector<FileItem>& items, SftpSortField field, bool ascending, bool isRemote) {
    std::stable_sort(items.begin(), items.end(), [&](const FileItem& a, const FileItem& b) {
        if (a.name == L"..") return b.name != L"..";
        if (b.name == L"..") return false;
        if (a.isDir != b.isDir) return a.isDir > b.isDir;

        int cmp = 0;
        switch (field) {
        case SftpSortField::Name:
            cmp = _wcsicmp(a.name.c_str(), b.name.c_str());
            break;
        case SftpSortField::Date:
            if (a.rawModified != b.rawModified) cmp = (a.rawModified < b.rawModified) ? -1 : 1;
            else cmp = _wcsicmp(a.modified.c_str(), b.modified.c_str());
            break;
        case SftpSortField::Size:
            if (a.size != b.size) cmp = (a.size < b.size) ? -1 : 1;
            break;
        case SftpSortField::Type:
            if (isRemote) {
                cmp = _wcsicmp(a.permissions.c_str(), b.permissions.c_str());
                if (cmp == 0) cmp = _wcsicmp(a.kind.c_str(), b.kind.c_str());
            } else {
                cmp = _wcsicmp(a.kind.c_str(), b.kind.c_str());
            }
            break;
        }
        if (cmp != 0) return ascending ? (cmp < 0) : (cmp > 0);
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

bool LocalFileSystem::MakeDirectory(const std::wstring& path, std::wstring* err) {
#if defined(_WIN32)
    if (::CreateDirectoryW(path.c_str(), nullptr)) return true;
    DWORD gle = GetLastError();
    if (gle == ERROR_ALREADY_EXISTS && (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY)) return true;
    if (err) *err = L"Klasor olusturulamadi (hata " + std::to_wstring(gle) + L")";
    return false;
#else
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path), ec);
    if (!ec) return true;
    if (err) *err = L"Klasor olusturulamadi: " + Utf8ToWide(ec.message());
    return false;
#endif
}

bool LocalFileSystem::CreateEmptyFile(const std::wstring& path, std::wstring* err) {
#if defined(_WIN32)
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        ::CloseHandle(h);
        return true;
    }
    const DWORD gle = GetLastError();
    if (err) {
        if (gle == ERROR_FILE_EXISTS || gle == ERROR_ALREADY_EXISTS) {
            *err = L"Dosya zaten mevcut / File already exists.";
        } else {
            *err = L"Dosya olusturulamadi (hata " + std::to_wstring(gle) + L")";
        }
    }
    return false;
#else
    const std::string u8 = WideToUtf8(path);
    const int fd = ::open(u8.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd >= 0) {
        ::close(fd);
        return true;
    }
    if (err) {
        if (errno == EEXIST) {
            *err = L"Dosya zaten mevcut / File already exists.";
        } else {
            *err = L"Dosya olusturulamadi: " + Utf8ToWide(strerror(errno));
        }
    }
    return false;
#endif
}

bool LocalFileSystem::DeleteItem(const std::wstring& path, bool isDir, std::wstring* err) {
    std::error_code ec;
    if (isDir) {
        std::filesystem::remove_all(std::filesystem::path(path), ec);
    } else {
        std::filesystem::remove(std::filesystem::path(path), ec);
    }
    if (!ec) return true;
    if (err) *err = L"Silinemedi: " + Utf8ToWide(ec.message());
    return false;
}

bool LocalFileSystem::Rename(const std::wstring& oldPath, const std::wstring& newPath, std::wstring* err) {
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(oldPath), std::filesystem::path(newPath), ec);
    if (!ec) return true;
    if (err) *err = L"Yeniden adlandirilamadi: " + Utf8ToWide(ec.message());
    return false;
}

// ------------------------------------------------------- RemoteSftpFileSystem

struct RemoteSftpFileSystem::PersistentSession {
    std::mutex mtx;
    std::condition_variable cv;
    Screen screen;
    VtParser parser{screen};
    std::string cmdBuffer;
    bool pwSent = false;
    bool ready = false;
    bool exited = false;
    DWORD exitCode = 0;
    ULONGLONG lastActivity = 0;
    std::function<void(int)> progressCb;
    ConPty pty;

    PersistentSession() {
        screen.Resize(1000, 40);
        screen.SetScrollbackLimit(200);
        lastActivity = GetTickCount64();
    }
    ~PersistentSession() {
        pty.Close();
    }
};

RemoteSftpFileSystem::RemoteSftpFileSystem(const Host& host, const Inventory& inv)
    : m_host(host), m_inv(inv)
{
    m_sshExe = FindSshExe();
    m_sftpExe = FindSftpExe();
}

RemoteSftpFileSystem::~RemoteSftpFileSystem() {
    Cancel();
    std::lock_guard<std::mutex> opLock(m_opMtx);
    CloseSession();
}

void RemoteSftpFileSystem::Cancel() {
    m_cancelled.store(true);
    m_opCancelled.store(true);
    std::shared_ptr<PersistentSession> sess;
    {
        std::lock_guard<std::mutex> lk(m_sessionPtrMtx);
        sess = m_session;
    }
    if (sess) {
        std::lock_guard<std::mutex> lk(sess->mtx);
        sess->cv.notify_all();
    }
}

void RemoteSftpFileSystem::CancelOperation() {
    m_opCancelled.store(true);
    std::shared_ptr<PersistentSession> sess;
    {
        std::lock_guard<std::mutex> lk(m_sessionPtrMtx);
        sess = m_session;
    }
    if (sess) {
        std::lock_guard<std::mutex> lk(sess->mtx);
        sess->cv.notify_all();
    }
}

std::wstring RemoteSftpFileSystem::LastProgressDetail() const {
    std::lock_guard<std::mutex> lk(m_progressMtx);
    return m_lastProgressDetail;
}

void RemoteSftpFileSystem::CloseSession() const {
    std::shared_ptr<PersistentSession> old;
    {
        std::lock_guard<std::mutex> lk(m_sessionPtrMtx);
        old.swap(m_session);
    }
    if (old) {
        if (old->ready && !old->exited && !old->pty.ProcessExited() && !m_cancelled && !m_opCancelled) {
            old->pty.Write("bye\r", 4);
        }
        old->pty.Close();
        std::lock_guard<std::mutex> lk(old->mtx);
        old->exited = true;
        old->cv.notify_all();
    }
}

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
    if (m_cancelled.load()) {
        if (err) *err = L"SFTP operation cancelled.";
        return -1;
    }
    m_opCancelled.store(false);
    {
        std::lock_guard<std::mutex> plk(m_progressMtx);
        m_lastProgressDetail.clear();
    }

    const std::wstring cmd = BuildSftpCommand();
    if (cmd.empty()) {
#if defined(_WIN32)
        if (err) *err = L"sftp.exe bulunamadi.";
#else
        if (err) *err = L"sftp bulunamadi (openssh-client paketi kurulu mu?).";
#endif
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

    std::vector<std::string> commands;
    {
        std::istringstream commandStream(batchCommands);
        for (std::string command; std::getline(commandStream, command);) {
            if (!command.empty() && command.back() == '\r') command.pop_back();
            if (!command.empty()) commands.push_back(std::move(command));
        }
    }

    std::lock_guard<std::mutex> opLock(m_opMtx);

    auto extractProgress = [this](const std::string& rawChunk, const std::string& screenLine,
                                  const std::function<void(int)>& cb) {
        if (!cb) return;
        auto parseFrom = [&](const std::string& src) -> bool {
            size_t pctPos = src.find('%');
            while (pctPos != std::string::npos) {
                size_t startDigits = pctPos;
                while (startDigits > 0 && isdigit((unsigned char)src[startDigits - 1])) {
                    --startDigits;
                }
                if (startDigits < pctPos && (pctPos - startDigits) <= 3) {
                    try {
                        int pct = std::stoi(src.substr(startDigits, pctPos - startDigits));
                        if (pct >= 0 && pct <= 100) {
                            std::string tail;
                            size_t after = pctPos + 1;
                            while (after < src.size() && src[after] != '\r' && src[after] != '\n') {
                                tail.push_back(src[after++]);
                            }
                            std::string compact;
                            bool prevSpace = true;
                            for (char ch : tail) {
                                if (ch == ' ' || ch == '\t') {
                                    if (!prevSpace) { compact += "  "; prevSpace = true; }
                                } else {
                                    compact.push_back(ch);
                                    prevSpace = false;
                                }
                            }
                            while (!compact.empty() && compact.back() == ' ') compact.pop_back();
                            if (!compact.empty()) {
                                std::lock_guard<std::mutex> plk(m_progressMtx);
                                m_lastProgressDetail = Utf8ToWide(compact);
                            }
                            cb(pct);
                            return true;
                        }
                    } catch (...) {}
                }
                pctPos = src.find('%', pctPos + 1);
            }
            return false;
        };
        if (!parseFrom(screenLine)) {
            parseFrom(StripAnsi(rawChunk));
        }
    };

    auto runAttempt = [&](auto& self, bool allowReconnect) -> int {
        output.clear();
        if (err) err->clear();

        std::shared_ptr<PersistentSession> sess;
        {
            std::lock_guard<std::mutex> lk(m_sessionPtrMtx);
            sess = m_session;
        }

        bool wasExisting = (sess != nullptr);
        if (sess) {
            std::lock_guard<std::mutex> lk(sess->mtx);
            if (!sess->ready || sess->exited || sess->pty.ProcessExited()) {
                sess.reset();
            }
        }
        if (!sess) {
            CloseSession();
            wasExisting = false;
            sess = std::make_shared<PersistentSession>();
            PersistentSession* raw = sess.get();

            sess->pty.onOutput = [this, raw, pw, &extractProgress](const char* data, size_t len) {
                std::string chunk(data, len);
                std::string screenLine;
                std::function<void(int)> progCb;
                bool sendPw = false;
                {
                    std::lock_guard<std::mutex> lk(raw->mtx);
                    raw->cmdBuffer.append(chunk);
                    raw->parser.Feed(data, len);
                    raw->lastActivity = GetTickCount64();
                    screenLine = raw->screen.CursorLineText();
                    progCb = raw->progressCb;

                    if (!raw->pwSent) {
                        std::string lower = StripAnsi(raw->cmdBuffer);
                        for (auto& c : lower) c = (char)tolower((unsigned char)c);
                        if (lower.find("password:") != std::string::npos ||
                            lower.find("passphrase") != std::string::npos) {
                            raw->pwSent = true;
                            sendPw = !pw.empty();
                        }
                    }
                    const std::string plain = StripAnsi(raw->cmdBuffer);
                    if (!raw->ready && plain.rfind("sftp>") != std::string::npos &&
                        screenLine == "sftp>") {
                        raw->pwSent = true;
                        raw->ready = true;
                    }
                    raw->cv.notify_all();
                }
                if (sendPw) {
                    raw->pty.Write(pw.c_str(), pw.size());
                    raw->pty.Write("\r\n", 2);
                }
                if (progCb) {
                    extractProgress(chunk, screenLine, progCb);
                }
            };

            sess->pty.onExit = [raw](DWORD code) {
                std::lock_guard<std::mutex> lk(raw->mtx);
                raw->exitCode = code;
                raw->exited = true;
                raw->cv.notify_all();
            };

            std::wstring ptyErr;
            if (!sess->pty.Start(cmd, L"", {}, 1000, 40, &ptyErr)) {
                if (err) *err = L"ConPTY SFTP baslatilamadi: " + ptyErr;
                return -1;
            }
            m_connectCount.fetch_add(1);
            {
                std::lock_guard<std::mutex> lk(m_sessionPtrMtx);
                m_session = sess;
            }

            // Ilk sftp> istemini bekle
            std::unique_lock<std::mutex> lk(sess->mtx);
            while (!sess->ready) {
                if (m_cancelled.load() || m_opCancelled.load()) {
                    lk.unlock();
                    CloseSession();
                    if (err) *err = L"SFTP operation cancelled.";
                    return -1;
                }
                if (sess->exited || sess->pty.ProcessExited()) {
                    DWORD code = sess->exited ? sess->exitCode : sess->pty.ExitCode();
                    output = StripAnsi(sess->cmdBuffer);
                    lk.unlock();
                    CloseSession();
                    if (auto failure = SftpError(output); !failure.empty()) {
                        if (err) *err = Utf8ToWide(failure);
                    } else if (err && !output.empty()) {
                        *err = Utf8ToWide(output);
                    } else if (err) {
                        *err = L"SFTP baglantisi kurulamadi (cikis " + std::to_wstring(code) + L").";
                    }
                    return -1;
                }
                if (GetTickCount64() - sess->lastActivity >= 30000) {
                    output = StripAnsi(sess->cmdBuffer);
                    lk.unlock();
                    CloseSession();
                    if (err) *err = L"SFTP islemi zaman asimina ugradi (30 sn hareketsizlik).";
                    return -1;
                }
                sess->cv.wait_for(lk, std::chrono::milliseconds(50));
            }

            const std::string initPlain = StripAnsi(sess->cmdBuffer);
            if (auto failure = SftpError(initPlain); !failure.empty()) {
                output = initPlain;
                lk.unlock();
                CloseSession();
                if (err) *err = Utf8ToWide(failure);
                return -1;
            }
        }

        // Kalici sftp> oturumu uzerinde komutlari tek tek ve dogrulayarak calistir
        {
            std::lock_guard<std::mutex> lk(sess->mtx);
            sess->progressCb = onProgress;
        }

        for (size_t i = 0; i < commands.size(); ++i) {
            {
                std::lock_guard<std::mutex> lk(sess->mtx);
                sess->cmdBuffer.clear();
                sess->lastActivity = GetTickCount64();
            }
            const std::string wire = commands[i] + "\r";
            sess->pty.Write(wire.data(), wire.size());

            std::unique_lock<std::mutex> lk(sess->mtx);
            bool promptReached = false;
            for (;;) {
                const std::string cmdPlain = StripAnsi(sess->cmdBuffer);
                if (cmdPlain.rfind("sftp>") != std::string::npos &&
                    sess->screen.CursorLineText() == "sftp>") {
                    promptReached = true;
                    break;
                }
                if (m_cancelled.load() || m_opCancelled.load()) {
                    sess->progressCb = nullptr;
                    lk.unlock();
                    CloseSession();
                    if (err) *err = L"SFTP operation cancelled.";
                    return -1;
                }
                if (sess->exited || sess->pty.ProcessExited()) {
                    sess->progressCb = nullptr;
                    const std::string tail = StripAnsi(sess->cmdBuffer);
                    lk.unlock();
                    CloseSession();
                    // Baglanti bosta iken karsi sunucu kapatmissa ilk komutta bir kez seffaf yeniden baglan
                    if (wasExisting && i == 0 && allowReconnect && SftpError(tail).empty()) {
                        return self(self, false);
                    }
                    output += tail;
                    if (auto failure = SftpError(output); !failure.empty()) {
                        if (err) *err = Utf8ToWide(failure);
                    } else if (err && !output.empty()) {
                        *err = Utf8ToWide(output);
                    } else if (err) {
                        *err = L"SFTP oturumu beklenmedik sekilde kapandi.";
                    }
                    return -1;
                }
                if (GetTickCount64() - sess->lastActivity >= 45000) {
                    sess->progressCb = nullptr;
                    output += StripAnsi(sess->cmdBuffer);
                    lk.unlock();
                    CloseSession();
                    if (err) *err = L"SFTP islemi zaman asimina ugradi (45 sn hareketsizlik).";
                    return -1;
                }
                sess->cv.wait_for(lk, std::chrono::milliseconds(50));
            }

            if (promptReached) {
                const std::string cmdPlain = StripAnsi(sess->cmdBuffer);
                output += cmdPlain;
                // Basarisiz bir cd / lcd sonrasinda put/rm calistirilmasini kes; oturum canli kalir!
                if (auto failure = SftpError(cmdPlain); !failure.empty()) {
                    sess->progressCb = nullptr;
                    if (err) *err = Utf8ToWide(failure);
                    return -1;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(sess->mtx);
            sess->progressCb = nullptr;
        }
        return 0;
    };

    return runAttempt(runAttempt, true);
}

std::vector<FileItem> RemoteSftpFileSystem::List(const std::wstring& path, std::wstring* err) {
    std::vector<FileItem> items;
    if (err) err->clear();
    auto normKey = [](std::wstring k) {
        while (k.size() > 1 && k.back() == L'/') k.pop_back();
        return k;
    };
    std::wstring p = path;
    if (p.empty() || p == L".") p = L"";
    const std::wstring lookupKey = p.empty() ? L"" : normKey(p);

    // Hizli onbellek: geri ve ileri gezinmelerde 0 ms aninda sonuclari don
    if (!lookupKey.empty()) {
        std::lock_guard<std::mutex> lock(m_cacheMtx);
        auto it = m_cache.find(lookupKey);
        if (it != m_cache.end()) {
            int64_t now = GetTickCount64();
            if (now - it->second.time < 30000) { // 30 saniye cache
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
            if (perm.size() < 10 || std::string("-dlbcps").find(perm[0]) == std::string::npos) continue;
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
            {
                static const char* const kMonths[12] = {
                    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
                };
                int mon = 0;
                for (int mi = 0; mi < 12; ++mi) {
#if defined(_WIN32)
                    if (_stricmp(month.c_str(), kMonths[mi]) == 0) { mon = mi + 1; break; }
#else
                    if (strcasecmp(month.c_str(), kMonths[mi]) == 0) { mon = mi + 1; break; }
#endif
                }
                int d = 0;
                try { d = std::stoi(day); } catch (...) { d = 0; }
#if defined(_WIN32)
                SYSTEMTIME nowSt{};
                GetLocalTime(&nowSt);
                int yr = nowSt.wYear;
                int hr = 0, mn = 0;
                const size_t colon = timeOrYear.find(':');
                if (colon != std::string::npos) {
                    try {
                        hr = std::stoi(timeOrYear.substr(0, colon));
                        mn = std::stoi(timeOrYear.substr(colon + 1));
                    } catch (...) { hr = 0; mn = 0; }
                    if (mon > (int)nowSt.wMonth || (mon == (int)nowSt.wMonth && d > (int)nowSt.wDay + 1)) {
                        yr -= 1;
                    }
                } else {
                    try { yr = std::stoi(timeOrYear); } catch (...) {}
                }
                if (mon >= 1 && mon <= 12 && d >= 1 && d <= 31 && yr >= 1601) {
                    SYSTEMTIME st{};
                    st.wYear = (WORD)yr;
                    st.wMonth = (WORD)mon;
                    st.wDay = (WORD)d;
                    st.wHour = (WORD)std::clamp(hr, 0, 23);
                    st.wMinute = (WORD)std::clamp(mn, 0, 59);
                    FILETIME ftTime{};
                    if (SystemTimeToFileTime(&st, &ftTime)) {
                        ULARGE_INTEGER uli{};
                        uli.LowPart = ftTime.dwLowDateTime;
                        uli.HighPart = ftTime.dwHighDateTime;
                        item.rawModified = static_cast<int64_t>(uli.QuadPart);
                    }
                }
#else
                std::time_t nowSec = std::time(nullptr);
                tm nowTm{};
                localtime_r(&nowSec, &nowTm);
                int yr = nowTm.tm_year + 1900;
                int nowMon = nowTm.tm_mon + 1;
                int nowDay = nowTm.tm_mday;
                int hr = 0, mn = 0;
                const size_t colon = timeOrYear.find(':');
                if (colon != std::string::npos) {
                    try {
                        hr = std::stoi(timeOrYear.substr(0, colon));
                        mn = std::stoi(timeOrYear.substr(colon + 1));
                    } catch (...) { hr = 0; mn = 0; }
                    if (mon > nowMon || (mon == nowMon && d > nowDay + 1)) {
                        yr -= 1;
                    }
                } else {
                    try { yr = std::stoi(timeOrYear); } catch (...) {}
                }
                if (mon >= 1 && mon <= 12 && d >= 1 && d <= 31 && yr >= 1970) {
                    tm tmv{};
                    tmv.tm_year = yr - 1900;
                    tmv.tm_mon = mon - 1;
                    tmv.tm_mday = d;
                    tmv.tm_hour = std::clamp(hr, 0, 23);
                    tmv.tm_min = std::clamp(mn, 0, 59);
                    tmv.tm_isdst = -1;
                    const std::time_t parsed = mktime(&tmv);
                    if (parsed != (std::time_t)-1) {
                        item.rawModified = static_cast<int64_t>(parsed);
                    }
                }
#endif
            }

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
        const int64_t ts = (int64_t)GetTickCount64();
        const std::wstring canonKey = normKey(m_currentRemotePath);
        if (!canonKey.empty()) {
            m_cache[canonKey] = { ts, m_currentRemotePath, items };
        }
        if (!lookupKey.empty() && lookupKey != canonKey) {
            m_cache[lookupKey] = { ts, m_currentRemotePath, items };
        }
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

bool RemoteSftpFileSystem::CreateEmptyFile(const std::wstring& path, std::wstring* err) {
    InvalidateCache();
#if defined(_WIN32)
    wchar_t tempDirBuf[MAX_PATH]{};
    DWORD n = GetTempPathW(MAX_PATH, tempDirBuf);
    if (!n || n >= MAX_PATH) {
        if (err) *err = L"Temp directory unavailable.";
        return false;
    }
    std::wstring tempDir(tempDirBuf);
    while (tempDir.size() > 1 && (tempDir.back() == L'\\' || tempDir.back() == L'/')) tempDir.pop_back();
    const std::wstring tempName = L"ft_empty_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
                                  std::to_wstring(GetTickCount64()) + L".tmp";
    const std::wstring tempFull = tempDir + L"\\" + tempName;
    HANDLE h = ::CreateFileW(tempFull.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = L"Could not stage empty file.";
        return false;
    }
    ::CloseHandle(h);
#else
    std::error_code ec;
    const std::filesystem::path tmpDirPath = std::filesystem::temp_directory_path(ec);
    if (ec || tmpDirPath.empty()) {
        if (err) *err = L"Temp directory unavailable.";
        return false;
    }
    std::wstring tempDir = tmpDirPath.wstring();
    while (tempDir.size() > 1 && tempDir.back() == L'/') tempDir.pop_back();
    const std::wstring tempName = L"ft_empty_" + std::to_wstring((long)getpid()) + L"_" +
                                  std::to_wstring(GetTickCount64()) + L".tmp";
    const std::filesystem::path tempFullPath = tmpDirPath / WideToUtf8(tempName);
    const int fd = ::open(tempFullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        if (err) *err = L"Could not stage empty file.";
        return false;
    }
    ::close(fd);
#endif

    std::wstring remoteDir = L".";
    std::wstring remoteName = path;
    const size_t lastSlash = path.find_last_of(L"/\\");
    if (lastSlash != std::wstring::npos) {
        remoteDir = path.substr(0, lastSlash);
        if (remoteDir.empty()) remoteDir = L"/";
        remoteName = path.substr(lastSlash + 1);
    }

    std::string batch = "lcd " + QuoteSftp(tempDir) + "\n";
    if (remoteDir != L".") batch += "cd " + QuoteSftp(remoteDir) + "\n";
    batch += "put " + QuoteSftp(tempName) + " " + QuoteSftp(remoteName) + "\n";

    std::string out;
    const int code = RunSftpBatch(batch, out, err);
#if defined(_WIN32)
    ::DeleteFileW(tempFull.c_str());
#else
    std::filesystem::remove(tempFullPath, ec);
#endif
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
#if defined(_WIN32)
    const DWORD attr = GetFileAttributesW(localDest.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || bool(attr & FILE_ATTRIBUTE_DIRECTORY) != isDir) {
#else
    std::error_code ec;
    const std::filesystem::path dest(WideToUtf8(localDest));
    if (!std::filesystem::exists(dest, ec) || std::filesystem::is_directory(dest, ec) != isDir) {
#endif
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
