// Exercises the RPC layer with a throwaway api_id. A 400 error is a valid
// result here; what matters is that the request/response cycle works.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/auth.h"
#include "core/rpc.h"
#include "core/tl.h"
#include "core/tl_parser.h"
#include "platform/platform.h"

using namespace tgnx;

int main(int argc, char** argv) {
    std::string ip = "149.154.167.51";
    int dcId = 2;
    int apiId = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--ip=", 0) == 0) ip = a.substr(5);
        else if (a.rfind("--dc=", 0) == 0) dcId = std::atoi(a.c_str() + 5);
        else if (a.rfind("--api=", 0) == 0) apiId = std::atoi(a.c_str() + 6);
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
    rpc.setApiCredentials(apiId, "00000000000000000000000000000000");
    TlWriter w;
    w.u32(0xc4f9186b);  // help.getConfig
    auto res = rpc.invoke(w.take(), 15000);
    if (!res.ok) {
        std::printf("rpc error %d: %s\n", res.error.code, res.error.message.c_str());
        return res.error.code == 0 ? 1 : 0;
    }
    try {
        tl::TlNode node = tl::parseObject(res.body);
        auto* opts = tl::getVector(node, "dc_options");
        std::printf("config ok: this_dc=%lld dc_options=%zu\n",
                    (long long)tl::getInt(node, "this_dc"),
                    opts ? opts->size() : 0);
        if (opts) {
            for (size_t i = 0; i < opts->size() && i < 3; ++i) {
                const auto& v = (*opts)[i];
                if (v.kind == tl::TlValue::Kind::Object && v.object) {
                    std::printf("  dc%d %s:%lld\n", (int)tl::getInt(*v.object, "id"),
                                tl::getString(*v.object, "ip_address").c_str(),
                                (long long)tl::getInt(*v.object, "port"));
                }
            }
        }
    } catch (const ProtocolError& e) {
        std::printf("parse error: %s\n", e.what());
    }
    return 0;
}
