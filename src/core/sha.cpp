#include "core/sha.h"

#include <cstring>

#include "core/util.h"

namespace tgnx {

namespace {

inline uint32_t rol32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

}  // namespace

Sha1::Sha1() : total_(0), buffered_(0) {
    h_[0] = 0x67452301;
    h_[1] = 0xEFCDAB89;
    h_[2] = 0x98BADCFE;
    h_[3] = 0x10325476;
    h_[4] = 0xC3D2E1F0;
}

void Sha1::processBlock(const uint8_t* block) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) w[i] = loadBe32(block + i * 4);
    for (int i = 16; i < 80; ++i) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
}

void Sha1::update(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    total_ += size;
    if (buffered_) {
        size_t take = 64 - buffered_;
        if (take > size) take = size;
        std::memcpy(buffer_ + buffered_, p, take);
        buffered_ += take;
        p += take;
        size -= take;
        if (buffered_ == 64) {
            processBlock(buffer_);
            buffered_ = 0;
        }
    }
    while (size >= 64) {
        processBlock(p);
        p += 64;
        size -= 64;
    }
    if (size) {
        std::memcpy(buffer_, p, size);
        buffered_ = size;
    }
}

std::array<uint8_t, 20> Sha1::finish() {
    uint64_t bitLength = total_ * 8;
    uint8_t padding[72] = {0x80};
    size_t padLen = (buffered_ < 56) ? (56 - buffered_) : (120 - buffered_);
    update(padding, padLen);
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i) lenBytes[i] = uint8_t(bitLength >> (56 - i * 8));
    update(lenBytes, 8);

    std::array<uint8_t, 20> out;
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = uint8_t(h_[i] >> 24);
        out[i * 4 + 1] = uint8_t(h_[i] >> 16);
        out[i * 4 + 2] = uint8_t(h_[i] >> 8);
        out[i * 4 + 3] = uint8_t(h_[i]);
    }
    return out;
}

Sha256::Sha256() : total_(0), buffered_(0) {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(h_, init, sizeof(h_));
}

void Sha256::processBlock(const uint8_t* block) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = loadBe32(block + i * 4);
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256::update(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    total_ += size;
    if (buffered_) {
        size_t take = 64 - buffered_;
        if (take > size) take = size;
        std::memcpy(buffer_ + buffered_, p, take);
        buffered_ += take;
        p += take;
        size -= take;
        if (buffered_ == 64) {
            processBlock(buffer_);
            buffered_ = 0;
        }
    }
    while (size >= 64) {
        processBlock(p);
        p += 64;
        size -= 64;
    }
    if (size) {
        std::memcpy(buffer_, p, size);
        buffered_ = size;
    }
}

std::array<uint8_t, 32> Sha256::finish() {
    uint64_t bitLength = total_ * 8;
    uint8_t padding[72] = {0x80};
    size_t padLen = (buffered_ < 56) ? (56 - buffered_) : (120 - buffered_);
    update(padding, padLen);
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i) lenBytes[i] = uint8_t(bitLength >> (56 - i * 8));
    update(lenBytes, 8);

    std::array<uint8_t, 32> out;
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = uint8_t(h_[i] >> 24);
        out[i * 4 + 1] = uint8_t(h_[i] >> 16);
        out[i * 4 + 2] = uint8_t(h_[i] >> 8);
        out[i * 4 + 3] = uint8_t(h_[i]);
    }
    return out;
}

Sha1Digest sha1(const void* data, size_t size) {
    Sha1 h;
    h.update(data, size);
    return h.finish();
}

Sha256Digest sha256(const void* data, size_t size) {
    Sha256 h;
    h.update(data, size);
    return h.finish();
}

}  // namespace tgnx
