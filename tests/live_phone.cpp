// Phone-code login against Telegram's test DC (see core.telegram.org/api/auth).
// Example: --ip=149.154.167.40 --port=80 --dc=10002 --phone=+9996621234 --code=22222
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/api.h"
#include "core/auth.h"
#include "core/rpc.h"
#include "platform/platform.h"

using namespace tgnx;

int main(int argc, char** argv) {
    std::string ip = "149.154.167.40";
    int port = 80;
    int dcId = 10002;
    int apiId = 1;
    std::string apiHash = "00000000000000000000000000000000";
    std::string phone = "+9996621234";
    std::string code = "22222";
    std::string first = "Switch";
    std::string last = "Tester";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--ip=", 0) == 0) ip = a.substr(5);
        else if (a.rfind("--port=", 0) == 0) port = std::atoi(a.c_str() + 7);
        else if (a.rfind("--dc=", 0) == 0) dcId = std::atoi(a.c_str() + 5);
        else if (a.rfind("--api=", 0) == 0) apiId = std::atoi(a.c_str() + 6);
        else if (a.rfind("--hash=", 0) == 0) apiHash = a.substr(7);
        else if (a.rfind("--phone=", 0) == 0) phone = a.substr(8);
        else if (a.rfind("--code=", 0) == 0) code = a.substr(7);
        else if (a.rfind("--first=", 0) == 0) first = a.substr(8);
        else if (a.rfind("--last=", 0) == 0) last = a.substr(7);
    }
    platform::initNetwork();
    mt::ObfuscatedTransport transport;
    std::string error;
    if (!transport.connect(ip, uint16_t(port), 15000, &error)) {
        std::printf("connect: %s\n", error.c_str());
        return 1;
    }
    mt::AuthKey key;
    if (!mt::generateAuthKey(transport, dcId, &key, &error)) {
        std::printf("handshake: %s\n", error.c_str());
        return 1;
    }
    mt::RpcClient rpc(transport, key);
    rpc.setApiCredentials(apiId, apiHash);
    rpc.setClientInfo("Nintendo Switch", "Horizon", "0.1.0");

    auto req = api::requestLoginCode(rpc, apiId, apiHash, phone);
    if (!req.ok) {
        std::printf("sendCode failed: %s\n", req.error.c_str());
        return 1;
    }
    std::printf("code hash: %s\n", req.codeHash.c_str());

    api::LoginToken token = api::signInWithCode(rpc, phone, req.codeHash, code);
    if (token.kind != api::LoginToken::Kind::Success &&
        token.errorText.find("UNOCCUPIED") != std::string::npos) {
        std::printf("signing up...\n");
        token = api::signUpWithCode(rpc, phone, req.codeHash, first, last);
    }
    if (token.kind != api::LoginToken::Kind::Success) {
        std::printf("login failed: %d %s\n", token.errorCode, token.errorText.c_str());
        return 1;
    }
    std::printf("logged in as %s\n", token.userName.c_str());

    api::PeerRef self;
    self.kind = api::PeerRef::Kind::Self;
    bool sent = api::sendText(rpc, self, "hello from TelegramNX", 1234567890123456789ULL);
    std::printf("sendMessage to self: %s\n", sent ? "ok" : "failed");
    auto history = api::loadHistory(rpc, self, 10);
    std::printf("history: %zu messages\n", history.size());
    for (const auto& m : history) std::printf("  %d %s: %s\n", m.id, m.out ? "out" : "in", m.text.c_str());
    auto dialogs = api::loadDialogs(rpc, 10);
    std::printf("dialogs: %zu\n", dialogs.size());
    for (const auto& d : dialogs) std::printf("  %s [%d] %s\n", d.title.c_str(), d.unread, d.preview.c_str());
    return 0;
}
