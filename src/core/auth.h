// Authorization key generation (DH exchange) and a small connectivity check.
#pragma once

#include <string>

#include "core/mtproto.h"

namespace tgnx::mt {

bool generateAuthKey(Transport& transport, int dcId, AuthKey* out, std::string* error);

// Sends an encrypted ping and waits for pong; proves the auth key and message
// encryption are accepted by the server.
bool ping(Transport& transport, const AuthKey& authKey, int timeoutMs, std::string* error);

}  // namespace tgnx::mt
