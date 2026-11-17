// AES-256 block cipher plus the two MTProto modes we need:
// AES-IGE (used for DH answers, auth payloads and message bodies) and AES-CTR
// (used by the obfuscated transport).
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/util.h"

namespace tgnx {

class Aes256 {
public:
    Aes256() = default;
    explicit Aes256(const uint8_t key[32]) { init(key); }
    void init(const uint8_t key[32]);
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) const;
    void decryptBlock(const uint8_t in[16], uint8_t out[16]) const;

private:
    uint32_t roundKeys_[60];  // 15 round keys * 4 words
};

// AES-IGE. `data` size must be a multiple of 16. IV is 32 bytes
// (previous ciphertext block || previous plaintext block).
void aesIgeEncrypt(const uint8_t key[32], const uint8_t iv[32], uint8_t* data, size_t size);
void aesIgeDecrypt(const uint8_t key[32], const uint8_t iv[32], uint8_t* data, size_t size);

// AES-CTR, big-endian 128-bit counter starting at `iv`.
class AesCtr {
public:
    AesCtr() = default;
    AesCtr(const uint8_t key[32], const uint8_t iv[16]) { init(key, iv); }
    void init(const uint8_t key[32], const uint8_t iv[16]);
    void process(uint8_t* data, size_t size);

private:
    Aes256 aes_;
    uint8_t counter_[16] = {};
    uint8_t keystream_[16] = {};
    size_t used_ = 16;
};

}  // namespace tgnx
