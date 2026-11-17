// TelegramNX — portable helpers shared by all modules.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tgnx {

using Bytes = std::vector<uint8_t>;

// Errors that come from malformed/unexpected data on the wire or from the API.
struct ProtocolError : std::runtime_error {
    explicit ProtocolError(const std::string& what) : std::runtime_error(what) {}
};

std::string hex(const void* data, size_t size);
template <typename Container>
std::string hex(const Container& c) {
    return hex(c.data(), c.size());
}
Bytes fromHex(std::string_view s);

std::string base64Encode(const void* data, size_t size);
inline std::string base64Encode(const Bytes& b) { return base64Encode(b.data(), b.size()); }
Bytes base64Decode(std::string_view s);
std::string base64UrlEncode(const Bytes& b);
Bytes base64UrlDecode(std::string_view s);

inline uint32_t loadLe32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint64_t loadLe64(const uint8_t* p) {
    return uint64_t(loadLe32(p)) | (uint64_t(loadLe32(p + 4)) << 32);
}
inline void storeLe32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}
inline void storeLe64(uint8_t* p, uint64_t v) {
    storeLe32(p, uint32_t(v));
    storeLe32(p + 4, uint32_t(v >> 32));
}
inline uint32_t loadBe32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline void storeBe32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

// Constant-time-ish comparison for short secrets.
bool equalBytes(const void* a, const void* b, size_t n);

void wipe(void* data, size_t size);

std::string toLower(std::string s);
std::string trim(std::string s);

}  // namespace tgnx
