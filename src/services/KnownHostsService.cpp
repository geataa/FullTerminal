#include "services/KnownHostsService.h"
#include "core/Utf8.h"
#include "core/Settings.h"
#include "core/ShellProfiles.h"
#include "core/Sha256.h"

#include <fstream>
#include <sstream>
#include <algorithm>

namespace ft {

namespace {

static const char kB64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64EncodeNoPad(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (data[i] << 16);
        if (i + 1 < len) b |= (data[i + 1] << 8);
        if (i + 2 < len) b |= data[i + 2];

        out.push_back(kB64Chars[(b >> 18) & 0x3F]);
        out.push_back(kB64Chars[(b >> 12) & 0x3F]);
        if (i + 1 < len) out.push_back(kB64Chars[(b >> 6) & 0x3F]);
        if (i + 2 < len) out.push_back(kB64Chars[b & 0x3F]);
    }
    return out;
}

std::vector<unsigned char> Base64Decode(const std::string& in) {
    std::vector<unsigned char> out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)kB64Chars[i]] = i;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back((unsigned char)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

} // namespace

KnownHostsService::KnownHostsService() {
    std::wstring home = UserHomeDir();
    if (!home.empty()) {
        wchar_t sep = (home.find(L'/') != std::wstring::npos) ? L'/' : L'\\';
        m_filePath = home + sep + L".ssh" + sep + L"known_hosts";
    }
}

std::wstring KnownHostsService::ComputeFingerprint(const std::wstring& keyData) {
    std::string b64 = WideToUtf8(keyData);
    std::vector<unsigned char> rawKey = Base64Decode(b64);
    if (rawKey.empty()) return L"";

    auto hash = Sha256::Hash(rawKey.data(), rawKey.size());
    std::string hashB64 = Base64EncodeNoPad(hash.data(), hash.size());
    return L"SHA256:" + Utf8ToWide(hashB64);
}

bool KnownHostsService::Load(const std::wstring& customPath) {
    if (!customPath.empty()) {
        m_filePath = customPath;
    }
    m_entries.clear();
    m_rawLines.clear();

    if (m_filePath.empty()) return false;

    std::string u8path = WideToUtf8(m_filePath);
    std::ifstream file(u8path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    size_t lineIdx = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        m_rawLines.push_back(line);

        std::string trimmed = line;
        size_t start = trimmed.find_first_not_of(" \t");
        if (start != std::string::npos && trimmed[start] != '#') {
            std::istringstream iss(trimmed.substr(start));
            std::string token1;
            iss >> token1;

            bool isRevoked = false;
            std::string hostTok, typeTok, keyTok;

            if (token1 == "@revoked" || token1 == "@cert-authority") {
                isRevoked = (token1 == "@revoked");
                iss >> hostTok >> typeTok >> keyTok;
            } else {
                hostTok = token1;
                iss >> typeTok >> keyTok;
            }

            std::string comment;
            std::string remaining;
            std::getline(iss, remaining);
            size_t cStart = remaining.find_first_not_of(" \t");
            if (cStart != std::string::npos) {
                comment = remaining.substr(cStart);
            }

            if (!hostTok.empty() && !typeTok.empty() && !keyTok.empty()) {
                KnownHostEntry entry;
                entry.lineIndex = lineIdx;
                entry.filePath = m_filePath;
                entry.hostPattern = Utf8ToWide(hostTok);
                entry.keyType = Utf8ToWide(typeTok);
                entry.keyData = Utf8ToWide(keyTok);
                entry.comment = Utf8ToWide(comment);
                entry.isRevoked = isRevoked;
                entry.isHashed = (hostTok.rfind("|1|", 0) == 0);
                entry.fingerprint = ComputeFingerprint(entry.keyData);

                m_entries.push_back(entry);
            }
        }
        ++lineIdx;
    }

    return true;
}

bool KnownHostsService::DeleteEntry(size_t index) {
    if (index >= m_entries.size()) return false;
    size_t targetLine = m_entries[index].lineIndex;
    if (targetLine >= m_rawLines.size()) return false;

    m_rawLines.erase(m_rawLines.begin() + targetLine);

    std::string u8path = WideToUtf8(m_filePath);
    std::ofstream out(u8path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    for (const auto& l : m_rawLines) {
        out << l << "\n";
    }
    out.close();

    return Load(m_filePath);
}

bool KnownHostsService::DeleteHost(const std::wstring& host) {
    std::string h = WideToUtf8(host);
    bool changed = false;
    for (auto it = m_rawLines.begin(); it != m_rawLines.end(); ) {
        if (!it->empty() && (*it)[0] != '#') {
            std::istringstream iss(*it);
            std::string hostTok;
            iss >> hostTok;
            if (hostTok == h || hostTok.find(h + ",") == 0 || hostTok.find("," + h) != std::string::npos) {
                it = m_rawLines.erase(it);
                changed = true;
                continue;
            }
        }
        ++it;
    }
    if (changed) {
        std::string u8path = WideToUtf8(m_filePath);
        std::ofstream out(u8path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        for (const auto& l : m_rawLines) out << l << "\n";
        out.close();
        return Load(m_filePath);
    }
    return false;
}

bool KnownHostsService::AddEntry(const std::wstring& host, const std::wstring& keyType, const std::wstring& keyData) {
    if (m_filePath.empty()) return false;
    std::string u8path = WideToUtf8(m_filePath);
    std::ofstream out(u8path, std::ios::binary | std::ios::app);
    if (!out.is_open()) return false;
    out << WideToUtf8(host) << " " << WideToUtf8(keyType) << " " << WideToUtf8(keyData) << "\n";
    out.close();
    return Load(m_filePath);
}

} // namespace ft
