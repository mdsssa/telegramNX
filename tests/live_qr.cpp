// QR login flow against a real DC. Requires your own api_id/api_hash from
// https://my.telegram.org. Displays a tg:// login URL that the Telegram app
// can scan (encode it as a QR code).
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
    std::string ip = "149.154.167.51";
    int dcId = 2;
    int apiId = 0;
    std::string apiHash;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--ip=", 0) == 0) ip = a.substr(5);
        else if (a.rfind("--dc=", 0) == 0) dcId = std::atoi(a.c_str() + 5);
        else if (a.rfind("--api=", 0) == 0) apiId = std::atoi(a.c_str() + 6);
        else if (a.rfind("--hash=", 0) == 0) apiHash = a.substr(7);
    }
    if (apiId == 0 || apiHash.empty()) {
        std::printf("usage: live_qr --api=ID --hash=HASH [--ip=... --dc=N]\n");
        return 2;
    }
    platform::initNetwork();
    mt::ObfuscatedTransport transport;
    std::string error;
    if (!transport.connect(ip, 443, 10000, &error)) {
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

    uint64_t deadline = platform::monotonicMs() + 300000;
    while (platform::monotonicMs() < deadline) {
        api::LoginToken token = api::exportLoginToken(rpc, apiId, apiHash);
        if (token.kind == api::LoginToken::Kind::Token) {
            std::printf("QR: %s\n", api::qrLoginUrl(token.token).c_str());
            std::fflush(stdout);
        } else if (token.kind == api::LoginToken::Kind::Success) {
            std::printf("LOGIN OK: %s\n", token.userName.c_str());
            return 0;
        } else if (token.kind == api::LoginToken::Kind::MigrateTo) {
            std::printf("MIGRATE to dc %d (not handled yet)\n", token.dcId);
            return 1;
        } else {
            std::printf("ERROR %d: %s\n", token.errorCode, token.errorText.c_str());
            return 1;
        }
        platform::monotonicMs();
        // Sleep ~2s between polls without pulling in a threading dependency.
        struct timespec ts {2, 0};
        nanosleep(&ts, nullptr);
    }
    std::printf("timed out\n");
    return 1;
}
