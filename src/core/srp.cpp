#include "core/srp.h"

#include <cstring>

#include "core/bigint.h"
#include "core/sha.h"
#include "core/sha512.h"
#include "platform/platform.h"

namespace tgnx {

namespace {

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

Bytes sha256Bytes(const Bytes& b) {
    auto d = sha256(b.data(), b.size());
    return Bytes(d.begin(), d.end());
}

Bytes toFixed256(const BigInt& v) { return v.toBytesBE(256); }

BigInt modSub(const BigInt& a, const BigInt& b, const BigInt& m) {
    BigInt r = a;
    if (r.compare(b) >= 0) {
        r.subtract(b);
        return r;
    }
    bool overflow = false;
    r.addWithCarry(m, overflow);
    r.subtract(b);
    return r;
}

bool checkA(const BigInt& A, const BigInt& p) {
    BigInt one(1);
    BigInt upper = p;
    upper.subtract(one);
    if (A.compare(one) <= 0 || A.compare(upper) >= 0) return false;
    BigInt safety(1);
    safety.w[31] = 1;  // 2^1984
    BigInt lower = safety;
    BigInt high = p;
    high.subtract(safety);
    return A.compare(lower) >= 0 && A.compare(high) <= 0;
}

}  // namespace

Bytes srpPasswordHash(const SrpParams& params, const std::string& password) {
    Bytes pw(password.begin(), password.end());
    Bytes h1 = sha256Bytes(concat({params.salt1, pw, params.salt1}));
    Bytes h2 = sha256Bytes(concat({params.salt2, h1, params.salt2}));
    Bytes h3 = pbkdf2HmacSha512(h2, params.salt1, 100000, 64);
    return sha256Bytes(concat({params.salt2, h3, params.salt2}));
}

bool computeSrpCheckWithA(const SrpParams& params, const std::string& password, const Bytes& aBytes,
                          SrpCheck* out, std::string* error) {
    auto fail = [&](const std::string& s) {
        if (error) *error = s;
        return false;
    };
    if (params.p.size() != 256 || std::memcmp(params.p.data(), kDhPrimeBytes, 256) != 0)
        return fail("unexpected SRP prime");
    if (params.g != 3 && params.g != 4 && params.g != 5 && params.g != 7)
        return fail("unsupported SRP generator");
    if (params.B.size() != 256) return fail("bad SRP B");
    if (aBytes.size() != 32) return fail("bad SRP a");

    BigInt p = BigInt::fromBytesBE(params.p.data(), params.p.size());
    BigInt g(params.g);
    BigInt B = BigInt::fromBytesBE(params.B.data(), params.B.size());
    if (B.isZero() || B.compare(p) >= 0) return fail("SRP B out of range");

    Bytes xBytes = srpPasswordHash(params, password);
    BigInt x = BigInt::fromBytesBE(xBytes.data(), xBytes.size());

    Bytes pFixed = toFixed256(p);
    Bytes gFixed = toFixed256(g);
    Bytes bFixed = toFixed256(B);
    BigInt k = BigInt::fromBytesBE(sha256Bytes(concat({pFixed, gFixed})).data(), 32);

    BigInt gx = modPow(g, x, p);
    BigInt kgx = modMultiply(k, gx, p);
    BigInt gb = modSub(B, kgx, p);
    if (!checkA(gb, p)) return fail("bad SRP g_b");

    BigInt a = BigInt::fromBytesBE(aBytes.data(), aBytes.size());
    BigInt A = modPow(g, a, p);
    if (!checkA(A, p)) return fail("bad SRP A");

    Bytes aFixed = toFixed256(A);
    BigInt u = BigInt::fromBytesBE(sha256Bytes(concat({aFixed, bFixed})).data(), 32);
    if (u.isZero()) return fail("SRP u is zero");

    BigInt gbU = modPow(gb, u, p);
    BigInt gbUx = modPow(gbU, x, p);
    BigInt gbA = modPow(gb, a, p);
    BigInt S = modMultiply(gbA, gbUx, p);

    Bytes sFixed = toFixed256(S);
    Bytes K = sha256Bytes(sFixed);

    auto shaP = sha256Bytes(pFixed);
    auto shaG = sha256Bytes(gFixed);
    Bytes xorPG(32);
    for (int i = 0; i < 32; ++i) xorPG[i] = shaP[i] ^ shaG[i];
    Bytes m1Input = concat({xorPG, sha256Bytes(params.salt1), sha256Bytes(params.salt2), aFixed,
                            bFixed, K});
    out->A = aFixed;
    out->M1 = sha256Bytes(m1Input);
    return true;
}

bool computeSrpCheck(const SrpParams& params, const std::string& password, SrpCheck* out,
                     std::string* error) {
    uint8_t raw[32];
    for (int attempt = 0; attempt < 8; ++attempt) {
        platform::secureRandom(raw, sizeof(raw));
        raw[0] |= 0x01;  // avoid a == 0
        Bytes aBytes(raw, raw + sizeof(raw));
        if (computeSrpCheckWithA(params, password, aBytes, out, error)) return true;
    }
    if (error) *error = "SRP: failed to generate a valid A";
    return false;
}

}  // namespace tgnx
