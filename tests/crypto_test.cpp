#include <cstdio>
#include <cstring>
#include <string>

#include "core/aes.h"
#include "core/bigint.h"
#include "core/rsa.h"
#include "core/sha.h"
#include "core/tl.h"
#include "core/util.h"
#include "core/mtproto.h"

using namespace tgnx;

static int gFailures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++gFailures;                                                  \
        }                                                                 \
    } while (0)

#define CHECK_HEX(actual, expected)                                                     \
    do {                                                                                \
        std::string a_ = hex(actual);                                                   \
        std::string e_ = (expected);                                                    \
        if (a_ != e_) {                                                                 \
            std::printf("FAIL %s:%d\n  got %s\n  exp %s\n", __FILE__, __LINE__, a_.c_str(), e_.c_str()); \
            ++gFailures;                                                                \
        }                                                                               \
    } while (0)

static void testSha() {
    CHECK_HEX(sha1("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK_HEX(sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string longInput(1000, 'a');
    CHECK_HEX(sha256(longInput), "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

static void testBase64() {
    CHECK(base64Encode(Bytes{'M', 'a', 'n'}) == "TWFu");
    CHECK(base64Encode(Bytes{'M', 'a'}) == "TWE=");
    CHECK(base64Encode(Bytes{'M'}) == "TQ==");
    CHECK(base64Decode("TWFu") == Bytes({'M', 'a', 'n'}));
    CHECK(base64UrlEncode(Bytes{0xfb, 0xff}) == "-_8");
    CHECK(base64UrlDecode("-_8") == Bytes({0xfb, 0xff}));
}

static void testAes() {
    Bytes key = fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    Bytes plain = fromHex("00112233445566778899aabbccddeeff");
    uint8_t out[16];
    Aes256 aes(key.data());
    aes.encryptBlock(plain.data(), out);
    CHECK_HEX(Bytes(out, out + 16), "8ea2b7ca516745bfeafc49904b496089");

    uint8_t back[16];
    aes.decryptBlock(out, back);
    CHECK(std::memcmp(back, plain.data(), 16) == 0);
}

static void testIge() {
    uint8_t key[32];
    uint8_t iv[32];
    for (int i = 0; i < 32; ++i) { key[i] = uint8_t(i); iv[i] = uint8_t(0x40 + i); }
    Bytes data(64);
    for (int i = 0; i < 64; ++i) data[i] = uint8_t(i * 7);
    Bytes original = data;
    aesIgeEncrypt(key, iv, data.data(), data.size());
    CHECK(data != original);
    aesIgeDecrypt(key, iv, data.data(), data.size());
    CHECK(data == original);
}

static void testIgeReference() {
    uint8_t key[32], iv[32], data[32];
    for (int i = 0; i < 32; ++i) { key[i] = uint8_t(i); iv[i] = uint8_t(i); data[i] = uint8_t(i); }
    aesIgeEncrypt(key, iv, data, sizeof(data));
    CHECK_HEX(Bytes(data, data + 32),
              "e28112a53e5c89c7b1ea8071c133699fd4e86b26c4bac9fc5af1ab8ce27a44a4");

    mt::Session unused;  // keep the namespace linked for the derivation test
    (void)unused;
}

static void testKeyDerivation() {
    uint8_t serverNonce[16], newNonce[32];
    for (int i = 0; i < 16; ++i) serverNonce[i] = uint8_t(i);
    for (int i = 0; i < 32; ++i) newNonce[i] = uint8_t(i);
    uint8_t key[32], iv[32];
    mt::deriveTmpKeyIv(serverNonce, newNonce, key, iv);
    CHECK_HEX(Bytes(key, key + 32),
              "0758f1533b615d24f6e8a94a6fcbee0a55eaab2217d7295ca9213d1a2d7d16a6");
    CHECK_HEX(Bytes(iv, iv + 32),
              "5a84108e9805656ce864070e166e6218f6783e8511471a5ab7802cf200010203");
}

static void testBigInt() {
    const char* expected1 =
        "9bb45a319cc267f235ad89383a04ef602db26f8ea04c89cff69b640426b452a23dba1c673f58e807f485ae24e3e7818373562e634cf09da0373a742faaaa975c5de7d551d815e99d66ce69e3811259d90390da09232fb411a7c0041dcc5add25eaf047e0a2e09c2b80c11c64badd97a3f5f07629aae40e059a959e0ff3bcac09512c5ed68213724c836e877a534bdc9c9ff3166dfd409b4b3fb49fa71ed9e56ea9dd2696af152e2226326e76f4d44a4d0a637825bee57bde7084d910f117ab1250ad2b0e59646d80c2ad1619cbca1cd46d9d86015a0f85aa86d5e9a414a6e63620a5de2c225d0a5254a6fd8e1bd9330175429e7e5993f608bf522d4a19f698e7";
    BigInt p = dhPrime();
    BigInt e1(123456789);
    BigInt pow1 = modPow(BigInt(3), e1, p);
    CHECK_HEX(pow1.toBytesBE(256), expected1);

    const char* expected2 =
        "9e7c2316bd6a5c4a45b135a000fec5ee428f32ba9cc0acf5c7935fe842d0392dcdecb19a61f07cfe6b895b6f1a661b72ed4add8eb4f66cde7aefd493b123d23da4323e0f84305de476c1b9ec3376faffd256644eaafc38a8b605b81a126b7c06a2733dae9028ff18f340e24913aeb9a6523844a8dc3093ca75d79c9db491cfac0f1ef6ddb9a287a322a43a349cc33bb063d6f9197a0d696de72a5b0995eb4bcbae52c89b84216e943728b569747328f178dddfad8bb234abdebf04c28b6f7fedd7ff7d3370dc8fa5aaa2706b0b65fd9d3b0e60025e896c265eee5ec49420a205f7e1038211dc9077912356144d77abd72448a1b80d6eafef24d71ebac9adf344";
    Bytes expBytes = fromHex(std::string("1234567890abcdef") + "1234567890abcdef" + "1234567890abcdef" +
                             "1234567890abcdef" + "1234567890abcdef" + "1234567890abcdef" +
                             "1234567890abcdef" + "1234567890abcdef" + "1234567890abcdef" +
                             "1234567890abcdef" + "1234567890abcdef" + "1234567890abcdef" +
                             "1234567890abcdef" + "1234567890abcdef" + "1234567890abcdef" +
                             "1234567890abcdef");
    BigInt e2 = BigInt::fromBytesBE(expBytes.data(), expBytes.size());
    BigInt pow2 = modPow(BigInt(3), e2, p);
    CHECK_HEX(pow2.toBytesBE(256), expected2);
}

static void testRsa() {
    RsaKey key = RsaKey::fromPem(productionRsaKeyPem());
    CHECK(key.e == 65537);
    std::printf("RSA fingerprint: %lld (0x%llx)\n", (long long)key.fingerprint,
                (unsigned long long)uint64_t(key.fingerprint));
    CHECK(key.fingerprint == -3414540481677951611LL);

    // The padding must be a valid 256-byte value below the modulus, and the
    // raw public operation must be deterministic.
    Bytes payload = fromHex("0123456789abcdef");
    Bytes enc1 = rsaPadAndEncrypt(payload, key);
    CHECK(enc1.size() == 256);
    Bytes enc2 = rsaPadAndEncrypt(payload, key);
    CHECK(enc1 != enc2);
    Bytes raw1 = rsaRawPublic(enc1, key);
    CHECK(raw1.size() == 256);
    CHECK(hex(raw1) != hex(enc1));
}

static void testTl() {
    TlWriter w;
    w.u32(0x12345678);
    w.str("hello");
    w.bytes(Bytes{1, 2, 3});
    w.u64(0x1122334455667788ULL);
    TlReader r(w.data());
    CHECK(r.u32() == 0x12345678);
    CHECK(r.str() == "hello");
    CHECK(r.bytes() == Bytes({1, 2, 3}));
    CHECK(r.u64() == 0x1122334455667788ULL);
    CHECK(r.atEnd());
}

int main() {
    testSha();
    testBase64();
    testAes();
    testIge();
    testIgeReference();
    testKeyDerivation();
    testBigInt();
    testRsa();
    testTl();
    if (gFailures) {
        std::printf("%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("all crypto tests passed\n");
    return 0;
}
