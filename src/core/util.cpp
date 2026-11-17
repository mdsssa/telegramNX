#include "core/util.h"

#include <cstdio>
#include <cstring>

namespace tgnx {

std::string hex(const void* data, size_t size) {
    static const char* digits = "0123456789abcdef";
    const auto* p = static_cast<const uint8_t*>(data);
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(digits[p[i] >> 4]);
        out.push_back(digits[p[i] & 15]);
    }
    return out;
}

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Bytes fromHex(std::string_view s) {
    Bytes out;
    out.reserve(s.size() / 2);
    int high = -1;
    for (char c : s) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        int d = hexDigit(c);
        if (d < 0) throw ProtocolError("fromHex: bad character");
        if (high < 0) {
            high = d;
        } else {
            out.push_back(uint8_t((high << 4) | d));
            high = -1;
        }
    }
    if (high >= 0) throw ProtocolError("fromHex: odd number of digits");
    return out;
}

static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char* kB64Url = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string base64EncodeImpl(const void* data, size_t size, const char* alphabet, bool pad) {
    const auto* p = static_cast<const uint8_t*>(data);
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        uint32_t v = uint32_t(p[i]) << 16;
        int n = 1;
        if (i + 1 < size) { v |= uint32_t(p[i + 1]) << 8; n = 2; }
        if (i + 2 < size) { v |= uint32_t(p[i + 2]); n = 3; }
        out.push_back(alphabet[(v >> 18) & 63]);
        out.push_back(alphabet[(v >> 12) & 63]);
        out.push_back(n >= 2 ? alphabet[(v >> 6) & 63] : (pad ? '=' : '\0'));
        out.push_back(n >= 3 ? alphabet[v & 63] : (pad ? '=' : '\0'));
        if (!pad) {
            if (n < 2) out.pop_back();
            if (n < 3) out.pop_back();
        }
    }
    return out;
}

std::string base64Encode(const void* data, size_t size) { return base64EncodeImpl(data, size, kB64, true); }
std::string base64UrlEncode(const Bytes& b) { return base64EncodeImpl(b.data(), b.size(), kB64Url, false); }

static int b64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static Bytes base64DecodeImpl(std::string_view s) {
    Bytes out;
    out.reserve(s.size() * 3 / 4 + 3);
    uint32_t accum = 0;
    int bits = 0;
    for (char c : s) {
        if (c == '=') break;
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int v = b64Value(c);
        if (v < 0) throw ProtocolError("base64Decode: bad character");
        accum = (accum << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((accum >> bits) & 0xFF));
        }
    }
    return out;
}

Bytes base64Decode(std::string_view s) { return base64DecodeImpl(s); }
Bytes base64UrlDecode(std::string_view s) { return base64DecodeImpl(s); }

bool equalBytes(const void* a, const void* b, size_t n) {
    const auto* x = static_cast<const uint8_t*>(a);
    const auto* y = static_cast<const uint8_t*>(b);
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= uint8_t(x[i] ^ y[i]);
    return diff == 0;
}

void wipe(void* data, size_t size) {
    volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
    while (size--) *p++ = 0;
}

std::string toLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return s;
}

std::string trim(std::string s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

}  // namespace tgnx
