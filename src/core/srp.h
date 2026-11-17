// Telegram cloud-password SRP (SRP-6a with SHA-256 and PBKDF2-HMAC-SHA512).
#pragma once

#include <cstdint>
#include <string>

#include "core/util.h"

namespace tgnx {

struct SrpParams {
    Bytes salt1;
    Bytes salt2;
    Bytes p;  // 256-byte big-endian prime
    Bytes B;  // 256-byte big-endian server public value
    int32_t g = 3;
    int64_t srpId = 0;
    std::string hint;
};

struct SrpCheck {
    Bytes A;   // 256 bytes
    Bytes M1;  // 32 bytes
};

bool computeSrpCheck(const SrpParams& params, const std::string& password, SrpCheck* out,
                     std::string* error);
// Deterministic variant for tests (uses the supplied 32-byte `a`).
bool computeSrpCheckWithA(const SrpParams& params, const std::string& password, const Bytes& a,
                          SrpCheck* out, std::string* error);

Bytes srpPasswordHash(const SrpParams& params, const std::string& password);

}  // namespace tgnx
