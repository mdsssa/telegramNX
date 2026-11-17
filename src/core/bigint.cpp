#include "core/bigint.h"

#include <cstring>

#include "platform/platform.h"

namespace tgnx {

namespace {

using u128 = unsigned __int128;

constexpr int N = BigInt::kLimbs;

// -n[0]^{-1} mod 2^64 for odd n[0].
uint64_t montgomeryN0(uint64_t n0) {
    uint64_t x = 1;
    for (int i = 0; i < 6; ++i) x *= 2 - n0 * x;
    return uint64_t(0 - x);
}

void mulRaw(const uint64_t* a, const uint64_t* b, uint64_t* t) {
    // t has 2*N+2 limbs pre-zeroed.
    for (int i = 0; i < N; ++i) {
        uint64_t carry = 0;
        for (int j = 0; j < N; ++j) {
            u128 uv = u128(a[i]) * b[j] + t[i + j] + carry;
            t[i + j] = uint64_t(uv);
            carry = uint64_t(uv >> 64);
        }
        t[i + N] = carry;
    }
}

void reduceMont(const uint64_t* n, uint64_t n0inv, uint64_t* t) {
    for (int i = 0; i < N; ++i) {
        uint64_t u = t[i] * n0inv;
        u128 carry = 0;
        for (int j = 0; j < N; ++j) {
            u128 uv = u128(u) * n[j] + t[i + j] + uint64_t(carry);
            t[i + j] = uint64_t(uv);
            carry = uv >> 64;
        }
        size_t pos = size_t(i) + N;
        u128 sum = u128(t[pos]) + uint64_t(carry);
        t[pos] = uint64_t(sum);
        uint64_t carry2 = uint64_t(sum >> 64);
        while (carry2) {
            ++pos;
            if (pos >= size_t(2 * N + 1)) break;
            u128 s2 = u128(t[pos]) + carry2;
            t[pos] = uint64_t(s2);
            carry2 = uint64_t(s2 >> 64);
        }
    }
}

struct MontCtx {
    uint64_t n[N];
    uint64_t n0inv;
    uint64_t r2[N];
    uint64_t one[N];

    explicit MontCtx(const BigInt& mod) {
        std::memcpy(n, mod.w, sizeof(n));
        n0inv = montgomeryN0(n[0]);
        std::memset(one, 0, sizeof(one));
        one[0] = 1;

        // R^2 mod n, R = 2^2048, via 4096 modular doublings.
        uint64_t r[N] = {0};
        r[0] = 1;
        for (int i = 0; i < 2 * N * 64; ++i) {
            uint64_t carry = 0;
            bool overflow = false;
            for (int j = 0; j < N; ++j) {
                uint64_t next = r[j] >> 63;
                r[j] = (r[j] << 1) | carry;
                carry = next;
            }
            overflow = carry != 0;
            if (overflow || cmpWords(r, n) >= 0) subWords(r, n);
        }
        std::memcpy(r2, r, sizeof(r2));
    }

    static int cmpWords(const uint64_t* a, const uint64_t* b) {
        for (int i = N - 1; i >= 0; --i) {
            if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
        }
        return 0;
    }

    static void subWords(uint64_t* a, const uint64_t* b) {
        uint64_t borrow = 0;
        for (int i = 0; i < N; ++i) {
            u128 d = u128(a[i]) - b[i] - borrow;
            a[i] = uint64_t(d);
            borrow = (d >> 64) != 0 ? 1 : 0;
        }
    }

    void mul(const BigInt& a, const BigInt& b, BigInt& out) const {
        uint64_t t[2 * N + 2];
        std::memset(t, 0, sizeof(t));
        mulRaw(a.w, b.w, t);
        reduceMont(n, n0inv, t);

        // Result lives in t[N .. 2N], possibly with a high limb at 2N.
        uint64_t hi = t[2 * N];
        uint64_t res[N];
        std::memcpy(res, t + N, sizeof(res));
        if (hi != 0 || cmpWords(res, n) >= 0) {
            uint64_t borrow = 0;
            for (int i = 0; i < N; ++i) {
                u128 d = u128(res[i]) - n[i] - borrow;
                res[i] = uint64_t(d);
                borrow = (d >> 64) != 0 ? 1 : 0;
            }
            if (hi != 0) {
                // The subtraction above only cancels the low half; the high
                // limb is consumed by the borrow propagation conceptually.
                borrow = 0;
            }
        }
        std::memcpy(out.w, res, sizeof(res));
    }

    BigInt toMont(const BigInt& x) const {
        BigInt r2v;
        std::memcpy(r2v.w, r2, sizeof(r2));
        BigInt r;
        mul(x, r2v, r);
        return r;
    }

    BigInt fromMont(const BigInt& x) const {
        BigInt onev;
        onev.w[0] = 1;
        BigInt r;
        mul(x, onev, r);
        return r;
    }
};

}  // namespace

BigInt::BigInt() { std::memset(w, 0, sizeof(w)); }

BigInt::BigInt(uint64_t v) : BigInt() { w[0] = v; }

BigInt BigInt::fromBytesBE(const uint8_t* data, size_t size) {
    BigInt r;
    // Ignore leading zeroes beyond 256 bytes.
    if (size > 256) {
        size_t skip = size - 256;
        bool allZero = true;
        for (size_t i = 0; i < skip; ++i) {
            if (data[i]) { allZero = false; break; }
        }
        if (!allZero) throw ProtocolError("BigInt: value too large");
        data += skip;
        size = 256;
    }
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = data[size - 1 - i];
        r.w[i / 8] |= uint64_t(byte) << (8 * (i % 8));
    }
    return r;
}

BigInt BigInt::fromBytesLE(const uint8_t* data, size_t size) {
    BigInt r;
    for (size_t i = 0; i < size && i < 256; ++i) {
        r.w[i / 8] |= uint64_t(data[i]) << (8 * (i % 8));
    }
    return r;
}

void BigInt::toBytesBE(uint8_t* out, size_t size) const {
    for (size_t i = 0; i < size; ++i) {
        size_t fromEnd = size - 1 - i;  // byte index counted from the LSB
        size_t limb = fromEnd / 8;
        out[i] = limb < size_t(kLimbs) ? uint8_t(w[limb] >> (8 * (fromEnd % 8))) : 0;
    }
}

Bytes BigInt::toBytesBE(size_t size) const {
    Bytes out(size);
    toBytesBE(out.data(), size);
    return out;
}

bool BigInt::isZero() const {
    for (int i = 0; i < kLimbs; ++i) {
        if (w[i]) return false;
    }
    return true;
}

bool BigInt::isOne() const {
    if (w[0] != 1) return false;
    for (int i = 1; i < kLimbs; ++i) {
        if (w[i]) return false;
    }
    return true;
}

int BigInt::compare(const BigInt& other) const {
    for (int i = kLimbs - 1; i >= 0; --i) {
        if (w[i] != other.w[i]) return w[i] < other.w[i] ? -1 : 1;
    }
    return 0;
}

void BigInt::subtract(const BigInt& other) {
    uint64_t borrow = 0;
    for (int i = 0; i < kLimbs; ++i) {
        u128 d = u128(w[i]) - other.w[i] - borrow;
        w[i] = uint64_t(d);
        borrow = (d >> 64) != 0 ? 1 : 0;
    }
}

void BigInt::addWithCarry(const BigInt& other, bool& overflow) {
    uint64_t carry = 0;
    for (int i = 0; i < kLimbs; ++i) {
        u128 s = u128(w[i]) + other.w[i] + carry;
        w[i] = uint64_t(s);
        carry = uint64_t(s >> 64);
    }
    overflow = carry != 0;
}

BigInt modPow(const BigInt& base, const BigInt& exponent, const BigInt& modulus) {
    if (modulus.isZero() || modulus.isEven()) throw ProtocolError("modPow: bad modulus");
    MontCtx ctx(modulus);

    BigInt b = base;
    while (b.compare(modulus) >= 0) b.subtract(modulus);
    BigInt bm = ctx.toMont(b);

    // Initialize with R mod n (= 1 in Montgomery form).
    BigInt result = ctx.toMont(BigInt(uint64_t(1)));

    for (int i = 2047; i >= 0; --i) {
        BigInt sq;
        ctx.mul(result, result, sq);
        result = sq;
        if ((exponent.w[i / 64] >> (i % 64)) & 1) {
            BigInt prod;
            ctx.mul(result, bm, prod);
            result = prod;
        }
    }
    return ctx.fromMont(result);
}

BigInt modMultiply(const BigInt& a, const BigInt& b, const BigInt& modulus) {
    if (modulus.isZero() || modulus.isEven()) throw ProtocolError("modMultiply: bad modulus");
    MontCtx ctx(modulus);
    BigInt x = a;
    BigInt y = b;
    while (x.compare(modulus) >= 0) x.subtract(modulus);
    while (y.compare(modulus) >= 0) y.subtract(modulus);
    BigInt xm = ctx.toMont(x);
    BigInt ym = ctx.toMont(y);
    BigInt r;
    ctx.mul(xm, ym, r);
    return ctx.fromMont(r);
}

BigInt randomBigInt2048() {
    Bytes b(256);
    platform::secureRandom(b.data(), b.size());
    b[0] &= 0x7F;  // < 2^2047
    b[0] |= 0x40;  // enough entropy, > 2^2046
    b[255] |= 1;   // odd
    return BigInt::fromBytesBE(b.data(), b.size());
}

// The safe 2048-bit prime currently used by Telegram (see auth_key docs).
const uint8_t kDhPrimeBytes[256] = {
    0xC7, 0x1C, 0xAE, 0xB9, 0xC6, 0xB1, 0xC9, 0x04, 0x8E, 0x6C, 0x52, 0x2F, 0x70, 0xF1, 0x3F, 0x73,
    0x98, 0x0D, 0x40, 0x23, 0x8E, 0x3E, 0x21, 0xC1, 0x49, 0x34, 0xD0, 0x37, 0x56, 0x3D, 0x93, 0x0F,
    0x48, 0x19, 0x8A, 0x0A, 0xA7, 0xC1, 0x40, 0x58, 0x22, 0x94, 0x93, 0xD2, 0x25, 0x30, 0xF4, 0xDB,
    0xFA, 0x33, 0x6F, 0x6E, 0x0A, 0xC9, 0x25, 0x13, 0x95, 0x43, 0xAE, 0xD4, 0x4C, 0xCE, 0x7C, 0x37,
    0x20, 0xFD, 0x51, 0xF6, 0x94, 0x58, 0x70, 0x5A, 0xC6, 0x8C, 0xD4, 0xFE, 0x6B, 0x6B, 0x13, 0xAB,
    0xDC, 0x97, 0x46, 0x51, 0x29, 0x69, 0x32, 0x84, 0x54, 0xF1, 0x8F, 0xAF, 0x8C, 0x59, 0x5F, 0x64,
    0x24, 0x77, 0xFE, 0x96, 0xBB, 0x2A, 0x94, 0x1D, 0x5B, 0xCD, 0x1D, 0x4A, 0xC8, 0xCC, 0x49, 0x88,
    0x07, 0x08, 0xFA, 0x9B, 0x37, 0x8E, 0x3C, 0x4F, 0x3A, 0x90, 0x60, 0xBE, 0xE6, 0x7C, 0xF9, 0xA4,
    0xA4, 0xA6, 0x95, 0x81, 0x10, 0x51, 0x90, 0x7E, 0x16, 0x27, 0x53, 0xB5, 0x6B, 0x0F, 0x6B, 0x41,
    0x0D, 0xBA, 0x74, 0xD8, 0xA8, 0x4B, 0x2A, 0x14, 0xB3, 0x14, 0x4E, 0x0E, 0xF1, 0x28, 0x47, 0x54,
    0xFD, 0x17, 0xED, 0x95, 0x0D, 0x59, 0x65, 0xB4, 0xB9, 0xDD, 0x46, 0x58, 0x2D, 0xB1, 0x17, 0x8D,
    0x16, 0x9C, 0x6B, 0xC4, 0x65, 0xB0, 0xD6, 0xFF, 0x9C, 0xA3, 0x92, 0x8F, 0xEF, 0x5B, 0x9A, 0xE4,
    0xE4, 0x18, 0xFC, 0x15, 0xE8, 0x3E, 0xBE, 0xA0, 0xF8, 0x7F, 0xA9, 0xFF, 0x5E, 0xED, 0x70, 0x05,
    0x0D, 0xED, 0x28, 0x49, 0xF4, 0x7B, 0xF9, 0x59, 0xD9, 0x56, 0x85, 0x0C, 0xE9, 0x29, 0x85, 0x1F,
    0x0D, 0x81, 0x15, 0xF6, 0x35, 0xB1, 0x05, 0xEE, 0x2E, 0x4E, 0x15, 0xD0, 0x4B, 0x24, 0x54, 0xBF,
    0x6F, 0x4F, 0xAD, 0xF0, 0x34, 0xB1, 0x04, 0x03, 0x11, 0x9C, 0xD8, 0xE3, 0xB9, 0x2F, 0xCC, 0x5B,
};

BigInt dhPrime() { return BigInt::fromBytesBE(kDhPrimeBytes, sizeof(kDhPrimeBytes)); }

}  // namespace tgnx
