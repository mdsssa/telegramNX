// SHA-512 and HMAC-SHA-512 / PBKDF2 helpers used by Telegram's SRP check.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/util.h"

namespace tgnx {

class Sha512 {
public:
    Sha512();
    void update(const void* data, size_t size);
    std::array<uint8_t, 64> finish();

private:
    void processBlock(const uint8_t* block);
    uint64_t h_[8];
    uint64_t total_;
    uint8_t buffer_[128];
    size_t buffered_;
};

using Sha512Digest = std::array<uint8_t, 64>;
Sha512Digest sha512(const void* data, size_t size);
inline Sha512Digest sha512(std::string_view s) { return sha512(s.data(), s.size()); }

Bytes hmacSha512(const Bytes& key, const Bytes& data);
Bytes pbkdf2HmacSha512(const Bytes& password, const Bytes& salt, int iterations, size_t outLen);

}  // namespace tgnx
