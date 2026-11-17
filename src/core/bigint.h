// Fixed-width big integers (up to 2048 bits) with Montgomery modular
// exponentiation. Enough for Telegram's 2048-bit DH, RSA and SRP.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/util.h"

namespace tgnx {

class BigInt {
public:
    static constexpr int kLimbs = 32;  // 2048 bits

    BigInt();
    explicit BigInt(uint64_t v);

    static BigInt fromBytesBE(const uint8_t* data, size_t size);
    static BigInt fromBytesLE(const uint8_t* data, size_t size);
    void toBytesBE(uint8_t* out, size_t size) const;
    Bytes toBytesBE(size_t size) const;

    bool isZero() const;
    bool isOne() const;
    bool isEven() const { return (w[0] & 1) == 0; }
    int compare(const BigInt& other) const;
    bool operator<(const BigInt& o) const { return compare(o) < 0; }
    bool operator==(const BigInt& o) const { return compare(o) == 0; }
    bool operator!=(const BigInt& o) const { return compare(o) != 0; }

    void subtract(const BigInt& other);  // requires *this >= other
    void addWithCarry(const BigInt& other, bool& overflow);

    uint64_t w[kLimbs];
};

// base^exponent mod modulus. Modulus must be odd and non-zero.
BigInt modPow(const BigInt& base, const BigInt& exponent, const BigInt& modulus);
BigInt modMultiply(const BigInt& a, const BigInt& b, const BigInt& modulus);

// Random 2048-bit value with the top bit cleared (so < 2^2047) and the next
// bit set, well above 1.
BigInt randomBigInt2048();

// Helpers for the fixed Telegram DH prime.
extern const uint8_t kDhPrimeBytes[256];
BigInt dhPrime();

}  // namespace tgnx
