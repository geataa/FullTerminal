#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

namespace ft {

class Sha256 {
public:
    Sha256() { Reset(); }

    void Reset() {
        m_len = 0;
        m_state[0] = 0x6a09e667;
        m_state[1] = 0xbb67ae85;
        m_state[2] = 0x3c6ef372;
        m_state[3] = 0xa54ff53a;
        m_state[4] = 0x510e527f;
        m_state[5] = 0x9b05688c;
        m_state[6] = 0x1f83d9ab;
        m_state[7] = 0x5be0cd19;
    }

    void Update(const void* data, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            m_buf[m_len % 64] = p[i];
            m_len++;
            if (m_len % 64 == 0) {
                Transform(m_buf);
            }
        }
    }

    std::vector<uint8_t> Final() {
        uint64_t totalBits = m_len * 8;
        m_buf[m_len % 64] = 0x80;
        size_t padIdx = (m_len % 64) + 1;
        if (padIdx > 56) {
            while (padIdx < 64) m_buf[padIdx++] = 0;
            Transform(m_buf);
            padIdx = 0;
        }
        while (padIdx < 56) m_buf[padIdx++] = 0;
        for (int i = 7; i >= 0; --i) {
            m_buf[56 + (7 - i)] = static_cast<uint8_t>((totalBits >> (i * 8)) & 0xFF);
        }
        Transform(m_buf);

        std::vector<uint8_t> digest(32);
        for (int i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<uint8_t>((m_state[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((m_state[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((m_state[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(m_state[i] & 0xFF);
        }
        return digest;
    }

    static std::vector<uint8_t> Hash(const void* data, size_t len) {
        Sha256 s;
        s.Update(data, len);
        return s.Final();
    }

private:
    static inline uint32_t ROR(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
    static inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
    static inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
    static inline uint32_t Sigma0(uint32_t x) { return ROR(x, 2) ^ ROR(x, 13) ^ ROR(x, 22); }
    static inline uint32_t Sigma1(uint32_t x) { return ROR(x, 6) ^ ROR(x, 11) ^ ROR(x, 25); }
    static inline uint32_t Gamma0(uint32_t x) { return ROR(x, 7) ^ ROR(x, 18) ^ (x >> 3); }
    static inline uint32_t Gamma1(uint32_t x) { return ROR(x, 17) ^ ROR(x, 19) ^ (x >> 10); }

    void Transform(const uint8_t* block) {
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };

        uint32_t W[64];
        for (int i = 0; i < 16; ++i) {
            W[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) | (block[i * 4 + 2] << 8) | (block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            W[i] = Gamma1(W[i - 2]) + W[i - 7] + Gamma0(W[i - 15]) + W[i - 16];
        }

        uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
        uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + K[i] + W[i];
            uint32_t t2 = Sigma0(a) + Maj(a, b, c);
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
        m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
    }

    uint64_t m_len = 0;
    uint32_t m_state[8];
    uint8_t  m_buf[64];
};

} // namespace ft
