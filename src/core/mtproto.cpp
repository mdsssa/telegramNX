#include "core/mtproto.h"

#include <cstring>

#include "core/aes.h"
#include "core/sha.h"
#include "core/tl.h"
#include "platform/platform.h"

namespace tgnx::mt {

namespace {

constexpr size_t kMaxFrame = 16 * 1024 * 1024;

void deriveMsgKeys(const Bytes& authKey, const uint8_t msgKey[16], bool serverToClient, uint8_t key[32],
                   uint8_t iv[32]) {
    int x = serverToClient ? 8 : 0;
    auto shaA = sha256(std::string(reinterpret_cast<const char*>(msgKey), 16) +
                       std::string(reinterpret_cast<const char*>(authKey.data() + x), 36));
    auto shaB = sha256(std::string(reinterpret_cast<const char*>(authKey.data() + 40 + x), 36) +
                       std::string(reinterpret_cast<const char*>(msgKey), 16));
    std::memcpy(key, shaA.data(), 8);
    std::memcpy(key + 8, shaB.data() + 8, 16);
    std::memcpy(key + 24, shaA.data() + 24, 8);
    std::memcpy(iv, shaB.data(), 8);
    std::memcpy(iv + 8, shaA.data() + 8, 16);
    std::memcpy(iv + 24, shaB.data() + 24, 8);
}

}  // namespace

bool IntermediateTransport::connect(const std::string& host, uint16_t port, int timeoutMs,
                                    std::string* error) {
    return sock_.connect(host, port, timeoutMs, error);
}

bool IntermediateTransport::send(const Bytes& payload, int timeoutMs, std::string* error) {
    if (payload.size() > kMaxFrame) {
        if (error) *error = "payload too large";
        return false;
    }
    Bytes frame;
    frame.resize(4 + payload.size());
    storeLe32(frame.data(), uint32_t(payload.size()));
    std::memcpy(frame.data() + 4, payload.data(), payload.size());
    return sock_.sendAll(frame.data(), frame.size(), timeoutMs, error);
}

bool IntermediateTransport::receive(Bytes* payload, int timeoutMs, std::string* error) {
    for (;;) {
        uint8_t header[4];
        if (!sock_.recvAll(header, 4, timeoutMs, error)) return false;
        uint32_t len = loadLe32(header);
        if (len & 0x80000000u) {
            // Quick ack marker: no payload follows.
            continue;
        }
        if (len == 0 || len > kMaxFrame) {
            if (error) *error = "bad frame length";
            return false;
        }
        payload->resize(len);
        if (!sock_.recvAll(payload->data(), len, timeoutMs, error)) return false;
        return true;
    }
}

void IntermediateTransport::close() { sock_.close(); }

namespace {

bool looksRandom(const uint8_t* header) {
    uint32_t prefix = loadLe32(header);
    if (prefix == 0xeeeeeeeeu || prefix == 0xefefefefu || prefix == 0xddddddddu) return false;
    if (std::memcmp(header, "GET ", 4) == 0) return false;
    if (std::memcmp(header, "POST", 4) == 0) return false;
    if (std::memcmp(header, "PVrG", 4) == 0) return false;
    return true;
}

}  // namespace

bool ObfuscatedTransport::connect(const std::string& host, uint16_t port, int timeoutMs,
                                  std::string* error) {
    if (!sock_.connect(host, port, timeoutMs, error)) return false;

    uint8_t header[64];
    do {
        platform::secureRandom(header, sizeof(header));
    } while (!looksRandom(header));

    // 0xEEEEEEEE selects intermediate framing inside obfuscation.
    header[56] = 0xee;
    header[57] = 0xee;
    header[58] = 0xee;
    header[59] = 0xee;

    uint8_t decryptKey[32], decryptIv[16];
    for (int i = 0; i < 32; ++i) decryptKey[i] = header[55 - i];
    for (int i = 0; i < 16; ++i) decryptIv[i] = header[23 - i];

    AesCtr encryptor(header + 8, header + 40);
    uint8_t scrambled[64];
    std::memcpy(scrambled, header, sizeof(scrambled));
    encryptor.process(scrambled, sizeof(scrambled));
    std::memcpy(header + 56, scrambled + 56, 8);

    if (!sock_.sendAll(header, sizeof(header), timeoutMs, error)) return false;
    sendCtr_ = encryptor;
    recvCtr_.init(decryptKey, decryptIv);
    return true;
}

bool ObfuscatedTransport::send(const Bytes& payload, int timeoutMs, std::string* error) {
    if (payload.size() > kMaxFrame) {
        if (error) *error = "payload too large";
        return false;
    }
    Bytes frame(4 + payload.size());
    storeLe32(frame.data(), uint32_t(payload.size()));
    std::memcpy(frame.data() + 4, payload.data(), payload.size());
    sendCtr_.process(frame.data(), frame.size());
    return sock_.sendAll(frame.data(), frame.size(), timeoutMs, error);
}

bool ObfuscatedTransport::receive(Bytes* payload, int timeoutMs, std::string* error) {
    for (;;) {
        uint8_t header[4];
        if (!sock_.recvAll(header, 4, timeoutMs, error)) return false;
        recvCtr_.process(header, 4);
        uint32_t len = loadLe32(header);
        if (len & 0x80000000u) continue;
        if (len == 0 || len > kMaxFrame) {
            if (error) *error = "bad obfuscated frame length";
            return false;
        }
        payload->resize(len);
        if (!sock_.recvAll(payload->data(), len, timeoutMs, error)) return false;
        recvCtr_.process(payload->data(), len);
        return true;
    }
}

void ObfuscatedTransport::close() { sock_.close(); }

Bytes makeUnencrypted(uint64_t msgId, const Bytes& body) {
    Bytes packet;
    packet.resize(8 + 8 + 4 + body.size());
    storeLe64(packet.data(), 0);
    storeLe64(packet.data() + 8, msgId);
    storeLe32(packet.data() + 16, uint32_t(body.size()));
    std::memcpy(packet.data() + 20, body.data(), body.size());
    return packet;
}

bool parseUnencrypted(const Bytes& packet, UnencryptedPacket* out) {
    if (packet.size() < 20) return false;
    out->authKeyId = loadLe64(packet.data());
    out->msgId = loadLe64(packet.data() + 8);
    uint32_t len = loadLe32(packet.data() + 16);
    if (uint64_t(len) + 20 != packet.size()) return false;
    out->body.assign(packet.begin() + 20, packet.end());
    return true;
}

uint64_t newSessionId() {
    uint8_t b[8];
    platform::secureRandom(b, sizeof(b));
    return loadLe64(b);
}

uint64_t MsgIdGenerator::next() {
    int64_t nowSec = platform::unixTimeMs() / 1000 + timeOffsetSec_;
    uint64_t id = (uint64_t(nowSec) << 32) | ((counter_++ & 0x3FFFFFFF) << 2);
    if (id <= last_) id = last_ + 4;
    last_ = id;
    return id;
}

void deriveTmpKeyIv(const uint8_t serverNonce[16], const uint8_t newNonce[32], uint8_t key[32],
                    uint8_t iv[32]) {
    Bytes nn(newNonce, newNonce + 32);
    Bytes sn(serverNonce, serverNonce + 16);

    Bytes h1Input = nn;
    h1Input.insert(h1Input.end(), sn.begin(), sn.end());
    auto h1 = sha1(h1Input.data(), h1Input.size());

    Bytes h2Input = sn;
    h2Input.insert(h2Input.end(), nn.begin(), nn.end());
    auto h2 = sha1(h2Input.data(), h2Input.size());

    Bytes h3Input = nn;
    h3Input.insert(h3Input.end(), nn.begin(), nn.end());
    auto h3 = sha1(h3Input.data(), h3Input.size());

    std::memcpy(key, h1.data(), 20);
    std::memcpy(key + 20, h2.data(), 12);
    std::memcpy(iv, h2.data() + 12, 8);
    std::memcpy(iv + 8, h3.data(), 20);
    std::memcpy(iv + 28, newNonce, 4);
}

Bytes encryptMessage(Session& session, const Bytes& body, bool contentRelated, uint64_t* msgIdOut) {
    uint64_t msgId = session.msgIds.next();
    uint32_t seqNo = 0;
    if (contentRelated) {
        seqNo = session.seqNo;
        session.seqNo += 2;
    }
    if (msgIdOut) *msgIdOut = msgId;

    // 12..27 bytes of padding is enough to reach a 16-byte boundary and keeps
    // us inside MTProto's 12..1024 range.
    size_t padLen = 12 + ((16 - ((32 + body.size() + 12) % 16)) % 16);
    size_t total = 32 + body.size() + padLen;

    Bytes plain;
    plain.resize(total);
    storeLe64(plain.data(), session.salt);
    storeLe64(plain.data() + 8, session.sessionId);
    storeLe64(plain.data() + 16, msgId);
    storeLe32(plain.data() + 24, seqNo);
    storeLe32(plain.data() + 28, uint32_t(body.size()));
    std::memcpy(plain.data() + 32, body.data(), body.size());
    platform::secureRandom(plain.data() + 32 + body.size(), padLen);

    Bytes msgKeyInput;
    msgKeyInput.reserve(32 + plain.size());
    msgKeyInput.insert(msgKeyInput.end(), session.authKey.begin() + 88, session.authKey.begin() + 120);
    msgKeyInput.insert(msgKeyInput.end(), plain.begin(), plain.end());
    auto digest = sha256(msgKeyInput.data(), msgKeyInput.size());
    uint8_t msgKey[16];
    std::memcpy(msgKey, digest.data() + 8, 16);

    uint8_t key[32], iv[32];
    deriveMsgKeys(session.authKey, msgKey, false, key, iv);
    aesIgeEncrypt(key, iv, plain.data(), plain.size());

    Bytes packet;
    packet.resize(8 + 16 + plain.size());
    storeLe64(packet.data(), session.authKeyId);
    std::memcpy(packet.data() + 8, msgKey, 16);
    std::memcpy(packet.data() + 24, plain.data(), plain.size());
    wipe(plain.data(), plain.size());
    return packet;
}

bool decryptMessage(const Session& session, const Bytes& packet, UnencryptedPacket* out,
                    std::string* error) {
    if (packet.size() < 24 || (packet.size() - 24) % 16 != 0) {
        if (error) *error = "bad encrypted packet size";
        return false;
    }
    if (loadLe64(packet.data()) != session.authKeyId) {
        if (error) *error = "unknown auth_key_id";
        return false;
    }
    Bytes cipher(packet.begin() + 24, packet.end());
    uint8_t msgKey[16];
    std::memcpy(msgKey, packet.data() + 8, 16);

    uint8_t key[32], iv[32];
    deriveMsgKeys(session.authKey, msgKey, true, key, iv);
    aesIgeDecrypt(key, iv, cipher.data(), cipher.size());

    Bytes checkInput;
    checkInput.reserve(32 + cipher.size());
    checkInput.insert(checkInput.end(), session.authKey.begin() + 96, session.authKey.begin() + 128);
    checkInput.insert(checkInput.end(), cipher.begin(), cipher.end());
    auto digest = sha256(checkInput.data(), checkInput.size());
    if (std::memcmp(digest.data() + 8, msgKey, 16) != 0) {
        if (error) *error = "msg_key mismatch";
        return false;
    }
    if (cipher.size() < 32) {
        if (error) *error = "short plaintext";
        return false;
    }
    uint32_t bodyLen = loadLe32(cipher.data() + 28);
    if (bodyLen > cipher.size() - 32) {
        if (error) *error = "body length out of range";
        return false;
    }
    size_t padding = cipher.size() - 32 - bodyLen;
    if (padding < 12 || padding > 1024) {
        if (error) *error = "bad padding";
        return false;
    }
    out->authKeyId = session.authKeyId;
    out->msgId = loadLe64(cipher.data() + 16);
    out->body.assign(cipher.begin() + 32, cipher.begin() + 32 + bodyLen);
    uint64_t salt = loadLe64(cipher.data());
    (void)salt;
    wipe(cipher.data(), cipher.size());
    return true;
}

bool unpackContainer(const Bytes& containerBody, std::vector<Bytes>* out) {
    try {
        TlReader r(containerBody);
        if (r.u32() != kMsgContainer) return false;
        uint32_t count = r.u32();
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t len = r.u32();
            if (r.remaining() < len) return false;
            Bytes raw(len);
            r.raw(raw.data(), len);
            out->push_back(std::move(raw));
        }
        return true;
    } catch (const ProtocolError&) {
        return false;
    }
}

}  // namespace tgnx::mt
