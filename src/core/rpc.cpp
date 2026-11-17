#include "core/rpc.h"

#include <cstring>
#include <zlib.h>

#include "core/tl.h"
#include "platform/platform.h"

namespace tgnx::mt {

namespace {

constexpr uint32_t kInvokeWithLayer = 0xda9b0d0d;
constexpr uint32_t kInitConnection = 0xc1cd5ea9;
constexpr uint32_t kBadMsgNotification = 0xa7eff811;

bool inflateZlib(const Bytes& input, Bytes* output) {
    for (int windowBits : {15 + 32, 15, -15}) {
        if (output->size()) break;
        z_stream zs{};
        if (inflateInit2(&zs, windowBits) != Z_OK) continue;
        zs.next_in = const_cast<Bytef*>(input.data());
        zs.avail_in = uInt(input.size());
        uint8_t buffer[8192];
        int rc = Z_OK;
        while (rc == Z_OK) {
            zs.next_out = buffer;
            zs.avail_out = sizeof(buffer);
            rc = inflate(&zs, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) break;
            output->insert(output->end(), buffer, buffer + (sizeof(buffer) - zs.avail_out));
        }
        inflateEnd(&zs);
        if (rc == Z_STREAM_END) return true;
        output->clear();
    }
    return false;
}

}  // namespace

RpcClient::RpcClient(Transport& transport, const AuthKey& authKey) : transport_(transport) {
    session_.authKey = authKey.key;
    session_.authKeyId = authKey.id;
    session_.salt = authKey.salt;
    session_.sessionId = newSessionId();
    session_.timeOffsetSec = authKey.timeOffsetSec;
    session_.seqNo = 1;
    session_.msgIds.setTimeOffset(authKey.timeOffsetSec);
}

void RpcClient::setApiCredentials(int apiId, std::string apiHash) {
    apiId_ = apiId;
    apiHash_ = std::move(apiHash);
}

void RpcClient::setClientInfo(std::string deviceModel, std::string systemVersion,
                              std::string appVersion) {
    deviceModel_ = std::move(deviceModel);
    systemVersion_ = std::move(systemVersion);
    appVersion_ = std::move(appVersion);
}

bool RpcClient::sendBody(const Bytes& body, uint64_t* msgId, bool contentRelated) {
    uint64_t usedId = 0;
    Bytes packet = encryptMessage(session_, body, contentRelated, &usedId);
    std::string error;
    if (!transport_.send(packet, 10000, &error)) return false;
    if (msgId) *msgId = usedId;
    return true;
}

Bytes RpcClient::wrapQuery(const Bytes& query) {
    // Every query is wrapped in invokeWithLayer. The first one also carries
    // initConnection, as required by the API.
    Bytes inner = query;
    if (!initialized_) {
        TlWriter init;
        init.u32(kInitConnection);
        init.u32(0);  // flags
        init.i32(apiId_);
        init.str(deviceModel_);
        init.str(systemVersion_);
        init.str(appVersion_);
        init.str("en");
        init.str("");
        init.str("en");
        init.raw(inner.data(), inner.size());
        inner = init.take();
        initialized_ = true;
    }
    TlWriter w;
    w.u32(kInvokeWithLayer);
    w.i32(layer_);
    w.raw(inner.data(), inner.size());
    return w.take();
}

RpcResult RpcClient::invoke(const Bytes& query, int timeoutMs) {
    RpcResult result;
    Bytes body = wrapQuery(query);
    uint64_t msgId = 0;
    if (!sendBody(body, &msgId, true)) {
        result.error = {0, "transport send failed"};
        return result;
    }

    int64_t deadline = int64_t(platform::monotonicMs()) + timeoutMs;
    int saltRetries = 0;
    while (int64_t(platform::monotonicMs()) < deadline) {
        int remaining = int(deadline - int64_t(platform::monotonicMs()));
        if (remaining <= 0) break;
        Bytes packet;
        std::string error;
        if (!transport_.receive(&packet, remaining, &error)) {
            if (error == "timeout") {
                result.error = {0, "timeout"};
            } else {
                result.error = {0, error};
            }
            return result;
        }
        UnencryptedPacket msg;
        if (!decryptMessage(session_, packet, &msg, &error)) {
            platform::logLine("rpc: decrypt failed: " + error);
            continue;
        }
        bool done = false;
        bool retrySalt = false;
        handleServerObject(msg.body, msgId, &result, &done, &retrySalt);
        if (done) return result;
        if (retrySalt && saltRetries++ < 2) {
            if (!sendBody(body, &msgId, true)) {
                result.error = {0, "transport send failed after salt change"};
                return result;
            }
        }
    }
    if (!result.ok && result.error.message.empty()) result.error = {0, "timeout"};
    return result;
}

bool RpcClient::handleServerObject(const Bytes& body, uint64_t waitId, RpcResult* out, bool* done,
                                   bool* retrySalt) {
    if (body.size() < 4) return false;
    uint32_t ctor = loadLe32(body.data());

    if (ctor == kMsgContainer) {
        try {
            TlReader r(body);
            r.u32();
            uint32_t count = r.u32();
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t msgId = uint64_t(r.i64());
                r.i32();  // seqno
                uint32_t len = r.u32();
                Bytes inner(len);
                r.raw(inner.data(), len);
                (void)msgId;
                handleServerObject(inner, waitId, out, done, retrySalt);
                if (*done) return true;
            }
        } catch (const ProtocolError& e) {
            platform::logLine(std::string("rpc: bad container: ") + e.what());
        }
        return true;
    }

    if (ctor == kGzipPacked) {
        try {
            TlReader r(body);
            r.u32();
            Bytes packed = r.bytes();
            Bytes unpacked;
            if (inflateZlib(packed, &unpacked)) handleServerObject(unpacked, waitId, out, done, retrySalt);
        } catch (const ProtocolError&) {
        }
        return true;
    }

    if (ctor == kRpcResult) {
        try {
            TlReader r(body);
            r.u32();
            uint64_t reqId = uint64_t(r.i64());
            uint32_t resultCtor = r.u32();
            if (resultCtor == kGzipPacked) {
                Bytes packed = r.bytes();
                Bytes unpacked;
                if (!inflateZlib(packed, &unpacked)) {
                    platform::logLine("rpc: gzip input " + std::to_string(packed.size()) + " bytes " +
                                      hex(packed.data(), packed.size() < 8 ? packed.size() : 8));
                    if (reqId == waitId) {
                        out->ok = false;
                        out->error = {0, "gzip inflate failed"};
                        *done = true;
                    }
                    return true;
                }
                if (reqId == waitId) {
                    out->ok = true;
                    out->body = std::move(unpacked);
                    *done = true;
                }
                return true;
            }
            if (resultCtor == kRpcError) {
                int32_t code = r.i32();
                std::string text = r.str();
                if (reqId == waitId) {
                    out->ok = false;
                    out->error = {code, text};
                    *done = true;
                }
            } else {
                // Re-serialize the object with its constructor.
                TlWriter w;
                w.u32(resultCtor);
                if (r.remaining()) w.raw(body.data() + r.pos(), r.remaining());
                if (reqId == waitId) {
                    out->ok = true;
                    out->body = w.take();
                    *done = true;
                }
            }
        } catch (const ProtocolError& e) {
            platform::logLine(std::string("rpc: bad rpc_result: ") + e.what());
        }
        return true;
    }

    if (ctor == kBadServerSalt) {
        try {
            TlReader r(body);
            r.u32();
            r.i64();
            r.i32();
            r.i32();
            session_.salt = uint64_t(r.i64());
            *retrySalt = true;
        } catch (const ProtocolError&) {
        }
        return true;
    }

    if (ctor == kNewSessionCreated) {
        try {
            TlReader r(body);
            r.u32();
            r.i64();
            r.i64();
            session_.salt = uint64_t(r.i64());
        } catch (const ProtocolError&) {
        }
        return true;
    }

    if (ctor == kBadMsgNotification) {
        try {
            TlReader r(body);
            r.u32();
            r.i64();
            r.i32();
            int32_t code = r.i32();
            platform::logLine("rpc: bad_msg_notification code=" + std::to_string(code));
        } catch (const ProtocolError&) {
        }
        return true;
    }

    // Updates, acks and pongs are ignored for now.
    return true;
}

}  // namespace tgnx::mt
