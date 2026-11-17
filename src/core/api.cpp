#include "core/api.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "core/tl.h"
#include "core/tl_parser.h"

namespace tgnx::api {

namespace {

constexpr uint32_t kAuthAuthorization = 0x2ea2c0d4;
constexpr uint32_t kAuthorizationSignUpRequired = 0x44747e9a;
constexpr uint32_t kLoginToken = 0x629f1980;
constexpr uint32_t kLoginTokenMigrateTo = 0x68e9916;
constexpr uint32_t kLoginTokenSuccess = 0x390d5c5e;
constexpr uint32_t kUser = 0xb1b8cc83;
constexpr uint32_t kUserEmpty = 0xd3bc4b7a;
constexpr uint32_t kBoolTrue = 0x997275b5;
constexpr uint32_t kBoolFalse = 0xbc799737;

// Parses just the leading fields we need. The object may have many optional
// trailing fields; callers that need to continue must skip them (currently
// only the terminal object of a response is parsed this way).
UserInfo parseUserPrefix(TlReader& r) {
    UserInfo user;
    uint32_t ctor = r.u32();
    if (ctor == kUserEmpty) {
        user.id = uint64_t(r.i64());
        return user;
    }
    if (ctor != kUser) throw ProtocolError("expected user");
    uint32_t flags = r.u32();
    r.u32();  // flags2
    user.id = uint64_t(r.i64());
    if (flags & (1u << 0)) r.i64();  // access_hash
    if (flags & (1u << 1)) user.firstName = r.str();
    if (flags & (1u << 2)) user.lastName = r.str();
    if (flags & (1u << 3)) user.username = r.str();
    if (flags & (1u << 4)) user.phone = r.str();
    user.isBot = (flags & (1u << 14)) != 0;
    return user;
}

}  // namespace

std::string UserInfo::displayName() const {
    std::string name = firstName;
    if (!lastName.empty()) {
        if (!name.empty()) name += " ";
        name += lastName;
    }
    if (name.empty()) name = username;
    if (name.empty()) name = std::to_string(id);
    return name;
}

bool parseAuthAuthorization(const Bytes& data, LoginToken* out, size_t* consumed) {
    TlReader r(data);
    uint32_t ctor = r.u32();
    if (ctor == kAuthorizationSignUpRequired) {
        out->kind = LoginToken::Kind::Invalid;
        return false;
    }
    if (ctor != kAuthAuthorization) return false;

    uint32_t flags = r.u32();
    if (flags & (1u << 1)) {
        // Ambiguous on the wire: either `true` or `otherwise_relogin_days`.
        uint32_t peek = loadLe32(data.data() + r.pos());
        if (peek == kBoolTrue) {
            r.u32();
        } else {
            r.i32();
        }
    }
    if (flags & (1u << 0)) r.i32();  // tmp_sessions
    if (flags & (1u << 2)) r.bytes();  // future_auth_token

    UserInfo user = parseUserPrefix(r);
    out->userName = user.displayName();
    out->kind = LoginToken::Kind::Success;
    if (consumed) *consumed = r.pos();
    return true;
}

LoginToken exportLoginToken(mt::RpcClient& rpc, int apiId, const std::string& apiHash) {
    TlWriter w;
    w.u32(0xb7e085fe);
    w.i32(apiId);
    w.str(apiHash);
    w.u32(0x1cb5c415);  // Vector<long>
    w.u32(0);           // except_ids
    auto res = rpc.invoke(w.take(), 20000);
    LoginToken token;
    if (!res.ok) {
        token.kind = LoginToken::Kind::Invalid;
        token.errorCode = res.error.code;
        token.errorText = res.error.message;
        return token;
    }
    try {
        TlReader r(res.body);
        uint32_t ctor = r.u32();
        if (ctor == kLoginToken) {
            token.kind = LoginToken::Kind::Token;
            token.expires = r.i32();
            token.token = r.bytes();
        } else if (ctor == kLoginTokenMigrateTo) {
            token.kind = LoginToken::Kind::MigrateTo;
            token.dcId = r.i32();
            token.token = r.bytes();
        } else if (ctor == kLoginTokenSuccess) {
            size_t consumed = 0;
            if (!parseAuthAuthorization(Bytes(res.body.begin() + r.pos(), res.body.end()), &token,
                                        &consumed)) {
                token.kind = LoginToken::Kind::Invalid;
            }
        }
    } catch (const ProtocolError&) {
        token.kind = LoginToken::Kind::Invalid;
    }
    return token;
}

LoginToken importLoginToken(mt::RpcClient& rpc, const Bytes& tokenBytes) {
    TlWriter w;
    w.u32(0x95ac5ce4);
    w.bytes(tokenBytes);
    auto res = rpc.invoke(w.take(), 20000);
    LoginToken token;
    if (!res.ok) {
        token.errorCode = res.error.code;
        token.errorText = res.error.message;
        return token;
    }
    try {
        TlReader r(res.body);
        uint32_t ctor = r.u32();
        if (ctor == kLoginTokenSuccess) {
            size_t consumed = 0;
            parseAuthAuthorization(Bytes(res.body.begin() + r.pos(), res.body.end()), &token, &consumed);
        } else if (ctor == kLoginTokenMigrateTo) {
            token.kind = LoginToken::Kind::MigrateTo;
            token.dcId = r.i32();
            token.token = r.bytes();
        }
    } catch (const ProtocolError&) {
    }
    return token;
}

std::string qrLoginUrl(const Bytes& token) { return "tg://login?token=" + base64UrlEncode(token); }

namespace {
Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }
}  // namespace

LoginCodeRequest requestLoginCode(mt::RpcClient& rpc, int apiId, const std::string& apiHash,
                                  const std::string& phone) {
    LoginCodeRequest out;
    TlWriter settings;
    settings.u32(0xad253d78);  // codeSettings
    settings.u32(0);           // flags
    TlWriter w;
    w.u32(0xa677244f);  // auth.sendCode
    w.str(phone);
    w.i32(apiId);
    w.str(apiHash);
    w.raw(settings.data().data(), settings.size());
    auto res = rpc.invoke(w.take(), 20000);
    if (!res.ok) {
        out.error = std::to_string(res.error.code) + " " + res.error.message;
        return out;
    }
    try {
        tl::TlNode root = tl::parseObject(res.body);
        if (root.ctor == 0x2390fe44) {  // auth.sentCodeSuccess
            out.ok = true;
            out.error = "already authorized";
            return out;
        }
        out.codeHash = tl::getString(root, "phone_code_hash");
        out.ok = !out.codeHash.empty();
        if (!out.ok) out.error = "нет phone_code_hash";
    } catch (const ProtocolError& e) {
        out.error = e.what();
    }
    return out;
}

LoginToken signInWithCode(mt::RpcClient& rpc, const std::string& phone,
                          const std::string& codeHash, const std::string& code) {
    TlWriter w;
    w.u32(0x8d52a951);  // auth.signIn
    w.u32(1u << 0);     // phone_code present
    w.str(phone);
    w.str(codeHash);
    w.str(code);
    auto res = rpc.invoke(w.take(), 30000);
    LoginToken token;
    if (!res.ok) {
        token.errorCode = res.error.code;
        token.errorText = res.error.message;
        return token;
    }
    if (!parseAuthAuthorization(res.body, &token, nullptr)) {
        token.errorCode = 400;
        token.errorText = "не удалось разобрать authorization";
    }
    return token;
}

LoginToken signUpWithCode(mt::RpcClient& rpc, const std::string& phone,
                          const std::string& codeHash, const std::string& firstName,
                          const std::string& lastName) {
    TlWriter w;
    w.u32(0xaac7b717);  // auth.signUp
    w.u32(0);           // flags
    w.str(phone);
    w.str(codeHash);
    w.str(firstName);
    w.str(lastName);
    auto res = rpc.invoke(w.take(), 30000);
    LoginToken token;
    if (!res.ok) {
        token.errorCode = res.error.code;
        token.errorText = res.error.message;
        return token;
    }
    if (!parseAuthAuthorization(res.body, &token, nullptr)) {
        token.errorCode = 400;
        token.errorText = "не удалось разобрать authorization";
    }
    return token;
}

bool fetchPasswordParams(mt::RpcClient& rpc, SrpParams* out, std::string* error) {
    TlWriter w;
    w.u32(0x548a30f5);  // account.getPassword
    auto res = rpc.invoke(w.take(), 20000);
    if (!res.ok) {
        if (error) *error = res.error.message;
        return false;
    }
    try {
        tl::TlNode root = tl::parseObject(res.body);
        const tl::TlNode* algo = tl::getObject(root, "current_algo");
        if (!algo) {
            if (error) *error = "облачный пароль не задан";
            return false;
        }
        if (algo->ctor != 0x3a912d4a) {
            if (error) *error = "неподдерживаемый KDF пароля";
            return false;
        }
        out->salt1 = bytesOf(tl::getString(*algo, "salt1"));
        out->salt2 = bytesOf(tl::getString(*algo, "salt2"));
        out->p = bytesOf(tl::getString(*algo, "p"));
        out->g = int32_t(tl::getInt(*algo, "g"));
        out->B = bytesOf(tl::getString(root, "srp_B"));
        out->srpId = tl::getLong(root, "srp_id");
        out->hint = tl::getString(root, "hint");
        return true;
    } catch (const ProtocolError& e) {
        if (error) *error = e.what();
        return false;
    }
}

LoginToken checkPassword(mt::RpcClient& rpc, const SrpParams& params, const std::string& password) {
    LoginToken token;
    SrpCheck check;
    std::string error;
    if (!computeSrpCheck(params, password, &check, &error)) {
        token.errorCode = 400;
        token.errorText = error;
        return token;
    }
    TlWriter inner;
    inner.u32(0xd27ff082);  // inputCheckPasswordSRP
    inner.i64(params.srpId);
    inner.bytes(check.A);
    inner.bytes(check.M1);
    TlWriter w;
    w.u32(0xd18b4d16);  // auth.checkPassword
    w.raw(inner.data().data(), inner.size());
    auto res = rpc.invoke(w.take(), 30000);
    if (!res.ok) {
        token.errorCode = res.error.code;
        token.errorText = res.error.message;
        return token;
    }
    size_t consumed = 0;
    if (!parseAuthAuthorization(res.body, &token, &consumed)) {
        token.errorCode = 400;
        token.errorText = "не удалось разобрать authorization";
    }
    return token;
}

}  // namespace tgnx::api

namespace tgnx::api {



namespace {

void writePeer(TlWriter& w, const PeerRef& peer) {
    switch (peer.kind) {
        case PeerRef::Kind::User:
            w.u32(0xdde8a54c);
            w.i64(peer.id);
            w.i64(int64_t(peer.accessHash));
            break;
        case PeerRef::Kind::Chat:
            w.u32(0x35a95cb9);
            w.i64(peer.id);
            break;
        case PeerRef::Kind::Channel:
            w.u32(0x27bcbbfc);
            w.i64(peer.id);
            w.i64(int64_t(peer.accessHash));
            break;
        case PeerRef::Kind::Self:
            w.u32(0x7da07ec9);  // inputPeerSelf
            break;
    }
}

std::string userTitle(const tl::TlNode& u) {
    std::string name = tl::getString(u, "first_name");
    std::string last = tl::getString(u, "last_name");
    if (!last.empty()) {
        if (!name.empty()) name += " ";
        name += last;
    }
    if (name.empty()) name = tl::getString(u, "username");
    return name;
}

std::string peerTitle(const tl::TlNode& peer, const std::map<int64_t, const tl::TlNode*>& users,
                      const std::map<int64_t, const tl::TlNode*>& chats) {
    if (peer.ctor == 0x59511722) {
        int64_t id = tl::getLong(peer, "user_id");
        auto it = users.find(id);
        if (it != users.end()) return userTitle(*it->second);
        return std::to_string(id);
    }
    if (peer.ctor == 0x36c6019a) {
        int64_t id = tl::getLong(peer, "chat_id");
        auto it = chats.find(id);
        if (it != chats.end()) return tl::getString(*it->second, "title", std::to_string(id));
        return std::to_string(id);
    }
    if (peer.ctor == 0xa2a5371e) {
        int64_t id = tl::getLong(peer, "channel_id");
        auto it = chats.find(id);
        if (it != chats.end()) return tl::getString(*it->second, "title", std::to_string(id));
        return std::to_string(id);
    }
    return "?";
}

PeerRef peerFromObject(const tl::TlNode& peer) {
    PeerRef ref;
    if (peer.ctor == 0x59511722) {
        ref.kind = PeerRef::Kind::User;
        ref.id = tl::getLong(peer, "user_id");
    } else if (peer.ctor == 0x36c6019a) {
        ref.kind = PeerRef::Kind::Chat;
        ref.id = tl::getLong(peer, "chat_id");
    } else if (peer.ctor == 0xa2a5371e) {
        ref.kind = PeerRef::Kind::Channel;
        ref.id = tl::getLong(peer, "channel_id");
    }
    return ref;
}

}  // namespace

void writeInputPeer(TlWriter& w, const PeerRef& peer) { writePeer(w, peer); }

std::vector<DialogInfo> parseDialogs(const Bytes& body) {
    std::vector<DialogInfo> out;
    tl::TlNode root = tl::parseObject(body);
    const auto* dialogs = tl::getVector(root, "dialogs");
    const auto* messages = tl::getVector(root, "messages");
    const auto* chats = tl::getVector(root, "chats");
    const auto* users = tl::getVector(root, "users");
    if (!dialogs) return out;

    std::map<int64_t, const tl::TlNode*> userMap, chatMap;
    if (users) {
        for (const auto& v : *users) {
            if (v.object) userMap[tl::getLong(*v.object, "id")] = v.object.get();
        }
    }
    if (chats) {
        for (const auto& v : *chats) {
            if (v.object) chatMap[tl::getLong(*v.object, "id")] = v.object.get();
        }
    }

    for (size_t i = 0; i < dialogs->size(); ++i) {
        const auto& d = (*dialogs)[i];
        if (!d.object) continue;
        const tl::TlNode* peerObj = tl::getObject(*d.object, "peer");
        if (!peerObj) continue;
        DialogInfo info;
        info.peer = peerFromObject(*peerObj);
        info.title = peerTitle(*peerObj, userMap, chatMap);
        info.unread = int(tl::getInt(*d.object, "unread_count"));
        info.pinned = tl::getBool(*d.object, "pinned");
        info.topMessageId = int(tl::getInt(*d.object, "top_message"));
        info.isChannel = info.peer.kind == PeerRef::Kind::Channel;
        if (info.peer.kind == PeerRef::Kind::User) {
            auto it = userMap.find(info.peer.id);
            if (it != userMap.end()) info.peer.accessHash = uint64_t(tl::getLong(*it->second, "access_hash"));
        } else if (info.peer.kind == PeerRef::Kind::Channel) {
            auto it = chatMap.find(info.peer.id);
            if (it != chatMap.end()) info.peer.accessHash = uint64_t(tl::getLong(*it->second, "access_hash"));
        }
        if (messages && i < messages->size() && (*messages)[i].object) {
            const auto& m = *(*messages)[i].object;
            info.preview = tl::getString(m, "message");
            if (info.preview.empty()) info.preview = tl::getString(m, "action");
            info.lastDate = int(tl::getInt(m, "date"));
        }
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<DialogInfo> loadDialogs(mt::RpcClient& rpc, int limit) {
    TlWriter w;
    w.u32(0xa0f4cb4f);  // messages.getDialogs
    w.u32(0);           // flags
    w.i32(0);           // offset_date
    w.i32(0);           // offset_id
    w.u32(0x7f3b18ea);  // inputPeerEmpty
    w.i32(limit);
    w.i64(0);  // hash
    auto res = rpc.invoke(w.take(), 30000);
    if (!res.ok) return {};
    return parseDialogs(res.body);
}

std::vector<MessageInfo> parseHistory(const Bytes& body) {
    std::vector<MessageInfo> out;
    tl::TlNode root = tl::parseObject(body);
    const auto* messages = tl::getVector(root, "messages");
    if (!messages) return out;
    for (const auto& v : *messages) {
        if (!v.object) continue;
        MessageInfo m;
        m.id = int(tl::getInt(*v.object, "id"));
        m.date = int(tl::getInt(*v.object, "date"));
        m.out = tl::getBool(*v.object, "out");
        m.text = tl::getString(*v.object, "message");
        m.service = v.object->ctor != 0x7600b9d3;
        const tl::TlNode* from = tl::getObject(*v.object, "from_id");
        if (from) {
            m.fromId = tl::getLong(*from, "user_id", tl::getLong(*from, "channel_id"));
        }
        out.push_back(std::move(m));
    }
    // Telegram returns newest-first; the UI draws oldest-first.
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<MessageInfo> loadHistory(mt::RpcClient& rpc, const PeerRef& peer, int limit) {
    TlWriter w;
    w.u32(0x4423e6c5);  // messages.getHistory
    writePeer(w, peer);
    w.i32(0);  // offset_id
    w.i32(0);  // offset_date
    w.i32(0);  // add_offset
    w.i32(limit);
    w.i32(0);  // max_id
    w.i32(0);  // min_id
    w.i64(0);  // hash
    auto res = rpc.invoke(w.take(), 30000);
    if (!res.ok) return {};
    return parseHistory(res.body);
}

bool sendText(mt::RpcClient& rpc, const PeerRef& peer, const std::string& text, uint64_t randomId) {
    TlWriter w;
    w.u32(0xfef48f62);  // messages.sendMessage
    w.u32(0);           // flags
    writePeer(w, peer);
    w.str(text);
    w.i64(int64_t(randomId));
    auto res = rpc.invoke(w.take(), 30000);
    return res.ok;
}

std::string initialsFor(const std::string& title) {
    std::string out;
    bool newWord = true;
    for (unsigned char c : title) {
        if (newWord && c >= 0x20 && out.size() < 8) {
            out.push_back(char(c));
            newWord = false;
        }
        if (c == ' ') newWord = true;
        if (out.size() >= 4) break;
    }
    return out;
}

}  // namespace tgnx::api
