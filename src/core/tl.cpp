#include "core/tl.h"

#include <cstring>

namespace tgnx {

void TlWriter::u32(uint32_t v) {
    uint8_t b[4];
    storeLe32(b, v);
    buf_.insert(buf_.end(), b, b + 4);
}

void TlWriter::u64(uint64_t v) {
    uint8_t b[8];
    storeLe64(b, v);
    buf_.insert(buf_.end(), b, b + 8);
}

void TlWriter::boolean(bool v) { u32(v ? 0x997275b5u : 0xbc799737u); }

void TlWriter::writeRawUnpadded(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    buf_.insert(buf_.end(), p, p + size);
}

void TlWriter::raw(const void* data, size_t size) { writeRawUnpadded(data, size); }

void TlWriter::zeros(size_t n) { buf_.insert(buf_.end(), n, 0); }

void TlWriter::bytes(const void* data, size_t size) {
    // Telegram's TL strings: a 1-byte length for <=253 bytes, otherwise 254
    // followed by a 3-byte little-endian length. The whole value is aligned
    // to a 4-byte boundary.
    if (size <= 253) {
        uint8_t len = uint8_t(size);
        writeRawUnpadded(&len, 1);
        writeRawUnpadded(data, size);
        size_t pad = (4 - ((size + 1) % 4)) % 4;
        if (pad) zeros(pad);
    } else {
        if (size > 0xFFFFFFu) throw ProtocolError("TL string too long");
        uint8_t header[4] = {254, uint8_t(size), uint8_t(size >> 8), uint8_t(size >> 16)};
        writeRawUnpadded(header, 4);
        writeRawUnpadded(data, size);
        size_t pad = (4 - (size % 4)) % 4;
        if (pad) zeros(pad);
    }
}

void TlWriter::patchU32(size_t offset, uint32_t v) {
    if (offset + 4 > buf_.size()) throw ProtocolError("patch out of range");
    storeLe32(buf_.data() + offset, v);
}

uint32_t TlReader::u32() {
    if (remaining() < 4) throw ProtocolError("TL read past end");
    uint32_t v = loadLe32(data_ + pos_);
    pos_ += 4;
    return v;
}

uint64_t TlReader::u64() {
    if (remaining() < 8) throw ProtocolError("TL read past end");
    uint64_t v = loadLe64(data_ + pos_);
    pos_ += 8;
    return v;
}

bool TlReader::boolean() {
    uint32_t v = u32();
    if (v == 0x997275b5u) return true;
    if (v == 0xbc799737u) return false;
    throw ProtocolError("TL boolean expected");
}

Bytes TlReader::bytes() {
    if (remaining() < 1) throw ProtocolError("TL bytes past end");
    uint32_t first = data_[pos_++];
    uint32_t len;
    size_t pad;
    if (first == 254) {
        if (remaining() < 3) throw ProtocolError("TL bytes length past end");
        len = uint32_t(data_[pos_]) | (uint32_t(data_[pos_ + 1]) << 8) | (uint32_t(data_[pos_ + 2]) << 16);
        pos_ += 3;
        pad = (4 - (len % 4)) % 4;
    } else {
        len = first;
        pad = (4 - ((len + 1) % 4)) % 4;
    }
    if (remaining() < len) throw ProtocolError("TL bytes past end");
    Bytes out(data_ + pos_, data_ + pos_ + len);
    pos_ += len;
    if (remaining() < pad) throw ProtocolError("TL bytes padding past end");
    pos_ += pad;
    return out;
}

std::string TlReader::str() {
    Bytes b = bytes();
    return std::string(b.begin(), b.end());
}

void TlReader::skip(size_t n) {
    if (remaining() < n) throw ProtocolError("TL skip past end");
    pos_ += n;
}

void TlReader::raw(void* out, size_t n) {
    if (remaining() < n) throw ProtocolError("TL raw past end");
    std::memcpy(out, data_ + pos_, n);
    pos_ += n;
}

void tlSkipBytes(TlReader& r) {
    uint32_t len = r.u32();
    size_t pad = (4 - (len % 4)) % 4;
    r.skip(len + pad);
}

}  // namespace tgnx
