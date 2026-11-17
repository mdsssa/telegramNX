// MTProto packet framing, transport and AES-IGE message encryption.
#pragma once

#include <cstdint>
#include <string>

#include "core/aes.h"
#include "core/net.h"
#include "core/util.h"

namespace tgnx::mt {

constexpr uint32_t kMsgContainer = 0x73f1f8dc;
constexpr uint32_t kGzipPacked = 0x3072cfa1;
constexpr uint32_t kRpcResult = 0xf35c6d01;
constexpr uint32_t kRpcError = 0x2144ca19;
constexpr uint32_t kBadServerSalt = 0xedab447b;
constexpr uint32_t kNewSessionCreated = 0x9ec20908;
constexpr uint32_t kMsgsAck = 0x62d6b459;
constexpr uint32_t kPong = 0x347773c5;
constexpr uint32_t kPing = 0x7abe77ec;

// A framed MTProto transport. Payloads are MTProto packets without the
// 4-byte transport length prefix.
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool connect(const std::string& host, uint16_t port, int timeoutMs, std::string* error) = 0;
    virtual bool send(const Bytes& payload, int timeoutMs, std::string* error) = 0;
    virtual bool receive(Bytes* payload, int timeoutMs, std::string* error) = 0;
    virtual void close() = 0;
    virtual bool valid() const = 0;
};

// Plain "intermediate" framing: 4-byte little-endian length prefix.
class IntermediateTransport : public Transport {
public:
    bool connect(const std::string& host, uint16_t port, int timeoutMs, std::string* error) override;
    bool send(const Bytes& payload, int timeoutMs, std::string* error) override;
    bool receive(Bytes* payload, int timeoutMs, std::string* error) override;
    void close() override;
    bool valid() const override { return sock_.valid(); }

private:
    net::TcpSocket sock_;
};

// Obfuscated2: a random-looking 64-byte header followed by AES-256-CTR
// encrypted intermediate framing. This is what official clients use and it
// passes DPI that drops plain MTProto.
class ObfuscatedTransport : public Transport {
public:
    bool connect(const std::string& host, uint16_t port, int timeoutMs, std::string* error) override;
    bool send(const Bytes& payload, int timeoutMs, std::string* error) override;
    bool receive(Bytes* payload, int timeoutMs, std::string* error) override;
    void close() override;
    bool valid() const override { return sock_.valid(); }

private:
    net::TcpSocket sock_;
    AesCtr sendCtr_;
    AesCtr recvCtr_;
};

struct UnencryptedPacket {
    uint64_t authKeyId = 0;
    uint64_t msgId = 0;
    Bytes body;
};

Bytes makeUnencrypted(uint64_t msgId, const Bytes& body);
bool parseUnencrypted(const Bytes& packet, UnencryptedPacket* out);

uint64_t newSessionId();

// Monotonic MTProto message ids; their high half is unix time in seconds.
class MsgIdGenerator {
public:
    explicit MsgIdGenerator(int64_t timeOffsetSec = 0) : timeOffsetSec_(timeOffsetSec) {}
    uint64_t next();
    void setTimeOffset(int64_t offset) { timeOffsetSec_ = offset; }

private:
    int64_t timeOffsetSec_ = 0;
    uint64_t counter_ = 0;
    uint64_t last_ = 0;
};

struct AuthKey {
    uint64_t id = 0;
    Bytes key;  // 256 bytes, big-endian integer
    uint64_t salt = 0;
    int64_t timeOffsetSec = 0;
};

struct Session {
    Bytes authKey;
    uint64_t authKeyId = 0;
    uint64_t salt = 0;
    uint64_t sessionId = 0;
    int64_t timeOffsetSec = 0;
    uint32_t seqNo = 1;
    MsgIdGenerator msgIds;
};

Bytes encryptMessage(Session& session, const Bytes& body, bool contentRelated = true,
                     uint64_t* msgIdOut = nullptr);
bool decryptMessage(const Session& session, const Bytes& packet, UnencryptedPacket* out,
                    std::string* error);

// The temporary AES key/iv used by the DH handshake.
void deriveTmpKeyIv(const uint8_t serverNonce[16], const uint8_t newNonce[32], uint8_t key[32],
                    uint8_t iv[32]);

// Splits a container into its messages (raw auth_key_id..body packets).
bool unpackContainer(const Bytes& containerBody, std::vector<Bytes>* out);

}  // namespace tgnx::mt
