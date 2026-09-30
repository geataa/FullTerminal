#pragma once
//
// SftpModel: SFTP ve Yerel Dosya Yoneticisi Modeli.
// SOLID prensiplerine uygun, IFileSystem arayuzu uzerinden hem yerel
// hem uzak dosya sistemlerini yoneten cift panelli (dual-pane) mimari.
//
#include "model/Inventory.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>

namespace ft {

struct FileItem {
    std::wstring name;
    std::wstring path;
    uint64_t     size = 0;
    std::wstring modified;      // ornek: "9/24/2026, 12:05 AM"
    int64_t      rawModified = 0;
    bool         isDir = false;
    std::wstring kind;          // "folder", "dll", "log", "exe", "txt", vb.
    std::wstring permissions;   // "drwxr-xr-x"
    bool         selected = false;
};

// IFileSystem arayuzu (SOLID - Interface Segregation & Dependency Inversion)
class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual std::vector<FileItem> List(const std::wstring& path, std::wstring* err) = 0;
    virtual bool MakeDirectory(const std::wstring& path, std::wstring* err) = 0;
    virtual bool DeleteItem(const std::wstring& path, bool isDir, std::wstring* err) = 0;
    virtual bool Rename(const std::wstring& oldPath, const std::wstring& newPath, std::wstring* err) = 0;
};

// Yerel Win32 dosya sistemi
class LocalFileSystem : public IFileSystem {
public:
    std::vector<FileItem> List(const std::wstring& path, std::wstring* err) override;
    bool MakeDirectory(const std::wstring& path, std::wstring* err) override;
    bool DeleteItem(const std::wstring& path, bool isDir, std::wstring* err) override;
    bool Rename(const std::wstring& oldPath, const std::wstring& newPath, std::wstring* err) override;

    static std::wstring DefaultPath();
    static std::wstring FormatFileSize(uint64_t bytes);
    static std::wstring GetFileKind(const std::wstring& name, bool isDir);
    static std::vector<std::wstring> GetDrives();
};

enum class SftpConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Failed
};

enum class TransferKind {
    Upload,
    Download
};

enum class TransferStatus {
    InProgress,
    Completed,
    Failed
};

struct TransferInfo {
    TransferKind   kind = TransferKind::Download;
    TransferStatus status = TransferStatus::InProgress;
    std::wstring   fileName;
    std::wstring   localPath;
    std::wstring   remotePath;
    std::wstring   statusText;
    int            progressPercent = 0;
    int64_t        startTime = 0;
    int64_t        finishTime = 0;
};

// Uzak SFTP dosya sistemi (OpenSSH sftp.exe / ssh.exe uzerinden)
class RemoteSftpFileSystem : public IFileSystem {
public:
    explicit RemoteSftpFileSystem(const Host& host, const Inventory& inv);
    ~RemoteSftpFileSystem() override;

    std::vector<FileItem> List(const std::wstring& path, std::wstring* err) override;
    bool MakeDirectory(const std::wstring& path, std::wstring* err) override;
    bool DeleteItem(const std::wstring& path, bool isDir, std::wstring* err) override;
    bool Rename(const std::wstring& oldPath, const std::wstring& newPath, std::wstring* err) override;

    bool Download(const std::wstring& remoteFile, const std::wstring& localDest, std::wstring* err) {
        return Download(remoteFile, localDest, false, err, nullptr);
    }
    bool Download(const std::wstring& remoteFile, const std::wstring& localDest, bool isDir, std::wstring* err,
                  std::function<void(int pct)> onProgress = nullptr);

    bool Upload(const std::wstring& localFile, const std::wstring& remoteDest, std::wstring* err) {
        return Upload(localFile, remoteDest, false, err, nullptr);
    }
    bool Upload(const std::wstring& localFile, const std::wstring& remoteDest, bool isDir, std::wstring* err,
                std::function<void(int pct)> onProgress = nullptr);

    void InvalidateCache() {
        std::lock_guard<std::mutex> lock(m_cacheMtx);
        m_cache.clear();
    }

    const Host& GetHost() const { return m_host; }
    const std::wstring& CurrentPath() const { return m_currentRemotePath; }

private:
    std::wstring BuildSftpCommand() const;
    int RunSftpBatch(const std::string& batchCommands, std::string& output, std::wstring* err,
                     std::function<void(int pct)> onProgress = nullptr) const;

    Host m_host;
    const Inventory& m_inv;
    std::wstring m_sshExe;
    std::wstring m_sftpExe;
    mutable std::wstring m_currentRemotePath = L"/";
    mutable std::mutex m_cacheMtx;
    mutable std::map<std::wstring, std::pair<int64_t, std::vector<FileItem>>> m_cache;
};

// Dual-pane SFTP Kontrolcusu
class SftpController {
public:
    explicit SftpController(const Inventory& inv);
    ~SftpController();

    // Yerel Panel
    void SetLocalPath(const std::wstring& path);
    const std::wstring& LocalPath() const { return m_localPath; }
    std::vector<FileItem> LocalItems() const;
    void RefreshLocal();
    void LocalNavigateUp();
    void LocalNavigateDown(const std::wstring& folderName);

    // Uzak Panel
    void ConnectRemote(const Host& host);
    void DisconnectRemote();
    SftpConnectionState RemoteState() const { return m_remoteState; }
    const std::wstring& RemoteError() const { return m_remoteError; }
    void SetRemotePath(const std::wstring& path);
    const std::wstring& RemotePath() const { return m_remotePath; }
    std::vector<FileItem> RemoteItems() const;
    const Host* ConnectedHost() const { return m_remoteFs ? &m_remoteFs->GetHost() : nullptr; }
    RemoteSftpFileSystem* RemoteFs() const { return m_remoteFs.get(); }
    void RefreshRemote();
    void RemoteNavigateUp();
    void RemoteNavigateDown(const std::wstring& folderName);

    // Transfer Islemleri
    bool UploadSelected(const std::wstring& localFileName, std::wstring* err);
    bool DownloadSelected(const std::wstring& remoteFileName, std::wstring* err);
    bool DeleteLocalItem(const std::wstring& name, bool isDir, std::wstring* err);
    bool DeleteRemoteItem(const std::wstring& name, bool isDir, std::wstring* err);
    bool CreateLocalFolder(const std::wstring& name, std::wstring* err);
    bool CreateRemoteFolder(const std::wstring& name, std::wstring* err);
    bool RenameRemoteItem(const std::wstring& oldName, const std::wstring& newName, std::wstring* err);
    bool RenameLocalItem(const std::wstring& oldName, const std::wstring& newName, std::wstring* err);

    // Transfer Banner & Notification API
    bool HasTransferBanner() const;
    TransferInfo CurrentTransfer() const;
    void DismissTransferBanner();
    void SetOnStateChanged(std::function<void()> cb) {
        std::lock_guard<std::mutex> lock(m_cbMtx);
        m_onStateChanged = std::move(cb);
    }
    void NotifyStateChanged() {
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lock(m_cbMtx);
            cb = m_onStateChanged;
        }
        if (cb) cb();
    }

    // Filtreleme
    std::vector<FileItem> GetFilteredLocal(const std::wstring& filter) const;
    std::vector<FileItem> GetFilteredRemote(const std::wstring& filter) const;

    // Asenkron durum
    bool Busy() const { return m_busy; }
    std::wstring StatusMessage() const {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_statusMsg;
    }
    void SetStatusMessage(const std::wstring& msg) {
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_statusMsg = msg;
        }
        NotifyStateChanged();
    }

private:
    const Inventory& m_inv;
    LocalFileSystem m_localFs;
    std::unique_ptr<RemoteSftpFileSystem> m_remoteFs;

    std::wstring m_localPath;
    std::vector<FileItem> m_localItems;

    std::wstring m_remotePath = L"/";
    std::vector<FileItem> m_remoteItems;
    SftpConnectionState m_remoteState = SftpConnectionState::Disconnected;
    std::wstring m_remoteError;

    std::atomic<bool> m_busy{ false };
    mutable std::mutex m_mtx;
    mutable std::mutex m_itemsMtx;
    std::wstring m_statusMsg;

    // Transfer HUD Banner
    mutable std::mutex m_transferMtx;
    TransferInfo m_currentTransfer;
    bool m_showTransferBanner = false;

    mutable std::mutex m_cbMtx;
    std::function<void()> m_onStateChanged;
};

} // namespace ft

