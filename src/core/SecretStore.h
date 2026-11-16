#pragma once
//
// FullTerminal — Linux gizli bilgi koruma (Windows DPAPI CryptProtectData karsiligi)
//
// Windows'ta MCP token'i ve veritabani parolalari DPAPI ile kullanici hesabina bagli
// sifreleniyor. Linux'ta her masaustunde calisan ortak bir anahtarlik yok; bu yuzden:
//   anahtar   = SHA-256("FullTerminal secret v1" | /etc/machine-id | uid | purpose)
//   akis      = SHA-256(anahtar | nonce | sayac) bloklari (CTR modu)
//   dogrulama = SHA-256(anahtar | nonce | sifreli metin) ilk 16 bayt
// Cikti: "lx2:" + hex(nonce16 | tag16 | ciphertext). Baska makineye/kullaniciya kopyalanan
// dosyada cozulemez (DPAPI'deki gibi "kilitli" olarak raporlanir) ve duz metin gorunmez.
// Dosyalar ayrica 0600 izinle yaziliyor.
//
#if !defined(_WIN32)
#include "core/Sha256.h"
#include "core/Utf8.h"

#include <string>
#include <vector>
#include <fstream>
#include <random>
#include <cstdint>
#include <unistd.h>

namespace ft::secret {

inline std::vector<uint8_t> Key(const char* purpose) {
    std::string material = "FullTerminal secret v1|";
    for (const char* f : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
        std::ifstream in(f);
        std::string id;
        if (in && std::getline(in, id) && !id.empty()) { material += id; break; }
    }
    material += "|" + std::to_string((unsigned long)getuid()) + "|" + (purpose ? purpose : "");
    return Sha256::Hash(material.data(), material.size());
}

inline std::vector<uint8_t> Block(const std::vector<uint8_t>& key, const uint8_t* nonce, uint32_t counter) {
    std::vector<uint8_t> buf(key);
    buf.insert(buf.end(), nonce, nonce + 16);
    for (int i = 0; i < 4; ++i) buf.push_back((uint8_t)(counter >> (8 * i)));
    return Sha256::Hash(buf.data(), buf.size());
}

inline std::vector<uint8_t> Tag(const std::vector<uint8_t>& key, const uint8_t* nonce, const std::string& cipher) {
    std::vector<uint8_t> buf(key);
    buf.push_back(0x54);
    buf.insert(buf.end(), nonce, nonce + 16);
    buf.insert(buf.end(), cipher.begin(), cipher.end());
    auto h = Sha256::Hash(buf.data(), buf.size());
    h.resize(16);
    return h;
}

inline std::string Protect(const std::wstring& value, const char* purpose) {
    if (value.empty()) return {};
    const auto key = Key(purpose);
    uint8_t nonce[16];
    std::random_device rd;
    for (auto& b : nonce) b = (uint8_t)rd();
    std::string plain = WideToUtf8(value);
    std::string cipher(plain.size(), '\0');
    for (size_t i = 0; i < plain.size(); i += 32) {
        const auto ks = Block(key, nonce, (uint32_t)(i / 32));
        for (size_t j = 0; j < 32 && i + j < plain.size(); ++j) cipher[i + j] = (char)(plain[i + j] ^ ks[j]);
    }
    std::fill(plain.begin(), plain.end(), '\0');
    const auto tag = Tag(key, nonce, cipher);
    static const char* hex = "0123456789abcdef";
    std::string out = "lx2:";
    auto put = [&](uint8_t b) { out += hex[b >> 4]; out += hex[b & 15]; };
    for (uint8_t b : nonce) put(b);
    for (uint8_t b : tag) put(b);
    for (char c : cipher) put((uint8_t)c);
    return out;
}

inline bool Unprotect(const std::string& text, std::wstring& value, const char* purpose) {
    if (text.rfind("lx2:", 0) != 0) return false;
    const std::string h = text.substr(4);
    if (h.size() % 2 || h.size() < 64) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<uint8_t> raw(h.size() / 2);
    for (size_t i = 0; i < raw.size(); ++i) {
        const int a = nib(h[2 * i]), b = nib(h[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        raw[i] = (uint8_t)((a << 4) | b);
    }
    const uint8_t* nonce = raw.data();
    const uint8_t* tag = raw.data() + 16;
    const std::string cipher(raw.begin() + 32, raw.end());
    const auto key = Key(purpose);
    const auto expect = Tag(key, nonce, cipher);
    uint8_t diff = 0;
    for (int i = 0; i < 16; ++i) diff |= (uint8_t)(expect[i] ^ tag[i]);
    if (diff) return false; // baska makine/kullanici veya bozuk veri
    std::string plain(cipher.size(), '\0');
    for (size_t i = 0; i < cipher.size(); i += 32) {
        const auto ks = Block(key, nonce, (uint32_t)(i / 32));
        for (size_t j = 0; j < 32 && i + j < cipher.size(); ++j) plain[i + j] = (char)(cipher[i + j] ^ ks[j]);
    }
    value = Utf8ToWide(plain);
    std::fill(plain.begin(), plain.end(), '\0');
    return true;
}

} // namespace ft::secret
#endif
