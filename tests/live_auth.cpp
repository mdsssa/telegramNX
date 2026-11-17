// Live MTProto handshake check. This talks to a real Telegram DC and is not
// part of the regular unit-test suite.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/auth.h"
#include "core/mtproto.h"
#include "core/util.h"
#include "platform/platform.h"

using namespace tgnx;

int main(int argc, char** argv) {
    platform::initNetwork();

    std::string ip = "149.154.167.51";  // DC2
    int dcId = 2;
    bool plain = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--plain") plain = true;
        else if (arg.rfind("--ip=", 0) == 0) ip = arg.substr(5);
        else if (arg.rfind("--dc=", 0) == 0) dcId = std::atoi(arg.c_str() + 5);
    }
    mt::ObfuscatedTransport obfuscated;
    mt::IntermediateTransport plainTransport;
    mt::Transport& transport = plain ? static_cast<mt::Transport&>(plainTransport)
                                     : static_cast<mt::Transport&>(obfuscated);
    std::string error;
    std::printf("connecting to %s:443 (dc%d) ...\n", ip.c_str(), dcId);
    if (!transport.connect(ip, 443, 10000, &error)) {
        std::printf("connect failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("connected, generating auth key (this does two 2048-bit modpows)\n");

    mt::AuthKey key;
    uint64_t t0 = platform::monotonicMs();
    if (!mt::generateAuthKey(transport, dcId, &key, &error)) {
        std::printf("handshake failed: %s\n", error.c_str());
        return 1;
    }
    uint64_t t1 = platform::monotonicMs();
    std::printf("auth key ok in %llu ms\n", (unsigned long long)(t1 - t0));
    std::printf("  id=%016llx salt=%016llx offset=%llds\n", (unsigned long long)key.id,
                (unsigned long long)key.salt, (long long)key.timeOffsetSec);
    std::printf("  key=%s...\n", hex(key.key.data(), 8).c_str());

    if (!mt::ping(transport, key, 15000, &error)) {
        std::printf("ping failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("encrypted ping/pong ok\n");
    transport.close();
    return 0;
}
