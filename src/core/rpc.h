// Synchronous RPC over an established MTProto session.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/mtproto.h"

namespace tgnx::mt {

struct RpcError {
    int code = 0;
    std::string message;
};

struct RpcResult {
    bool ok = false;
    Bytes body;  // full TL object of the result
    RpcError error;
};

class RpcClient {
public:
    RpcClient(Transport& transport, const AuthKey& authKey);

    void setApiCredentials(int apiId, std::string apiHash);
    void setLayer(int layer) { layer_ = layer; }
    void setClientInfo(std::string deviceModel, std::string systemVersion, std::string appVersion);

    // Sends `query` (a fully serialized TL method, including its constructor)
    // and waits for the matching rpc_result.
    RpcResult invoke(const Bytes& query, int timeoutMs);

    Session& session() { return session_; }
    Transport& transport() { return transport_; }

private:
    Bytes wrapQuery(const Bytes& query);
    bool sendBody(const Bytes& body, uint64_t* msgId, bool contentRelated);
    // Processes one decrypted server message. Sets done when it matched waitId.
    bool handleServerObject(const Bytes& body, uint64_t waitId, RpcResult* out, bool* done,
                            bool* retrySalt);

    Transport& transport_;
    Session session_;
    int apiId_ = 0;
    std::string apiHash_;
    int layer_ = 233;
    bool initialized_ = false;
    std::string deviceModel_ = "Nintendo Switch";
    std::string systemVersion_ = "Horizon";
    std::string appVersion_ = "0.1.0";
};

}  // namespace tgnx::mt
