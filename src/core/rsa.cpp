#include "core/rsa.h"

#include <cstring>

#include "core/aes.h"
#include "core/sha.h"
#include "core/tl.h"
#include "platform/platform.h"

namespace tgnx {

namespace {

// Minimal DER reader for PKCS#1 RSAPublicKey:
//   SEQUENCE { INTEGER n, INTEGER e }
struct DerReader {
    const uint8_t* p;
    const uint8_t* end;

    bool read(uint8_t expectedTag, const uint8_t** value, size_t* len) {
        if (p >= end) return false;
        uint8_t tag = *p++;
        if (tag != expectedTag) return false;
        if (p >= end) return false;
        size_t length = *p++;
        if (length & 0x80) {
            size_t count = length & 0x7F;
            if (count == 0 || count > 4 || size_t(end - p) < count) return false;
            length = 0;
            for (size_t i = 0; i < count; ++i) length = (length << 8) | *p++;
        }
        if (size_t(end - p) < length) return false;
        *value = p;
        *len = length;
        p += length;
        return true;
    }
};

Bytes bigIntMinimalBytes(const BigInt& v) {
    Bytes all = v.toBytesBE(256);
    size_t skip = 0;
    while (skip < all.size() && all[skip] == 0) ++skip;
    return Bytes(all.begin() + skip, all.end());
}

int64_t computeFingerprint(const Bytes& n, const Bytes& e) {
    TlWriter w;
    w.bytes(n.data(), n.size());
    w.bytes(e.data(), e.size());
    auto digest = sha1(w.data().data(), w.size());
    return int64_t(loadLe64(digest.data() + 12));
}

}  // namespace

RsaKey RsaKey::fromPem(const std::string& pem) {
    const char* beginMarker = "-----BEGIN RSA PUBLIC KEY-----";
    const char* endMarker = "-----END RSA PUBLIC KEY-----";
    size_t begin = pem.find(beginMarker);
    size_t end = pem.find(endMarker);
    if (begin == std::string::npos || end == std::string::npos || end <= begin)
        throw ProtocolError("RSA: bad PEM");
    std::string body = pem.substr(begin + std::strlen(beginMarker), end - begin - std::strlen(beginMarker));
    Bytes der = base64Decode(body);
    if (der.size() < 8) throw ProtocolError("RSA: DER too short");

    DerReader seq{der.data(), der.data() + der.size()};
    const uint8_t* seqValue;
    size_t seqLen;
    if (!seq.read(0x30, &seqValue, &seqLen)) throw ProtocolError("RSA: missing SEQUENCE");

    DerReader inner{seqValue, seqValue + seqLen};
    const uint8_t* nValue;
    size_t nLen;
    const uint8_t* eValue;
    size_t eLen;
    if (!inner.read(0x02, &nValue, &nLen)) throw ProtocolError("RSA: missing modulus");
    if (!inner.read(0x02, &eValue, &eLen)) throw ProtocolError("RSA: missing exponent");
    if (nLen == 0 || nLen > 257 || eLen == 0 || eLen > 5) throw ProtocolError("RSA: bad key sizes");

    RsaKey key;
    key.n = BigInt::fromBytesBE(nValue, nLen);
    uint32_t e = 0;
    for (size_t i = 0; i < eLen; ++i) e = (e << 8) | eValue[i];
    key.e = e;
    Bytes nBytes = bigIntMinimalBytes(key.n);
    Bytes eBytes;
    for (int shift = 24; shift >= 0; shift -= 8) {
        uint8_t b = uint8_t(e >> shift);
        if (!eBytes.empty() || b != 0) eBytes.push_back(b);
    }
    key.fingerprint = computeFingerprint(nBytes, eBytes);
    return key;
}

Bytes rsaRawPublic(const Bytes& input, const RsaKey& key) {
    if (input.size() != 256) throw ProtocolError("RSA: input must be 256 bytes");
    BigInt x = BigInt::fromBytesBE(input.data(), input.size());
    BigInt y = modPow(x, BigInt(key.e), key.n);
    return y.toBytesBE(256);
}

Bytes rsaPadAndEncrypt(const Bytes& data, const RsaKey& key) {
    if (data.size() > 144) throw ProtocolError("RSA_PAD: payload too long");

    for (int attempt = 0; attempt < 16; ++attempt) {
        Bytes dataWithPadding(192, 0);
        std::memcpy(dataWithPadding.data(), data.data(), data.size());
        platform::secureRandom(dataWithPadding.data() + data.size(), dataWithPadding.size() - data.size());

        Bytes reversed(dataWithPadding.rbegin(), dataWithPadding.rend());
        uint8_t tempKey[32];
        platform::secureRandom(tempKey, sizeof(tempKey));

        Bytes hashInput;
        hashInput.reserve(32 + dataWithPadding.size());
        hashInput.insert(hashInput.end(), tempKey, tempKey + 32);
        hashInput.insert(hashInput.end(), dataWithPadding.begin(), dataWithPadding.end());
        auto hash = sha256(hashInput.data(), hashInput.size());

        Bytes dataWithHash;
        dataWithHash.reserve(224);
        dataWithHash.insert(dataWithHash.end(), reversed.begin(), reversed.end());
        dataWithHash.insert(dataWithHash.end(), hash.begin(), hash.end());

        uint8_t zeroIv[32] = {0};
        aesIgeEncrypt(tempKey, zeroIv, dataWithHash.data(), dataWithHash.size());

        auto aesHash = sha256(dataWithHash.data(), dataWithHash.size());
        Bytes keyAesEncrypted;
        keyAesEncrypted.reserve(256);
        for (int i = 0; i < 32; ++i) keyAesEncrypted.push_back(uint8_t(tempKey[i] ^ aesHash[i]));
        keyAesEncrypted.insert(keyAesEncrypted.end(), dataWithHash.begin(), dataWithHash.end());

        BigInt value = BigInt::fromBytesBE(keyAesEncrypted.data(), keyAesEncrypted.size());
        if (value.compare(key.n) >= 0) continue;

        Bytes out = rsaRawPublic(keyAesEncrypted, key);
        wipe(dataWithPadding.data(), dataWithPadding.size());
        wipe(hashInput.data(), hashInput.size());
        wipe(tempKey, sizeof(tempKey));
        return out;
    }
    throw ProtocolError("RSA_PAD: failed to build a value below the modulus");
}

const char* testRsaKeyPem() {
    return "-----BEGIN RSA PUBLIC KEY-----\n"
           "MIIBCgKCAQEAyMEdY1aR+sCR3ZSJrtztKTKqigvO/vBfqACJLZtS7QMgCGXJ6XIR\n"
           "yy7mx66W0/sOFa7/1mAZtEoIokDP3ShoqF4fVNb6XeqgQfaUHd8wJpDWHcR2OFwv\n"
           "plUUI1PLTktZ9uW2WE23b+ixNwJjJGwBDJPQEQFBE+vfmH0JP503wr5INS1poWg/\n"
           "j25sIWeYPHYeOrFp/eXaqhISP6G+q2IeTaWTXpwZj4LzXq5YOpk4bYEQ6mvRq7D1\n"
           "aHWfYmlEGepfaYR8Q0YqvvhYtMte3ITnuSJs171+GDqpdKcSwHnd6FudwGO4pcCO\n"
           "j4WcDuXc2CTHgH8gFTNhp/Y8/SpDOhvn9QIDAQAB\n"
           "-----END RSA PUBLIC KEY-----";
}

const char* productionRsaKeyPem() {
    return "-----BEGIN RSA PUBLIC KEY-----\n"
           "MIIBCgKCAQEA6LszBcC1LGzyr992NzE0ieY+BSaOW622Aa9Bd4ZHLl+TuFQ4lo4g\n"
           "5nKaMBwK/BIb9xUfg0Q29/2mgIR6Zr9krM7HjuIcCzFvDtr+L0GQjae9H0pRB2OO\n"
           "62cECs5HKhT5DZ98K33vmWiLowc621dQuwKWSQKjWf50XYFw42h21P2KXUGyp2y/\n"
           "+aEyZ+uVgLLQbRA1dEjSDZ2iGRy12Mk5gpYc397aYp438fsJoHIgJ2lgMv5h7WY9\n"
           "t6N/byY9Nw9p21Og3AoXSL2q/2IJ1WRUhebgAdGVMlV1fkuOQoEzR7EdpqtQD9Cs\n"
           "5+bfo3Nhmcyvk5ftB0WkJ9z6bNZ7yxrP8wIDAQAB\n"
           "-----END RSA PUBLIC KEY-----";
}

}  // namespace tgnx
