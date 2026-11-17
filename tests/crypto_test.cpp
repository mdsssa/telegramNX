#include <cstdio>
#include <cstring>
#include <string>

#include "core/aes.h"
#include "core/bigint.h"
#include "core/rsa.h"
#include "core/sha.h"
#include "core/sha512.h"
#include "core/srp.h"
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

static void testSha512() {
    CHECK_HEX(sha512("abc"),
              "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
              "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    CHECK_HEX(sha512(""),
              "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
              "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    Bytes key = {'k', 'e', 'y'};
    Bytes msg = Bytes("The quick brown fox jumps over the lazy dog",
                      "The quick brown fox jumps over the lazy dog" + 43);
    CHECK_HEX(hmacSha512(key, msg),
              "b42af09057bac1e2d41708e48a902e09b5ff7f12ab428a4fe86653c73dd248fb"
              "82f948a549f7b791a5b41915ee4d1ec3935357e4e2317250d0372afa2ebeeb3a");
    Bytes pw = {'p', 'a', 's', 's', 'w', 'o', 'r', 'd'};
    Bytes salt = {'s', 'a', 'l', 't'};
    CHECK_HEX(pbkdf2HmacSha512(pw, salt, 1000, 64),
              "afe6c5530785b6cc6b1c6453384731bd5ee432ee549fd42fb6695779ad8a1c5b"
              "f59de69c48f774efc4007d5298f9033c0241d5ab69305e7b64eceeb8d834cfec");
}

static void testSrp() {
    SrpParams params;
    params.salt1.resize(16);
    params.salt2.resize(16);
    for (int i = 0; i < 16; ++i) {
        params.salt1[i] = uint8_t(1 + i);
        params.salt2[i] = uint8_t(17 + i);
    }
    params.p.assign(kDhPrimeBytes, kDhPrimeBytes + 256);
    params.B = fromHex(
        "24c6e0a2bff37a61e2c28fda0f65d09840b26eecddaf6ea96a949036ed96a83a"
        "b96d0afa10fbf4164e29a61775c032c2461a1452f7513254b945811a709bca65"
        "8646c1f573dbc4435e1b7e01417af6ec29f47439e5f61f87cac5d918d84ded1d"
        "5e6ec7b14383292cae5371bb33f7f47eba8c0e5eb693b42ab444e6e6edae0845"
        "0306b809583c48a35767f1212c34c54d707f8ac7c604a9f49bd506c484916aed"
        "070a502ce9eb05a71581327562b2682b835d3846fa76f77e1a9295582204fe12"
        "9e068c3fff8fd5a5226d026a81555b48b0a06543ea765ea45a166f22be5e3a59"
        "1def94e3bcefffe85da6884cd08eaa9e4c7cec8a16ec97d26faeab5e54730236");
    params.g = 3;
    Bytes a = fromHex("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
    SrpCheck check;
    std::string error;
    CHECK(computeSrpCheckWithA(params, "correct horse", a, &check, &error));
    CHECK_HEX(check.A,
              "7b30783d1cee5412ddea4df0067f21c0c664826da44616df4aa39b1e73604db5"
              "7684fc97ca5ebd3ce5c018c01d0d199eac0a94bb3eb5d4193f651e08e42fcbda"
              "5391ff0234f60d9a5999a4f64493fe3fbc5e3e6b2989eaa319eadeb0d96d7d45"
              "51f36d5a415b3538fa71285f45a41dcc5ad9060d0a65758af271fa6fd74ebec5"
              "082bc0d8ef24ee4a93a12c6b7bb9e7ebf7a627d48ad2a71e9056f14f2bca70c4"
              "7d191052fc62f1d6073667909ef5cfb3859d5e4bc4010a562575b2e192f2147"
              "7571b59ced5777e74fa5640db98ea1026ac7c1421a873291c7117a2217de4da"
              "6747f7abe7a517e3140e6ea7c4709910440b47ac555abc0b900d1a8407c91eef3f");
    CHECK_HEX(check.M1, "3d7493884592ce6e10a42a80a8ff7edce873ca5ecbb3261c18e60d6e7886cc33");
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

    RsaKey testKey = RsaKey::fromPem(testRsaKeyPem());
    CHECK(uint64_t(testKey.fingerprint) == 0xb25898df208d2603ULL);

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
    testSha512();
    testSrp();
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
