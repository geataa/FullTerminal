#include "model/SftpModel.h"
#include "core/I18n.h"
#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <filesystem>
#include <algorithm>

namespace ft {
namespace {
bool ValidName(const std::wstring& name) {
    return !name.empty() && name != L"." && name != L".." &&
           name.find_first_of(L"/\\\r\n") == std::wstring::npos;
}
std::wstring Join(const std::wstring& path, const std::wstring& name, wchar_t separator) {
    return path + (path.empty() || path.back() == separator ? L"" : std::wstring(1, separator)) + name;
}
std::wstring Parent(const std::wstring& path) {
    auto p = path;
    while (p.size() > 1 && p.back() == L'/') p.pop_back();
    auto slash = p.rfind(L'/');
    return slash == std::wstring::npos || slash == 0 ? L"/" : p.substr(0, slash);
}
void AddParent(std::vector<FileItem>& items, const std::wstring& path) {
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto& i) { return i.name == L".."; }), items.end());
    if (path != L"/") {
        FileItem item; item.name = L".."; item.path = Parent(path); item.isDir = true; item.kind = L"folder";
        items.insert(items.begin(), item);
    }
}
}

SftpController::SftpController(const Inventory& inv, RemoteFactory factory)
    : m_inv(inv), m_remoteFactory(std::move(factory)) {
    SetLocalPath(LocalFileSystem::DefaultPath());
}
SftpController::~SftpController() {
    SetOnStateChanged({});
    if (m_remoteFs) m_remoteFs->Cancel();
    if (m_worker.joinable()) m_worker.join();
}

void SftpController::StartWork(std::function<std::function<void()>()> work) {
    if (m_busy) return;
    if (m_worker.joinable()) m_worker.join();
    m_busy = true;
    const auto generation = m_generation;
    try { m_worker = std::thread([this, generation, work = std::move(work)] {
        std::function<void()> complete;
        try { complete = work(); }
        catch (...) { complete = [this, generation] {
            if (generation != m_generation) return;
            if (m_remoteState == SftpConnectionState::Connecting) m_remoteState = SftpConnectionState::Failed;
            SetStatusMessage(L"SFTP operation failed.");
        }; }
        {
            std::lock_guard lock(m_completionMtx);
            m_completion = std::move(complete);
        }
        NotifyStateChanged(); // notification only; never mutate UI state from this thread
    }); } catch (...) { m_busy = false; SetStatusMessage(L"Could not start the SFTP worker."); }
}
void SftpController::Poll() {
    std::function<void()> complete;
    { std::lock_guard lock(m_completionMtx); complete.swap(m_completion); }
    if (!complete) return;
    if (m_worker.joinable()) m_worker.join();
    m_busy = false;
    complete();
    if (m_pendingHost) {
        auto host = std::move(*m_pendingHost); m_pendingHost.reset(); ConnectRemote(host);
    } else if (m_pendingPath && m_remoteFs && !m_busy) {
        auto path = std::move(*m_pendingPath); m_pendingPath.reset(); LoadRemote(path);
    }
    NotifyStateChanged();
}

std::vector<FileItem> SftpController::LocalItems() const { return m_localItems; }
std::vector<FileItem> SftpController::RemoteItems() const { return m_remoteItems; }

void SftpController::SetLocalPath(const std::wstring& path) {
    wchar_t absolute[32768]{};
    DWORD count = GetFullPathNameW((path.empty() ? LocalFileSystem::DefaultPath() : path).c_str(), 32768, absolute, nullptr);
    if (!count || count >= 32768) { SetStatusMessage(L"Invalid local directory."); return; }
    std::wstring error; auto items = m_localFs.List(absolute, &error);
    if (!error.empty()) { SetStatusMessage(error); return; }
    m_localPath = absolute; m_localItems = std::move(items);
    SetStatusMessage(TrText(L"Local directory refreshed.", L"Yerel dizin yenilendi."));
}
void SftpController::RefreshLocal() { SetLocalPath(m_localPath); }
void SftpController::LocalNavigateUp() {
    auto path = std::filesystem::path(m_localPath).lexically_normal();
    if (path == path.root_path()) return;
    if (path.filename().empty()) path = path.parent_path();
    SetLocalPath(path.parent_path().wstring());
}
void SftpController::LocalNavigateDown(const std::wstring& name) {
    if (name == L"..") LocalNavigateUp();
    else if (ValidName(name)) SetLocalPath(Join(m_localPath, name, L'\\'));
}

void SftpController::ConnectRemote(const Host& host) {
    DisconnectRemote();
    if (m_busy) { m_pendingHost = host; return; }
    m_remoteFs = m_remoteFactory ? m_remoteFactory(host) : std::make_shared<RemoteSftpFileSystem>(host, m_inv);
    m_remoteState = SftpConnectionState::Connecting;
    m_remoteError.clear();
    LoadRemote(L".", true);
}
void SftpController::DisconnectRemote() {
    ++m_generation;
    if (m_remoteFs) m_remoteFs->Cancel();
    m_remoteFs.reset(); m_pendingPath.reset(); m_pendingHost.reset();
    m_remoteState = SftpConnectionState::Disconnected;
    { std::lock_guard lock(m_transferMtx); m_showTransferBanner = false; }
    m_remotePath = L"/"; m_remoteItems.clear(); m_remoteError.clear();
    SetStatusMessage(TrText(L"SFTP disconnected.", L"SFTP bağlantısı kesildi."));
}
void SftpController::LoadRemote(const std::wstring& path, bool connecting) {
    if (!m_remoteFs) return;
    if (m_busy) { m_pendingPath = path; return; }
    const auto fs = m_remoteFs;
    const auto generation = m_generation;
    SetStatusMessage(TrText(L"Loading directory...", L"Klasör yükleniyor..."));
    StartWork([this, fs, generation, path, connecting] {
        std::wstring error;
        fs->InvalidateCache();
        auto items = fs->List(path, &error);
        auto actual = fs->CurrentPath();
        return [this, generation, connecting, error, actual, items = std::move(items)]() mutable {
            if (generation != m_generation) return;
            if (!error.empty()) {
                if (connecting) { m_remoteState = SftpConnectionState::Failed; m_remoteError = error; }
                SetStatusMessage(error); // leave the previously displayed path/list intact
                return;
            }
            m_remotePath = actual.empty() ? L"/" : actual;
            AddParent(items, m_remotePath);
            m_remoteItems = std::move(items);
            m_remoteState = SftpConnectionState::Connected;
            m_remoteError.clear();
            SetStatusMessage(TrText(L"Remote directory refreshed.", L"Uzak dizin yenilendi."));
        };
    });
}
void SftpController::RefreshRemote() { LoadRemote(m_remotePath); }
void SftpController::SetRemotePath(const std::wstring& path) { LoadRemote(path.empty() ? L"/" : path); }
void SftpController::RemoteNavigateUp() { if (m_remotePath != L"/") LoadRemote(Parent(m_remotePath)); }
void SftpController::RemoteNavigateDown(const std::wstring& name) {
    if (name == L"..") RemoteNavigateUp();
    else if (ValidName(name)) LoadRemote(Join(m_remotePath, name, L'/'));
}

bool SftpController::CanModify(std::wstring* err) const {
    if (!m_remoteFs || m_remoteState != SftpConnectionState::Connected || m_busy) {
        if (err) *err = m_busy ? TrText(L"Wait for the current SFTP operation.", L"Devam eden SFTP işlemini bekleyin.")
                              : TrText(L"Not connected.", L"Bağlı değil.");
        return false;
    }
    return true;
}
bool SftpController::MutateRemote(const std::function<bool(RemoteSftpFileSystem&, std::wstring*)>& op, std::wstring* err) {
    if (!CanModify(err)) return false;
    auto fs = m_remoteFs; auto generation = m_generation;
    StartWork([this, fs, generation, op] {
        std::wstring error; bool ok = op(*fs, &error);
        return [this, generation, ok, error] {
            if (generation != m_generation) return;
            if (ok) RefreshRemote(); else SetStatusMessage(error);
        };
    });
    return true;
}
bool SftpController::DeleteRemoteItem(const std::wstring& name, bool, std::wstring* err) {
    auto item = std::find_if(m_remoteItems.begin(), m_remoteItems.end(), [&](const auto& f) { return f.name == name; });
    if (!ValidName(name) || item == m_remoteItems.end()) { if (err) *err = L"Invalid selection."; return false; }
    auto path = Join(m_remotePath, name, L'/'); bool directory = item->isDir && !item->isLink;
    return MutateRemote([path, directory](auto& fs, auto* error) { return fs.DeleteItem(path, directory, error); }, err);
}
bool SftpController::CreateRemoteFolder(const std::wstring& name, std::wstring* err) {
    if (!ValidName(name)) { if (err) *err = L"Invalid folder name."; return false; }
    auto path = Join(m_remotePath, name, L'/');
    return MutateRemote([path](auto& fs, auto* error) { return fs.MakeDirectory(path, error); }, err);
}
bool SftpController::RenameRemoteItem(const std::wstring& oldName, const std::wstring& newName, std::wstring* err) {
    if (!ValidName(oldName) || !ValidName(newName)) { if (err) *err = L"Invalid file name."; return false; }
    auto from = Join(m_remotePath, oldName, L'/'), to = Join(m_remotePath, newName, L'/');
    return MutateRemote([from, to](auto& fs, auto* error) { return fs.Rename(from, to, error); }, err);
}
bool SftpController::DeleteLocalItem(const std::wstring& name, bool, std::wstring* err) {
    auto item = std::find_if(m_localItems.begin(), m_localItems.end(), [&](const auto& f) { return f.name == name; });
    if (!ValidName(name) || item == m_localItems.end()) { if (err) *err = L"Invalid selection."; return false; }
    bool ok = m_localFs.DeleteItem(Join(m_localPath, name, L'\\'), item->isDir, err);
    if (ok) RefreshLocal(); return ok;
}
bool SftpController::CreateLocalFolder(const std::wstring& name, std::wstring* err) {
    if (!ValidName(name)) { if (err) *err = L"Invalid folder name."; return false; }
    bool ok = m_localFs.MakeDirectory(Join(m_localPath, name, L'\\'), err);
    if (ok) RefreshLocal(); return ok;
}
bool SftpController::RenameLocalItem(const std::wstring& oldName, const std::wstring& newName, std::wstring* err) {
    if (!ValidName(oldName) || !ValidName(newName)) { if (err) *err = L"Invalid file name."; return false; }
    bool ok = m_localFs.Rename(Join(m_localPath, oldName, L'\\'), Join(m_localPath, newName, L'\\'), err);
    if (ok) RefreshLocal(); return ok;
}

bool SftpController::Transfer(const std::wstring& name, bool upload, std::wstring* err) {
    if (!CanModify(err)) return false;
    const auto& items = upload ? m_localItems : m_remoteItems;
    auto item = std::find_if(items.begin(), items.end(), [&](const auto& f) { return f.name == name; });
    if (!ValidName(name) || item == items.end()) { if (err) *err = L"Invalid selection."; return false; }
    const auto local = Join(m_localPath, name, L'\\');
    const auto remote = upload ? m_remotePath : Join(m_remotePath, name, L'/');
    auto fs = m_remoteFs; auto generation = m_generation; bool isDir = item->isDir;
    {
        std::lock_guard lock(m_transferMtx);
        m_currentTransfer = {};
        m_currentTransfer.kind = upload ? TransferKind::Upload : TransferKind::Download;
        m_currentTransfer.status = TransferStatus::InProgress;
        m_currentTransfer.fileName = name; m_currentTransfer.localPath = local; m_currentTransfer.remotePath = remote;
        m_currentTransfer.startTime = GetTickCount64(); m_currentTransfer.statusText = L"Transferring...";
        m_showTransferBanner = true;
    }
    StartWork([this, fs, generation, upload, local, remote, isDir] {
        std::wstring error;
        auto progress = [this](int pct) { std::lock_guard lock(m_transferMtx); m_currentTransfer.progressPercent = std::clamp(pct, 0, 100); };
        bool ok = upload ? fs->Upload(local, remote, isDir, &error, progress) : fs->Download(remote, local, isDir, &error, progress);
        return [this, generation, upload, ok, error] {
            if (generation != m_generation) return;
            {
                std::lock_guard lock(m_transferMtx);
                m_currentTransfer.status = ok ? TransferStatus::Completed : TransferStatus::Failed;
                m_currentTransfer.finishTime = GetTickCount64();
                m_currentTransfer.progressPercent = ok ? 100 : m_currentTransfer.progressPercent;
                m_currentTransfer.statusText = ok ? L"Transfer complete." : error;
            }
            SetStatusMessage(ok ? L"Transfer complete." : error);
            if (ok) { if (upload) RefreshRemote(); else RefreshLocal(); }
        };
    });
    return true;
}
bool SftpController::UploadSelected(const std::wstring& name, std::wstring* err) { return Transfer(name, true, err); }
bool SftpController::DownloadSelected(const std::wstring& name, std::wstring* err) { return Transfer(name, false, err); }
bool SftpController::UploadFiles(const std::vector<std::wstring>& paths, std::wstring* err) {
    if (!CanModify(err)) return false;
    auto fs = m_remoteFs; auto generation = m_generation; auto destination = m_remotePath;
    StartWork([this, fs, generation, destination, paths] {
        std::wstring error;
        for (const auto& path : paths) {
            const DWORD attr = GetFileAttributesW(path.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES) { error = L"Local file is no longer available."; break; }
            if (!fs->Upload(path, destination, bool(attr & FILE_ATTRIBUTE_DIRECTORY), &error)) {
                if (error.empty()) error = L"Upload failed.";
                break;
            }
        }
        return [this, generation, error] {
            if (generation != m_generation) return;
            if (error.empty()) RefreshRemote(); else SetStatusMessage(error);
        };
    });
    return true;
}
bool SftpController::PreviewFile(const std::wstring& name, const std::wstring& destination, std::wstring* err) {
    if (!CanModify(err) || !ValidName(name)) return false;
    auto fs = m_remoteFs; auto generation = m_generation; auto source = Join(m_remotePath, name, L'/');
    StartWork([this, fs, generation, source, destination] {
        std::wstring error; bool ok = fs->Download(source, destination, &error);
        return [this, generation, destination, ok, error] {
            if (generation != m_generation) return;
            if (ok) ShellExecuteW(nullptr, L"open", destination.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else SetStatusMessage(error);
        };
    });
    return true;
}
bool SftpController::HasTransferBanner() const {
    std::lock_guard lock(m_transferMtx);
    return m_showTransferBanner && (m_currentTransfer.status == TransferStatus::InProgress ||
        GetTickCount64() - m_currentTransfer.finishTime < 18000);
}
TransferInfo SftpController::CurrentTransfer() const { std::lock_guard lock(m_transferMtx); return m_currentTransfer; }
void SftpController::DismissTransferBanner() { { std::lock_guard lock(m_transferMtx); m_showTransferBanner = false; } NotifyStateChanged(); }
std::vector<FileItem> SftpController::GetFilteredLocal(const std::wstring& filter) const {
    std::vector<FileItem> result;
    for (const auto& item : m_localItems) if (item.name == L".." || filter.empty() || StrStrIW(item.name.c_str(), filter.c_str())) result.push_back(item);
    return result;
}
std::vector<FileItem> SftpController::GetFilteredRemote(const std::wstring& filter) const {
    std::vector<FileItem> result;
    for (const auto& item : m_remoteItems) if (item.name == L".." || filter.empty() || StrStrIW(item.name.c_str(), filter.c_str())) result.push_back(item);
    return result;
}
} // namespace ft
