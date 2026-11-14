#include "model/SftpModel.h"
#include "core/ShellProfiles.h"
#include "core/Utf8.h"
#include <windows.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace ft;

struct FakeRemote : RemoteSftpFileSystem {
    std::atomic<bool> hold{false}, entered{false};
    std::wstring current = L"/home", lastDelete, lastCreatedFile;
    std::vector<std::wstring> uploadedFiles;
    bool deletedDirectory = false;
    FakeRemote(const Host& h, const Inventory& i) : RemoteSftpFileSystem(h, i) {}
    std::vector<FileItem> List(const std::wstring& path, std::wstring* err) override {
        entered = true;
        while (hold && !Cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (Cancelled()) { *err = L"Cancelled"; return {}; }
        if (path == L"/home/denied") { *err = L"Permission denied"; return {}; }
        current = path == L"." ? L"/home" : path;
        FileItem folder; folder.name = L"folder"; folder.isDir = true;
        FileItem marker; marker.name = current + L"-marker";
        return {folder, marker};
    }
    std::wstring CurrentPath() const override { return current; }
    bool CreateEmptyFile(const std::wstring& path, std::wstring*) override { lastCreatedFile = path; return true; }
    bool DeleteItem(const std::wstring& path, bool dir, std::wstring*) override { lastDelete = path; deletedDirectory = dir; return true; }
    bool Upload(const std::wstring& localFile, const std::wstring&, bool, std::wstring*, std::function<void(int pct)> onProgress) override {
        if (onProgress) onProgress(50);
        uploadedFiles.push_back(localFile);
        if (onProgress) onProgress(100);
        return true;
    }
};
struct TranscriptRemote : RemoteSftpFileSystem {
    mutable std::string lastBatch;
    bool fail = false;
    TranscriptRemote(const Host& h, const Inventory& i) : RemoteSftpFileSystem(h,i) {}
    int RunSftpBatch(const std::string& batch, std::string& output, std::wstring* err, std::function<void(int)>) const override {
        lastBatch = batch;
        if (fail) { if (err) *err = L"Transfer failed"; return 1; }
        auto directory = batch.find("/other") == std::string::npos ? "/home" : "/other";
        output = std::string("Remote working directory: ") + directory + "\n"
            "drwxr-xr-x 2 1000 1000 4096 Oct 8 12:00 folder with spaces\n"
            "-rw-r--r-- 1 1000 1000 14 Oct 8 12:00 report -> final.txt\n";
        return 0;
    }
};
static void Idle(SftpController& ctrl) {
    for (int i=0; i<3000; ++i) { ctrl.Poll(); if (!ctrl.Busy()) return; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    assert(false && "SFTP worker did not settle");
}
struct LocalProtocolRemote : RemoteSftpFileSystem {
    using RemoteSftpFileSystem::RemoteSftpFileSystem;
    int RunSftpBatch(const std::string& batch, std::string& output, std::wstring* err, std::function<void(int)> progress) const override {
        int code = RemoteSftpFileSystem::RunSftpBatch(batch, output, err, progress);
        if (code) std::cerr << "SFTP diagnostic: " << output << "\n";
        return code;
    }
    std::wstring BuildSftpCommand() const override {
        wchar_t shortServer[32768]{};
        assert(GetShortPathNameW(L"C:\\Program Files\\Git\\usr\\lib\\ssh\\sftp-server.exe", shortServer, 32768));
        return L"\"" + FindSftpExe() + L"\" -D \"" + shortServer + L"\"";
    }
};
static void RealSftpProtocol(const Host& host, const Inventory& inventory) {
    if (GetFileAttributesW(L"C:\\Program Files\\Git\\usr\\lib\\ssh\\sftp-server.exe") == INVALID_FILE_ATTRIBUTES) {
        std::cout << "SKIP: local OpenSSH SFTP server is not installed\n"; return;
    }
    // Git's server lives outside usr/bin, where its MSYS runtime DLL lives.
    wchar_t oldPath[32768]{};
    GetEnvironmentVariableW(L"PATH", oldPath, 32768);
    SetEnvironmentVariableW(L"PATH", (std::wstring(L"C:\\Program Files\\Git\\usr\\bin;") + oldPath).c_str());
    std::filesystem::create_directories("sandbox/remote files/child");
    for (int i = 0; i < 140; ++i) std::ofstream("sandbox/remote files/file-" + std::to_string(i) + ".txt") << "fixture";
    std::ofstream("sandbox/remote files/Permission denied failure not found.txt") << "valid filename";
    LocalProtocolRemote fs(host, inventory); std::wstring error;
    fs.List(L".", &error);
    if (!error.empty()) std::cerr << WideToUtf8(error) << "\n";
    assert(error.empty());
    auto base = fs.CurrentPath() + L"/sandbox/remote files";
    auto files = fs.List(base, &error);
    if (!error.empty()) std::cerr << WideToUtf8(error) << "\n";
    assert(error.empty());
    int count = 0;
    for (auto& item : files) if (item.name.starts_with(L"file-")) ++count;
    if (count != 140) std::cerr << "Real SFTP listed only " << count << " files\n";
    assert(count == 140);
    fs.List(base + L"/child", &error); assert(error.empty());
    assert(fs.CurrentPath() == base + L"/child");
    const auto cacheT0 = std::chrono::steady_clock::now();
    fs.List(base, &error);
    const auto cacheMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cacheT0).count();
    assert(error.empty() && fs.CurrentPath() == base);
    assert(cacheMs < 25); // cached parent revisit must not hit SFTP wire
    auto source = std::filesystem::absolute("sandbox/upload.txt"); std::ofstream(source) << "transfer-marker";
    if (!fs.Upload(source.wstring(), base, &error)) { std::cerr << WideToUtf8(error) << "\n"; assert(false); }
    auto destination = std::filesystem::absolute("sandbox/download.txt");
    if (!fs.Download(base + L"/upload.txt", destination.wstring(), &error)) { std::cerr << WideToUtf8(error) << "\n"; assert(false); }
    std::ifstream contents(destination); std::string line; std::getline(contents, line); assert(line == "transfer-marker");
    assert(fs.Rename(base + L"/upload.txt", base + L"/renamed.txt", &error));
    assert(fs.DeleteItem(base + L"/renamed.txt", false, &error));
    std::ofstream("sandbox/must-not-upload.txt") << "isolated regression fixture";
    assert(!fs.Upload(std::filesystem::absolute("sandbox/must-not-upload.txt").wstring(), base + L"/missing", &error));
    assert(!std::filesystem::exists("must-not-upload.txt")); // failed cd must abort put
    assert(fs.CurrentPath() == base);
    auto special = std::filesystem::absolute("sandbox/data[1].txt"); std::ofstream(special) << "literal brackets";
    assert(fs.Upload(special.wstring(), base, &error));
    auto specialDest = std::filesystem::absolute("sandbox/brackets-downloaded.txt");
    assert(fs.Download(base + L"/data[1].txt", specialDest.wstring(), &error));
    assert(fs.DeleteItem(base + L"/data[1].txt", false, &error));
    assert(fs.CreateEmptyFile(base + L"/created-empty.txt", &error));
    assert(std::filesystem::exists("sandbox/remote files/created-empty.txt") &&
           std::filesystem::file_size("sandbox/remote files/created-empty.txt") == 0);
    assert(fs.DeleteItem(base + L"/created-empty.txt", false, &error));
    assert(fs.SessionConnectCount() == 1); // all SFTP ops reused one persistent session!
    SetEnvironmentVariableW(L"PATH", oldPath);
    std::cout << "PASS: real OpenSSH SFTP protocol (1 persistent session), 140-file list, 0ms cache, paths with spaces, cd, upload/download/rename/delete\n";
}
int main() {
    std::filesystem::create_directories("sandbox/empty");
    std::filesystem::create_directories("sandbox/parent/child");
    Inventory inventory; Host host; host.address = L"fixture.invalid";
    std::shared_ptr<FakeRemote> remote;
    auto factory = [&](const Host& h) { remote = std::make_shared<FakeRemote>(h, inventory); return remote; };
    SftpController ctrl(inventory, factory);
    ctrl.SetLocalPath(std::filesystem::absolute("sandbox/empty").wstring());
    const auto empty = ctrl.LocalPath();
    ctrl.LocalNavigateDown(L"missing"); assert(ctrl.LocalPath() == empty);
    ctrl.SetLocalPath(std::filesystem::absolute("sandbox/parent").wstring());
    ctrl.LocalNavigateDown(L"child"); assert(std::filesystem::path(ctrl.LocalPath()).filename() == L"child");
    ctrl.LocalNavigateUp(); assert(std::filesystem::path(ctrl.LocalPath()).filename() == L"parent");
    ctrl.ConnectRemote(host); Idle(ctrl); assert(ctrl.RemotePath() == L"/home");
    remote->hold = true; remote->entered = false;
    ctrl.SetRemotePath(L"/home/slow");
    while (!remote->entered) std::this_thread::yield();
    assert(ctrl.RemotePath() == L"/home");
    ctrl.SetRemotePath(L"/other");
    remote->hold = false; Idle(ctrl); assert(ctrl.RemotePath() == L"/other");
    assert(ctrl.RemoteItems().back().name == L"/other-marker");
    ctrl.SetRemotePath(L"/home/denied"); Idle(ctrl);
    assert(ctrl.RemotePath() == L"/other" && ctrl.StatusMessage() == L"Permission denied");
    std::wstring error;
    assert(ctrl.DeleteRemoteItem(L"folder", false, &error)); Idle(ctrl);
    assert(remote->deletedDirectory && remote->lastDelete == L"/other/folder");
    assert(!ctrl.DeleteRemoteItem(L"..", true, &error));
    assert(ctrl.CreateRemoteFile(L"new-config.yaml", &error)); Idle(ctrl);
    assert(remote->lastCreatedFile == L"/other/new-config.yaml");

    // Local file creation & duplicate guard
    std::filesystem::remove("sandbox/parent/created_local.txt");
    assert(ctrl.CreateLocalFile(L"created_local.txt", &error));
    assert(std::filesystem::is_regular_file("sandbox/parent/created_local.txt") &&
           std::filesystem::file_size("sandbox/parent/created_local.txt") == 0);
    assert(!ctrl.CreateLocalFile(L"created_local.txt", &error));
    std::filesystem::remove("sandbox/parent/created_local.txt");

    // Column sorting (Name, Date via rawModified, Size numeric, Type/Permissions, '..' pinned, dirs grouped)
    {
        auto mkItem = [](const wchar_t* name, bool isDir, uint64_t size, const wchar_t* mod,
                         const wchar_t* kind, const wchar_t* perm, int64_t rawMod) {
            FileItem f;
            f.name = name;
            f.isDir = isDir;
            f.size = size;
            f.modified = mod;
            f.kind = kind;
            f.permissions = perm;
            f.rawModified = rawMod;
            return f;
        };
        std::vector<FileItem> sortItems = {
            mkItem(L"zeta.log",  false, 1200, L"01.01.2025 10:00", L"LOG",    L"-rw-r--r--", 300),
            mkItem(L"..",        true,  0,    L"",                 L"Folder", L"",           0),
            mkItem(L"alpha.bin", false, 15,   L"31.12.2024 10:00", L"BIN",    L"-rwxr-xr-x", 100),
            mkItem(L"docs",      true,  0,    L"05.05.2025 10:00", L"Folder", L"drwxr-xr-x", 500),
            mkItem(L"apps",      true,  0,    L"02.02.2024 10:00", L"Folder", L"drwxr-x---", 50),
            mkItem(L"beta.txt",  false, 2,    L"15.06.2025 10:00", L"TXT",    L"-rw-------", 200),
        };
        SortFileItems(sortItems, SftpSortField::Name, true, false);
        assert(sortItems[0].name == L".." && sortItems[1].name == L"apps" && sortItems[2].name == L"docs");
        assert(sortItems[3].name == L"alpha.bin" && sortItems[4].name == L"beta.txt" && sortItems[5].name == L"zeta.log");

        SortFileItems(sortItems, SftpSortField::Name, false, false);
        assert(sortItems[0].name == L".." && sortItems[1].name == L"docs" && sortItems[2].name == L"apps");
        assert(sortItems[3].name == L"zeta.log" && sortItems[4].name == L"beta.txt" && sortItems[5].name == L"alpha.bin");

        SortFileItems(sortItems, SftpSortField::Size, true, false);
        assert(sortItems[0].name == L".." && sortItems[3].name == L"beta.txt" &&
               sortItems[4].name == L"alpha.bin" && sortItems[5].name == L"zeta.log");

        SortFileItems(sortItems, SftpSortField::Size, false, false);
        assert(sortItems[0].name == L".." && sortItems[3].name == L"zeta.log" &&
               sortItems[4].name == L"alpha.bin" && sortItems[5].name == L"beta.txt");

        SortFileItems(sortItems, SftpSortField::Date, true, false);
        assert(sortItems[0].name == L".." && sortItems[1].name == L"apps" && sortItems[2].name == L"docs");
        assert(sortItems[3].name == L"alpha.bin" && sortItems[4].name == L"beta.txt" && sortItems[5].name == L"zeta.log");

        SortFileItems(sortItems, SftpSortField::Date, false, false);
        assert(sortItems[0].name == L".." && sortItems[1].name == L"docs" && sortItems[2].name == L"apps");
        assert(sortItems[3].name == L"zeta.log" && sortItems[4].name == L"beta.txt" && sortItems[5].name == L"alpha.bin");

        SortFileItems(sortItems, SftpSortField::Type, true, true);
        assert(sortItems[0].name == L".." && sortItems[3].name == L"beta.txt"); // -rw------- < -rw-r--r-- < -rwxr-xr-x
    }
    auto extA = std::filesystem::absolute("sandbox/ext-a.bin"); std::ofstream(extA) << "alpha";
    auto extB = std::filesystem::absolute("sandbox/ext-b.bin"); std::ofstream(extB) << "beta";
    assert(ctrl.UploadFiles({extA.wstring(), extB.wstring()}, &error));
    assert(ctrl.HasTransferBanner());
    assert(ctrl.CurrentTransfer().kind == TransferKind::Upload && ctrl.CurrentTransfer().totalFiles == 2);
    Idle(ctrl);
    assert(remote->uploadedFiles.size() == 2);
    assert(ctrl.HasTransferBanner());
    assert(ctrl.CurrentTransfer().status == TransferStatus::Completed && ctrl.CurrentTransfer().progressPercent == 100);
    ctrl.DismissTransferBanner();
    assert(!ctrl.HasTransferBanner());
    remote->hold = true; remote->entered = false; ctrl.SetRemotePath(L"/stale");
    while (!remote->entered) std::this_thread::yield();
    ctrl.DisconnectRemote(); Idle(ctrl);
    assert(ctrl.RemoteState() == SftpConnectionState::Disconnected && ctrl.RemoteItems().empty());
    {
        SftpController shortLived(inventory, factory);
        shortLived.ConnectRemote(host); Idle(shortLived);
        remote->hold = true; remote->entered = false; shortLived.RefreshRemote();
        while (!remote->entered) std::this_thread::yield();
    }
    assert(remote->Cancelled());
    TranscriptRemote transcript(host, inventory);
    error.clear(); auto items = transcript.List(L"/home", &error);
    assert(items.size() == 2 && items[1].name == L"report -> final.txt");
    transcript.List(L"/other", &error); assert(transcript.CurrentPath() == L"/other");
    transcript.List(L"/home", &error); assert(transcript.CurrentPath() == L"/home");
    transcript.Rename(L"/home/a\"b", L"/home/c", &error);
    assert(transcript.lastBatch.find("a\\\"b") != std::string::npos);
    std::ofstream("sandbox/existing.txt") << "old contents";
    transcript.fail = true;
    assert(!transcript.Download(L"/missing", std::filesystem::absolute("sandbox/existing.txt").wstring(), &error));
    RealSftpProtocol(host, inventory);
    std::cout << "PASS: SFTP queued navigation, atomic path/list, rollback, cancellation, cache, quoting, transfer errors\n";
}
