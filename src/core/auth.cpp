#include "core/auth.h"

#include <cstring>

#include "core/aes.h"
#include "core/bigint.h"
#include "core/rsa.h"
#include "core/sha.h"
#include "core/tl.h"
#include "platform/platform.h"

namespace tgnx::mt {

namespace {

constexpr uint32_t kReqPqMulti = 0xbe7e8ef1;
constexpr uint32_t kResPq = 0x05162463;
constexpr uint32_t kPqInnerDataDc = 0xa9f55f95;
constexpr uint32_t kReqDhParams = 0xd712e4be;
constexpr uint32_t kServerDhParamsOk = 0xd0e8075c;
constexpr uint32_t kServerDhParamsFail = 0x79cb045d;
constexpr uint32_t kServerDhInnerData = 0xb5890dba;
constexpr uint32_t kClientDhInnerData = 0x6643b654;
constexpr uint32_t kSetClientDhParams = 0xf5045f1f;
constexpr uint32_t kDhGenOk = 0x3bcbf734;
constexpr uint32_t kDhGenRetry = 0x46dc1fb9;
constexpr uint32_t kDhGenFail = 0xa69dae02;

using u128 = unsigned __int128;

uint64_t mulMod(uint64_t a, uint64_t b, uint64_t mod) { return uint64_t((u128(a) * b) % mod); }

uint64_t powMod64(uint64_t a, uint64_t e, uint64_t mod) {
    uint64_t r = 1;
    a %= mod;
    while (e) {
        if (e & 1) r = mulMod(r, a, mod);
        a = mulMod(a, a, mod);
        e >>= 1;
    }
    return r;
}

uint64_t gcd64(uint64_t a, uint64_t b) {
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

bool isPrime64(uint64_t n) {
    if (n < 2) return false;
    for (uint64_t p : {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull}) {
        if (n % p == 0) return n == p;
    }
    uint64_t d = n - 1;
    int s = 0;
    while ((d & 1) == 0) {
        d >>= 1;
        ++s;
    }
    for (uint64_t a : {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull}) {
        uint64_t x = powMod64(a, d, n);
        if (x == 1 || x == n - 1) continue;
        bool composite = true;
        for (int r = 1; r < s; ++r) {
            x = mulMod(x, x, n);
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        if (composite) return false;
    }
    return true;
}

uint64_t pollardRho(uint64_t n) {
    if ((n & 1) == 0) return 2;
    for (uint64_t c = 1;; ++c) {
        uint64_t x = 2, y = 2, d = 1;
        auto f = [&](uint64_t v) { return (mulMod(v, v, n) + c) % n; };
        while (d == 1) {
            x = f(x);
            y = f(f(y));
            uint64_t diff = x > y ? x - y : y - x;
            d = gcd64(diff, n);
        }
        if (d != n) return d;
    }
}

bool factorize(uint64_t n, uint64_t* p, uint64_t* q) {
    if (n < 4) return false;
    for (;;) {
        if (isPrime64(n)) return false;
        uint64_t d = pollardRho(n);
        uint64_t other = n / d;
        if (d > 1 && other > 1 && d * other == n) {
            *p = d < other ? d : other;
            *q = d < other ? other : d;
            return true;
        }
    }
}

bool receivePlain(Transport& t, int timeoutMs, Bytes* body, std::string* error) {
    Bytes packet;
    if (!t.receive(&packet, timeoutMs, error)) return false;
    UnencryptedPacket msg;
    if (!parseUnencrypted(packet, &msg)) {
        *error = "handshake: not an unencrypted packet";
        return false;
    }
    if (msg.authKeyId != 0) {
        *error = "handshake: unexpected encrypted packet";
        return false;
    }
    *body = std::move(msg.body);
    return true;
}

}  // namespace

bool generateAuthKey(Transport& transport, int dcId, AuthKey* out, std::string* error) {
    auto fail = [&](const std::string& s) {
        if (error) *error = s;
        return false;
    };

    MsgIdGenerator msgIds;
    Bytes nonceBytes(16);
    platform::secureRandom(nonceBytes.data(), nonceBytes.size());

    TlWriter req;
    req.u32(kReqPqMulti);
    req.raw(nonceBytes.data(), 16);
    if (!transport.send(makeUnencrypted(msgIds.next(), req.take()), 10000, error)) return false;

    Bytes body;
    if (!receivePlain(transport, 10000, &body, error)) return false;

    Bytes resNonce, serverNonce, pqBytes;
    std::vector<uint64_t> fingerprints;
    try {
        TlReader r(body);
        if (r.u32() != kResPq) return fail("handshake: expected resPQ");
        resNonce.resize(16);
        r.raw(resNonce.data(), 16);
        serverNonce.resize(16);
        r.raw(serverNonce.data(), 16);
        pqBytes = r.bytes();
        if (r.u32() != 0x1cb5c415) return fail("handshake: expected fingerprint vector");
        uint32_t count = r.u32();
        for (uint32_t i = 0; i < count; ++i) fingerprints.push_back(r.u64());
    } catch (const ProtocolError& e) {
        return fail(std::string("handshake: ") + e.what() + " body=" + hex(body));
    }

    if (resNonce != nonceBytes) return fail("handshake: nonce mismatch");
    if (pqBytes.empty() || pqBytes.size() > 8) return fail("handshake: bad pq");
    uint64_t pq = 0;
    for (uint8_t b : pqBytes) pq = (pq << 8) | b;

    uint64_t p = 0, q = 0;
    if (!factorize(pq, &p, &q)) return fail("handshake: failed to factor pq");
    if (p * q != pq) return fail("handshake: factor mismatch");

    RsaKey rsa = RsaKey::fromPem(productionRsaKeyPem());
    bool fingerprintOk = false;
    for (uint64_t f : fingerprints) {
        if (uint64_t(rsa.fingerprint) == f) fingerprintOk = true;
    }
    if (!fingerprintOk) {
        std::string list;
        for (uint64_t f : fingerprints) list += hex(&f, 8) + " ";
        return fail("handshake: no matching RSA key (server: " + list + ")");
    }

    Bytes newNonce(32);
    platform::secureRandom(newNonce.data(), newNonce.size());

    TlWriter inner;
    inner.u32(kPqInnerDataDc);
    inner.bytes(pqBytes.data(), pqBytes.size());
    uint8_t pBytes[4], qBytes[4];
    storeBe32(pBytes, uint32_t(p));
    storeBe32(qBytes, uint32_t(q));
    inner.bytes(pBytes, 4);
    inner.bytes(qBytes, 4);
    inner.raw(nonceBytes.data(), 16);
    inner.raw(serverNonce.data(), 16);
    inner.raw(newNonce.data(), 32);
    inner.i32(dcId);

    Bytes encryptedData = rsaPadAndEncrypt(inner.data(), rsa);

    TlWriter reqDh;
    reqDh.u32(kReqDhParams);
    reqDh.raw(nonceBytes.data(), 16);
    reqDh.raw(serverNonce.data(), 16);
    reqDh.bytes(pBytes, 4);
    reqDh.bytes(qBytes, 4);
    reqDh.i64(rsa.fingerprint);
    reqDh.bytes(encryptedData.data(), encryptedData.size());
    if (!transport.send(makeUnencrypted(msgIds.next(), reqDh.take()), 10000, error)) return false;

    if (!receivePlain(transport, 15000, &body, error)) return false;

    Bytes encryptedAnswer;
    try {
        TlReader r(body);
        uint32_t ctor = r.u32();
        if (ctor == kServerDhParamsFail) {
            Bytes unused(16);
            r.raw(unused.data(), 16);
            r.raw(unused.data(), 16);
            return fail("handshake: server rejected DH params (-404?)");
        }
        if (ctor != kServerDhParamsOk) return fail("handshake: expected server_DH_params_ok");
        Bytes echoedNonce(16), echoedServerNonce(16);
        r.raw(echoedNonce.data(), 16);
        r.raw(echoedServerNonce.data(), 16);
        if (echoedNonce != nonceBytes) return fail("handshake: nonce mismatch in DH params");
        if (echoedServerNonce != serverNonce) return fail("handshake: server_nonce mismatch");
        encryptedAnswer = r.bytes();
    } catch (const ProtocolError& e) {
        return fail(std::string("handshake: ") + e.what());
    }

    if (encryptedAnswer.size() % 16 != 0) return fail("handshake: answer not padded");
    uint8_t tmpKey[32], tmpIv[32];
    deriveTmpKeyIv(serverNonce.data(), newNonce.data(), tmpKey, tmpIv);
    aesIgeDecrypt(tmpKey, tmpIv, encryptedAnswer.data(), encryptedAnswer.size());

    if (encryptedAnswer.size() < 20) return fail("handshake: answer too short");

    BigInt dhPrimeValue = dhPrime();
    BigInt gA;
    BigInt g(3);
    int32_t serverTime = 0;
    try {
        TlReader r(encryptedAnswer.data() + 20, encryptedAnswer.size() - 20);
        if (r.u32() != kServerDhInnerData) return fail("handshake: expected server_DH_inner_data");
        Bytes echoedNonce(16), echoedServerNonce(16);
        r.raw(echoedNonce.data(), 16);
        r.raw(echoedServerNonce.data(), 16);
        if (echoedNonce != nonceBytes) return fail("handshake: inner nonce mismatch");
        if (echoedServerNonce != serverNonce) return fail("handshake: inner server_nonce mismatch");
        int32_t gValue = r.i32();
        if (gValue < 2 || gValue > 7) return fail("handshake: bad g");
        g = BigInt(uint32_t(gValue));
        Bytes prime = r.bytes();
        Bytes gABytes = r.bytes();
        serverTime = r.i32();
        if (prime.size() != 256 || std::memcmp(prime.data(), kDhPrimeBytes, 256) != 0)
            return fail("handshake: unexpected dh_prime");
        gA = BigInt::fromBytesBE(gABytes.data(), gABytes.size());

        // The leading SHA-1 covers exactly the serialized TL object, not the
        // random padding after it.
        auto answerHash = sha1(encryptedAnswer.data() + 20, r.pos());
        if (std::memcmp(answerHash.data(), encryptedAnswer.data(), 20) != 0)
            return fail("handshake: answer hash mismatch");
    } catch (const ProtocolError& e) {
        return fail(std::string("handshake: ") + e.what());
    }

    BigInt one(1);
    BigInt upper = dhPrimeValue;
    upper.subtract(one);
    if (gA.compare(one) <= 0 || gA.compare(upper) >= 0) return fail("handshake: g_a out of range");
    BigInt safety = BigInt(1);
    // 2^1984: limb 31, bit 0.
    safety.w[31] = 1;
    BigInt lowerBound = safety;
    BigInt upperBound = dhPrimeValue;
    upperBound.subtract(safety);
    if (gA.compare(lowerBound) < 0 || gA.compare(upperBound) > 0)
        return fail("handshake: g_a outside recommended range");

    BigInt b = randomBigInt2048();
    BigInt gB = modPow(g, b, dhPrimeValue);
    BigInt authKeyValue = modPow(gA, b, dhPrimeValue);

    Bytes gBBytes = gB.toBytesBE(256);
    TlWriter clientDh;
    clientDh.u32(kClientDhInnerData);
    clientDh.raw(nonceBytes.data(), 16);
    clientDh.raw(serverNonce.data(), 16);
    clientDh.i64(0);
    clientDh.bytes(gBBytes.data(), gBBytes.size());
    Bytes clientDhData = clientDh.take();

    auto clientHash = sha1(clientDhData.data(), clientDhData.size());
    Bytes clientWithHash;
    clientWithHash.insert(clientWithHash.end(), clientHash.begin(), clientHash.end());
    clientWithHash.insert(clientWithHash.end(), clientDhData.begin(), clientDhData.end());
    size_t padTo16 = (16 - (clientWithHash.size() % 16)) % 16;
    size_t oldSize = clientWithHash.size();
    clientWithHash.resize(oldSize + padTo16);
    platform::secureRandom(clientWithHash.data() + oldSize, padTo16);
    aesIgeEncrypt(tmpKey, tmpIv, clientWithHash.data(), clientWithHash.size());

    TlWriter setDh;
    setDh.u32(kSetClientDhParams);
    setDh.raw(nonceBytes.data(), 16);
    setDh.raw(serverNonce.data(), 16);
    setDh.bytes(clientWithHash.data(), clientWithHash.size());
    if (!transport.send(makeUnencrypted(msgIds.next(), setDh.take()), 10000, error)) return false;

    if (!receivePlain(transport, 15000, &body, error)) return false;

    Bytes authKeyBytes = authKeyValue.toBytesBE(256);
    auto authDigest = sha1(authKeyBytes.data(), authKeyBytes.size());
    uint8_t auxHash[8];
    std::memcpy(auxHash, authDigest.data(), 8);

    try {
        TlReader r(body);
        uint32_t ctor = r.u32();
        Bytes echoedNonce(16), echoedServerNonce(16), hash(16);
        r.raw(echoedNonce.data(), 16);
        r.raw(echoedServerNonce.data(), 16);
        r.raw(hash.data(), 16);
        if (echoedNonce != nonceBytes || echoedServerNonce != serverNonce)
            return fail("handshake: nonce mismatch in dh_gen");
        if (ctor != kDhGenOk) {
            if (ctor == kDhGenRetry) return fail("handshake: dh_gen_retry");
            if (ctor == kDhGenFail) return fail("handshake: dh_gen_fail");
            return fail("handshake: unexpected dh_gen response");
        }
        Bytes hashInput;
        hashInput.insert(hashInput.end(), newNonce.begin(), newNonce.end());
        hashInput.push_back(1);
        hashInput.insert(hashInput.end(), auxHash, auxHash + 8);
        auto expected = sha1(hashInput.data(), hashInput.size());
        if (std::memcmp(expected.data() + 4, hash.data(), 16) != 0)
            return fail("handshake: new_nonce_hash1 mismatch");
    } catch (const ProtocolError& e) {
        return fail(std::string("handshake: ") + e.what());
    }

    out->key = authKeyBytes;
    out->id = loadLe64(authDigest.data() + 12);
    uint8_t saltBytes[8];
    for (int i = 0; i < 8; ++i) saltBytes[i] = newNonce[i] ^ serverNonce[i];
    out->salt = loadLe64(saltBytes);
    out->timeOffsetSec = int64_t(serverTime) - int64_t(platform::unixTimeMs() / 1000);
    return true;
}

bool ping(Transport& transport, const AuthKey& authKey, int timeoutMs, std::string* error) {
    Session session;
    session.authKey = authKey.key;
    session.authKeyId = authKey.id;
    session.salt = authKey.salt;
    session.sessionId = newSessionId();
    session.timeOffsetSec = authKey.timeOffsetSec;
    session.msgIds.setTimeOffset(authKey.timeOffsetSec);

    uint8_t pingBytes[8];
    platform::secureRandom(pingBytes, sizeof(pingBytes));
    uint64_t pingId = loadLe64(pingBytes);

    TlWriter w;
    w.u32(kPing);
    w.i64(int64_t(pingId));

    Bytes pingPacket = encryptMessage(session, w.take());
    if (!transport.send(pingPacket, 10000, error)) return false;

    int64_t deadline = int64_t(platform::monotonicMs()) + timeoutMs;
    while (platform::monotonicMs() < deadline) {
        Bytes packet;
        if (!transport.receive(&packet, 10000, error)) return false;
        UnencryptedPacket msg;
        if (!decryptMessage(session, packet, &msg, error)) {
                continue;
        }
        if (msg.body.size() < 4) continue;
        uint32_t ctor = loadLe32(msg.body.data());
        if (ctor == kPong) {
            TlReader r(msg.body);
            r.u32();
            r.i64();  // msg_id
            uint64_t got = uint64_t(r.i64());
            if (got == pingId) return true;
            *error = "pong with unexpected ping_id";
            return false;
        }
        if (ctor == kBadServerSalt) {
            TlReader r(msg.body);
            r.u32();
            r.i64();
            r.i32();
            r.i32();
            session.salt = uint64_t(r.i64());
            continue;
        }
        if (ctor == kNewSessionCreated) {
            TlReader r(msg.body);
            r.u32();
            r.i64();
            r.i64();
            session.salt = uint64_t(r.i64());
            continue;
        }
        if (ctor == kMsgContainer) {
            try {
                TlReader r(msg.body);
                r.u32();
                uint32_t count = r.u32();
                for (uint32_t i = 0; i < count; ++i) {
                    r.i64();
                    r.i32();
                    uint32_t innerLen = r.u32();
                    Bytes inner(innerLen);
                    r.raw(inner.data(), innerLen);
                    if (inner.size() >= 4 && loadLe32(inner.data()) == kPong) {
                        TlReader pr(inner);
                        pr.u32();
                        pr.i64();
                        uint64_t got = uint64_t(pr.i64());
                        if (got == pingId) return true;
                    }
                }
            } catch (const ProtocolError& e) {
                *error = std::string("ping: bad container: ") + e.what();
                return false;
            }
            continue;
        }
        if (ctor == kRpcResult) {
            TlReader r(msg.body);
            r.u32();
            r.i64();  // req_msg_id
            if (r.u32() == kPong) {
                r.i64();
                uint64_t got = uint64_t(r.i64());
                if (got == pingId) return true;
            }
            continue;
        }
        // Updates/acks are irrelevant for this check.
    }
    *error = "ping: timeout";
    return false;
}

}  // namespace tgnx::mt
