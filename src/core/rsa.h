#pragma once

#include <cstdint>
#include <string>

#include "core/bigint.h"
#include "core/util.h"

namespace tgnx {

struct RsaKey {
    BigInt n;
    uint32_t e = 65537;
    int64_t fingerprint = 0;

    static RsaKey fromPem(const std::string& pem);
};

// MTProto's RSA_PAD (OAEP+ variant) followed by raw RSA. Returns 256 bytes.
Bytes rsaPadAndEncrypt(const Bytes& data, const RsaKey& key);

// Raw RSA public operation, 256-byte big-endian in and out.
Bytes rsaRawPublic(const Bytes& input, const RsaKey& key);

// Current production server keys used by official clients.
const char* productionRsaKeyPem();
const char* testRsaKeyPem();

}  // namespace tgnx
