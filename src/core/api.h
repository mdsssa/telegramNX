// High-level Telegram API helpers used by the UI.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/rpc.h"
#include "core/tl.h"
#include "core/util.h"

namespace tgnx::api {

struct LoginToken {
    enum class Kind { Invalid, Token, MigrateTo, Success };
    Kind kind = Kind::Invalid;
    int expires = 0;
    Bytes token;
    int dcId = 0;
    std::string userName;
    bool passwordPending = false;
    int errorCode = 0;
    std::string errorText;
};

struct UserInfo {
    uint64_t id = 0;
    std::string firstName;
    std::string lastName;
    std::string username;
    std::string phone;
    bool isBot = false;
    std::string displayName() const;
};

LoginToken exportLoginToken(mt::RpcClient& rpc, int apiId, const std::string& apiHash);
LoginToken importLoginToken(mt::RpcClient& rpc, const Bytes& token);

std::string qrLoginUrl(const Bytes& token);

bool parseAuthAuthorization(const Bytes& data, LoginToken* out, size_t* consumed);

struct PeerRef {
    enum class Kind { User, Chat, Channel };
    Kind kind = Kind::User;
    int64_t id = 0;
    uint64_t accessHash = 0;
};

struct DialogInfo {
    PeerRef peer;
    std::string title;
    std::string preview;
    int unread = 0;
    bool pinned = false;
    int topMessageId = 0;
    int lastDate = 0;
    bool isChannel = false;
};

struct MessageInfo {
    int id = 0;
    int date = 0;
    bool out = false;
    bool service = false;
    int64_t fromId = 0;
    std::string text;
};

std::vector<DialogInfo> loadDialogs(mt::RpcClient& rpc, int limit);
std::vector<DialogInfo> parseDialogs(const Bytes& body);
std::vector<MessageInfo> parseHistory(const Bytes& body);
std::vector<MessageInfo> loadHistory(mt::RpcClient& rpc, const PeerRef& peer, int limit);
bool sendText(mt::RpcClient& rpc, const PeerRef& peer, const std::string& text, uint64_t randomId);
void writeInputPeer(TlWriter& w, const PeerRef& peer);

std::string initialsFor(const std::string& title);

}  // namespace tgnx::api
