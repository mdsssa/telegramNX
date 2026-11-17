#include <cstdio>
#include <cstring>

#include "core/api.h"
#include "core/tl.h"

using namespace tgnx;

int main() {
    // auth.authorization { user: user { first_name, last_name } }
    TlWriter user;
    user.u32(0xb1b8cc83);                       // user
    user.u32((1u << 1) | (1u << 2));            // first_name, last_name
    user.u32(0);                                // flags2
    user.i64(123456789);                        // id
    user.str("Иван");
    user.str("Петров");

    TlWriter auth;
    auth.u32(0x2ea2c0d4);  // auth.authorization
    auth.u32(0);           // flags
    auth.raw(user.data().data(), user.size());

    api::LoginToken token;
    size_t consumed = 0;
    bool ok = api::parseAuthAuthorization(auth.data(), &token, &consumed);
    if (!ok || token.kind != api::LoginToken::Kind::Success) {
        std::printf("parse failed\n");
        return 1;
    }
    if (token.userName != "Иван Петров") {
        std::printf("unexpected name: %s\n", token.userName.c_str());
        return 1;
    }

    // Same, but with otherwise_relogin_days present (flag bit 1, integer).
    TlWriter auth2;
    auth2.u32(0x2ea2c0d4);
    auth2.u32(1u << 1);
    auth2.i32(180);
    auth2.raw(user.data().data(), user.size());
    api::LoginToken token2;
    if (!api::parseAuthAuthorization(auth2.data(), &token2, nullptr) ||
        token2.userName != "Иван Петров") {
        std::printf("parse with relogin days failed\n");
        return 1;
    }
    std::printf("api tests passed\n");
    return 0;
}
