// Binary TL serialization (little-endian, 4-byte alignment). Only the
// primitives are implemented here; message layouts live next to the code
// that understands them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/util.h"

namespace tgnx {

class TlWriter {
public:
    void u32(uint32_t v);
    void i32(int32_t v) { u32(uint32_t(v)); }
    void u64(uint64_t v);
    void i64(int64_t v) { u64(uint64_t(v)); }
    void boolean(bool v);
    void raw(const void* data, size_t size);
    void bytes(const void* data, size_t size);
    void bytes(const Bytes& b) { bytes(b.data(), b.size()); }
    void str(std::string_view s) { bytes(s.data(), s.size()); }
    void zeros(size_t n);

    size_t size() const { return buf_.size(); }
    const Bytes& data() const { return buf_; }
    Bytes take() { return std::move(buf_); }
    void clear() { buf_.clear(); }

    void patchU32(size_t offset, uint32_t v);

private:
    void writeRawUnpadded(const void* data, size_t size);
    Bytes buf_;
};

class TlReader {
public:
    TlReader(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0) {}
    explicit TlReader(const Bytes& b) : TlReader(b.data(), b.size()) {}

    uint32_t u32();
    int32_t i32() { return int32_t(u32()); }
    uint64_t u64();
    int64_t i64() { return int64_t(u64()); }
    bool boolean();
    Bytes bytes();
    std::string str();
    void skip(size_t n);
    void raw(void* out, size_t n);

    size_t remaining() const { return size_ - pos_; }
    size_t pos() const { return pos_; }
    bool atEnd() const { return pos_ == size_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
};

// Reads and skips a length-prefixed byte string without allocating.
void tlSkipBytes(TlReader& r);

}  // namespace tgnx
