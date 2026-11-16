#include "services/KnownHostsService.h"
#include "core/Utf8.h"
#include "core/Settings.h"
#include "core/ShellProfiles.h"
#include "core/Sha256.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace ft {

namespace {

static const char kB64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline uint32_t Rotl32(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32u - n));
}

// OpenSSH HashKnownHosts (|1|salt|hash) dogrulamasi icin bagimsiz SHA-1 + HMAC-SHA1
std::array<uint8_t, 20> Sha1Hash(const uint8_t* data, size_t len) {
    uint32_t h0 = 0x67452301u;
    uint32_t h1 = 0xEFCDAB89u;
    uint32_t h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u;
    uint32_t h4 = 0xC3D2E1F0u;

    const uint64_t bitLen = (uint64_t)len * 8u;
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80u);
    while ((msg.size() % 64u) != 56u) msg.push_back(0x00u);
    for (int i = 7; i >= 0; --i) {
        msg.push_back((uint8_t)((bitLen >> (i * 8)) & 0xFFu));
    }

    uint32_t w[80];
    for (size_t off = 0; off < msg.size(); off += 64u) {
        for (int i = 0; i < 16; ++i) {
            const size_t p = off + (size_t)i * 4u;
            w[i] = ((uint32_t)msg[p] << 24) |
                   ((uint32_t)msg[p + 1] << 16) |
                   ((uint32_t)msg[p + 2] << 8) |
                   (uint32_t)msg[p + 3];
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = Rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const uint32_t tmp = Rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = Rotl32(b, 30);
            b = a;
            a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::array<uint8_t, 20> out{};
    const uint32_t hs[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i) {
        out[(size_t)i * 4 + 0] = (uint8_t)((hs[i] >> 24) & 0xFFu);
        out[(size_t)i * 4 + 1] = (uint8_t)((hs[i] >> 16) & 0xFFu);
        out[(size_t)i * 4 + 2] = (uint8_t)((hs[i] >> 8) & 0xFFu);
        out[(size_t)i * 4 + 3] = (uint8_t)(hs[i] & 0xFFu);
    }
    return out;
}

std::array<uint8_t, 20> HmacSha1(const uint8_t* key, size_t keyLen, const std::string& msg) {
    uint8_t kBlock[64]{};
    if (keyLen > 64u) {
        auto kh = Sha1Hash(key, keyLen);
        std::copy(kh.begin(), kh.end(), kBlock);
    } else if (keyLen > 0) {
        std::copy(key, key + keyLen, kBlock);
    }

    std::vector<uint8_t> inner(64u + msg.size());
    uint8_t outer[64u + 20u];
    for (size_t i = 0; i < 64u; ++i) {
        inner[i] = kBlock[i] ^ 0x36u;
        outer[i] = kBlock[i] ^ 0x5Cu;
    }
    if (!msg.empty()) {
        std::copy(msg.begin(), msg.end(), inner.begin() + 64u);
    }
    auto ih = Sha1Hash(inner.data(), inner.size());
    std::copy(ih.begin(), ih.end(), outer + 64u);
    return Sha1Hash(outer, sizeof(outer));
}

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

struct HashedHostParsed {
    std::vector<unsigned char> salt;
    std::array<uint8_t, 20> hash{};
    bool valid = false;
};

HashedHostParsed ParseOpenSshHash(const std::string& hostTok) {
    HashedHostParsed res;
    if (hostTok.rfind("|1|", 0) != 0) return res;
    const size_t sep = hostTok.find('|', 3);
    if (sep == std::string::npos || sep <= 3 || sep + 1 >= hostTok.size()) return res;
    res.salt = Base64Decode(hostTok.substr(3, sep - 3));
    std::vector<unsigned char> h = Base64Decode(hostTok.substr(sep + 1));
    if (res.salt.empty() || h.size() != 20u) return res;
    std::copy(h.begin(), h.end(), res.hash.begin());
    res.valid = true;
    return res;
}

bool IsPlausibleHostToken(const std::string& s) {
    if (s.empty() || s.size() > 253 || s.front() == '-' || s.front() == '.' || s == "*") return false;
    for (unsigned char c : s) {
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == ':')) return false;
    }
    return true;
}

std::string LowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return s;
}

struct HostCandidateSet {
    // OpenSSH known_hosts anahtar deseni: "host" veya "[host]:port"
    std::vector<std::string> patterns;
    std::unordered_set<std::string> seenPatterns;
    std::unordered_map<std::string, std::string> patternUser;

    void AddHostPort(const std::string& rawHost, int port, const std::string& user = "") {
        std::string h = rawHost;
        if (h.size() >= 2 && h.front() == '[' && h.back() == ']') {
            h = h.substr(1, h.size() - 2);
        }
        h = LowerAscii(h);
        if (!IsPlausibleHostToken(h)) return;
        auto addPat = [&](const std::string& pat) {
            if (seenPatterns.insert(pat).second) {
                patterns.push_back(pat);
            }
            if (!user.empty() && patternUser[pat].empty()) {
                patternUser[pat] = user;
            }
        };
        if (port > 0 && port != 22 && port <= 65535) {
            addPat("[" + h + "]:" + std::to_string(port));
        }
        addPat(h);
    }

    void AddKnownHostToken(const std::string& tok) {
        if (tok.empty() || tok.rfind("|1|", 0) == 0) return;
        size_t pos = 0;
        while (pos < tok.size()) {
            size_t comma = tok.find(',', pos);
            std::string item = tok.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            pos = (comma == std::string::npos) ? tok.size() : comma + 1;
            if (item.empty() || item.find('*') != std::string::npos || item.find('?') != std::string::npos) continue;
            if (item.front() == '[') {
                size_t rb = item.find(']');
                if (rb != std::string::npos && rb > 1) {
                    int port = 22;
                    if (rb + 2 < item.size() && item[rb + 1] == ':') {
                        try { port = std::stoi(item.substr(rb + 2)); } catch (...) { port = 22; }
                    }
                    AddHostPort(item.substr(1, rb - 1), port);
                }
            } else {
                AddHostPort(item, 22);
            }
        }
    }
};

void HarvestSshConfigFile(const std::filesystem::path& cfgPath, HostCandidateSet& out) {
    std::ifstream cf(cfgPath);
    if (!cf.is_open()) return;
    std::string line;
    std::vector<std::string> curAliases;
    std::string curHostName;
    std::string curUser;
    int curPort = 22;

    auto flush = [&]() {
        for (const auto& a : curAliases) out.AddHostPort(a, curPort, curUser);
        if (!curHostName.empty()) out.AddHostPort(curHostName, curPort, curUser);
        curAliases.clear();
        curHostName.clear();
        curUser.clear();
        curPort = 22;
    };

    while (std::getline(cf, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos || line[s] == '#') continue;
        std::istringstream iss(line.substr(s));
        std::string key;
        iss >> key;
        key = LowerAscii(key);
        if (key == "host") {
            flush();
            std::string alias;
            while (iss >> alias) {
                if (alias.find('*') == std::string::npos && alias.find('?') == std::string::npos && alias.front() != '!') {
                    curAliases.push_back(alias);
                }
            }
        } else if (key == "hostname") {
            iss >> curHostName;
        } else if (key == "user") {
            iss >> curUser;
        } else if (key == "port") {
            std::string ps;
            iss >> ps;
            try { curPort = std::stoi(ps); } catch (...) { curPort = 22; }
        }
    }
    flush();
}

void HarvestShellHistoryFile(const std::filesystem::path& histPath, HostCandidateSet& out) {
    std::ifstream hf(histPath, std::ios::binary);
    if (!hf.is_open()) return;
    std::string line;
    while (std::getline(hf, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        // zsh extended history: ": 1700000000:0;ssh user@host"
        if (line[0] == ':' && line.size() > 3) {
            const size_t sc = line.find(';');
            if (sc != std::string::npos && sc + 1 < line.size()) line = line.substr(sc + 1);
        } else if (line.rfind("- cmd: ", 0) == 0) {
            // fish history
            line = line.substr(7);
        }
        if (line.find("ssh") == std::string::npos &&
            line.find("sftp") == std::string::npos &&
            line.find("scp") == std::string::npos) {
            continue;
        }

        // Noktali virgul / boru ile ayrilmis alt komutlari tara
        for (char& c : line) {
            if (c == ';' || c == '|' || c == '&' || c == '"' || c == '\'') c = ' ';
        }
        std::istringstream iss(line);
        std::vector<std::string> toks;
        std::string tok;
        while (iss >> tok) toks.push_back(tok);

        for (size_t i = 0; i < toks.size(); ++i) {
            std::string cmd = toks[i];
            const size_t slash = cmd.rfind('/');
            if (slash != std::string::npos) cmd = cmd.substr(slash + 1);
            if (cmd != "ssh" && cmd != "sftp" && cmd != "scp") continue;

            int port = 22;
            std::string user;
            std::vector<std::string> posArgs;
            for (size_t k = i + 1; k < toks.size(); ++k) {
                const std::string& a = toks[k];
                if (a == "ssh" || a == "sftp" || a == "scp" || a == "sudo") break;
                if ((cmd == "ssh" && a == "-p") || ((cmd == "sftp" || cmd == "scp") && a == "-P")) {
                    if (k + 1 < toks.size()) {
                        try { port = std::stoi(toks[++k]); } catch (...) { port = 22; }
                    }
                    continue;
                }
                if ((cmd == "ssh" && a.rfind("-p", 0) == 0 && a.size() > 2) ||
                    ((cmd == "sftp" || cmd == "scp") && a.rfind("-P", 0) == 0 && a.size() > 2)) {
                    try { port = std::stoi(a.substr(2)); } catch (...) { port = 22; }
                    continue;
                }
                if (cmd == "ssh" && a == "-l" && k + 1 < toks.size()) {
                    user = toks[++k];
                    continue;
                }
                if (a == "-i" || a == "-F" || a == "-J" || a == "-L" || a == "-R" ||
                    a == "-D" || a == "-W" || a == "-o" || a == "-b" || a == "-c" ||
                    a == "-E" || a == "-I" || a == "-m" || a == "-O" || a == "-S" || a == "-w") {
                    if (k + 1 < toks.size()) ++k;
                    continue;
                }
                if (!a.empty() && a[0] == '-') continue;
                posArgs.push_back(a);
            }

            for (const std::string& rawTarget : posArgs) {
                std::string t = rawTarget;
                if (t.rfind("ssh://", 0) == 0) t = t.substr(6);
                else if (t.rfind("sftp://", 0) == 0) t = t.substr(7);
                else if (t.rfind("scp://", 0) == 0) t = t.substr(6);

                std::string tUser = user;
                const size_t at = t.rfind('@');
                if (at != std::string::npos) {
                    if (at > 0) tUser = t.substr(0, at);
                    t = t.substr(at + 1);
                }

                if (cmd == "scp") {
                    const size_t colon = t.find(':');
                    if (colon == std::string::npos) continue; // yerel dosya argumani
                    t = t.substr(0, colon);
                } else {
                    const size_t slashPos = t.find('/');
                    if (slashPos != std::string::npos) t = t.substr(0, slashPos);
                    const size_t colon = t.find(':');
                    if (colon != std::string::npos) {
                        std::string after = t.substr(colon + 1);
                        t = t.substr(0, colon);
                        if (cmd != "sftp" && !after.empty() && std::all_of(after.begin(), after.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                            try { port = std::stoi(after); } catch (...) {}
                        }
                    }
                }

                if (IsPlausibleHostToken(t)) {
                    out.AddHostPort(t, port, tUser);
                    if (cmd != "scp") break; // ssh/sftp'de ilk konumsal arguman sunucudur, sonrakiler uzak komuttur
                }
            }
        }
    }
}

void ResolveHashedKnownHosts(const std::wstring& filePath, std::vector<KnownHostEntry>& entries) {
    bool anyHashed = false;
    for (const auto& e : entries) {
        if (e.isHashed && !e.isRevoked) { anyHashed = true; break; }
    }
    if (!anyHashed) return;

    HostCandidateSet cands;
    for (const auto& e : entries) {
        if (!e.isHashed) cands.AddKnownHostToken(WideToUtf8(e.hostPattern));
    }

    std::error_code ec;
    const std::filesystem::path khPath(filePath);
    const std::filesystem::path sshDir = khPath.parent_path();

    // Ayni dizindeki known_hosts*, config, config.d/* dosyalarini tara
    if (!sshDir.empty() && std::filesystem::exists(sshDir, ec)) {
        for (const auto& item : std::filesystem::directory_iterator(sshDir, ec)) {
            if (!item.is_regular_file(ec)) continue;
            const std::string fname = item.path().filename().string();
            if (fname.rfind("known_hosts", 0) == 0 && item.path() != khPath) {
                std::ifstream f(item.path(), std::ios::binary);
                std::string line;
                while (std::getline(f, line)) {
                    const size_t s = line.find_first_not_of(" \t");
                    if (s == std::string::npos || line[s] == '#') continue;
                    std::istringstream iss(line.substr(s));
                    std::string tok;
                    iss >> tok;
                    if (tok == "@revoked" || tok == "@cert-authority") iss >> tok;
                    cands.AddKnownHostToken(tok);
                }
            } else if (fname == "config" || fname.rfind("config.", 0) == 0) {
                HarvestSshConfigFile(item.path(), cands);
            }
        }
        const std::filesystem::path cfgD = sshDir / "config.d";
        if (std::filesystem::is_directory(cfgD, ec)) {
            for (const auto& item : std::filesystem::directory_iterator(cfgD, ec)) {
                if (item.is_regular_file(ec)) HarvestSshConfigFile(item.path(), cands);
            }
        }
        // Birim testi veya ozel dizin icindeki yerel gecmis dosyasi
        if (std::filesystem::is_regular_file(sshDir / ".bash_history", ec)) {
            HarvestShellHistoryFile(sshDir / ".bash_history", cands);
        }
    }

    // Kullanicinin varsayilan ~/.ssh dizini yukleniyorsa /etc/hosts ve kabuk gecmisini de tara
    const std::wstring homeW = UserHomeDir();
    if (!homeW.empty()) {
        const std::filesystem::path homeDir(homeW);
        const std::filesystem::path defaultSshDir = homeDir / ".ssh";
        std::error_code eqEc;
        if (sshDir == defaultSshDir || std::filesystem::equivalent(sshDir, defaultSshDir, eqEc)) {
            std::ifstream hostsFile("/etc/hosts");
            if (hostsFile.is_open()) {
                std::string line;
                while (std::getline(hostsFile, line)) {
                    const size_t hashPos = line.find('#');
                    if (hashPos != std::string::npos) line.resize(hashPos);
                    std::istringstream iss(line);
                    std::string ip, alias;
                    if (!(iss >> ip)) continue;
                    if (ip == "127.0.0.1" || ip == "::1" || ip.rfind("fe00:", 0) == 0 || ip.rfind("ff0", 0) == 0) {
                        continue;
                    }
                    cands.AddHostPort(ip, 22);
                    while (iss >> alias) cands.AddHostPort(alias, 22);
                }
            }
            HarvestShellHistoryFile(homeDir / ".bash_history", cands);
            HarvestShellHistoryFile(homeDir / ".zsh_history", cands);
            HarvestShellHistoryFile(homeDir / ".local" / "share" / "fish" / "fish_history", cands);
        }
    }

    if (cands.patterns.empty()) return;

    for (auto& e : entries) {
        if (!e.isHashed || e.isRevoked) continue;
        const HashedHostParsed parsed = ParseOpenSshHash(WideToUtf8(e.hostPattern));
        if (!parsed.valid) continue;
        for (const std::string& cand : cands.patterns) {
            if (HmacSha1(parsed.salt.data(), parsed.salt.size(), cand) == parsed.hash) {
                e.resolvedHost = Utf8ToWide(cand);
                auto uit = cands.patternUser.find(cand);
                if (uit != cands.patternUser.end() && !uit->second.empty()) {
                    e.resolvedUser = Utf8ToWide(uit->second);
                }
                break;
            }
        }
    }
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

    ResolveHashedKnownHosts(m_filePath, m_entries);
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
    std::string hLower = LowerAscii(h);
    bool changed = false;
    for (auto it = m_rawLines.begin(); it != m_rawLines.end(); ) {
        if (!it->empty() && (*it)[0] != '#') {
            std::istringstream iss(*it);
            std::string hostTok;
            iss >> hostTok;
            if (hostTok == "@revoked" || hostTok == "@cert-authority") iss >> hostTok;
            bool match = (hostTok == h || hostTok.find(h + ",") == 0 || hostTok.find("," + h) != std::string::npos);
            if (!match && hostTok.rfind("|1|", 0) == 0) {
                const HashedHostParsed parsed = ParseOpenSshHash(hostTok);
                if (parsed.valid && HmacSha1(parsed.salt.data(), parsed.salt.size(), hLower) == parsed.hash) {
                    match = true;
                }
            }
            if (match) {
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
