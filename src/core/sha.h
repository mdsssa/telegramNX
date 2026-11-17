// SHA-1 and SHA-256. MTProto uses both in ways that make every byte matter,
// so these are plain, well-tested implementations.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tgnx {

class Sha1 {
public:
    Sha1();
    void update(const void* data, size_t size);
    std::array<uint8_t, 20> finish();

private:
    void processBlock(const uint8_t* block);
    uint32_t h_[5];
    uint64_t total_;
    uint8_t buffer_[64];
    size_t buffered_;
};

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t size);
    std::array<uint8_t, 32> finish();

private:
    void processBlock(const uint8_t* block);
    uint32_t h_[8];
    uint64_t total_;
    uint8_t buffer_[64];
    size_t buffered_;
};

using Sha1Digest = std::array<uint8_t, 20>;
using Sha256Digest = std::array<uint8_t, 32>;

Sha1Digest sha1(const void* data, size_t size);
inline Sha1Digest sha1(std::string_view s) { return sha1(s.data(), s.size()); }
Sha256Digest sha256(const void* data, size_t size);
inline Sha256Digest sha256(std::string_view s) { return sha256(s.data(), s.size()); }

}  // namespace tgnx
