#pragma once
//
// KnownHostsService: OpenSSH known_hosts dosyasini okuma, ayristirma,
// listeleme, parmak izi (SHA256) hesaplama ve silme/duzenleme servisi.
//
#include <string>
#include <vector>

namespace ft {

struct KnownHostEntry {
    size_t       lineIndex = 0;
    std::wstring filePath;
    std::wstring hostPattern;
    std::wstring keyType;
    std::wstring keyData;
    std::wstring fingerprint;
    std::wstring comment;
    bool         isHashed = false;
    bool         isRevoked = false;
};

class KnownHostsService {
public:
    KnownHostsService();

    bool Load(const std::wstring& customPath = L"");
    const std::vector<KnownHostEntry>& Entries() const { return m_entries; }

    bool DeleteEntry(size_t index);
    bool DeleteHost(const std::wstring& host);
    bool AddEntry(const std::wstring& host, const std::wstring& keyType, const std::wstring& keyData);

    std::wstring GetFilePath() const { return m_filePath; }
    static std::wstring ComputeFingerprint(const std::wstring& keyData);

private:
    std::wstring m_filePath;
    std::vector<KnownHostEntry> m_entries;
    std::vector<std::string> m_rawLines;
};

} // namespace ft
