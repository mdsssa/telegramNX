// Smoke test for the background client loop (expects API_ID_INVALID with a
// dummy api_id, but exercises connect/handshake/thread/event plumbing).
#include <cstdio>
#include <string>

#include "core/client.h"
#include "platform/platform.h"

using namespace tgnx;

int main() {
    platform::initNetwork();
    TelegramClient client;
    ClientConfig cfg;
    cfg.apiId = 1;
    cfg.apiHash = "00000000000000000000000000000000";
    cfg.dcId = 2;
    cfg.dcIp = "149.154.167.51";
    client.start(cfg);
    uint64_t deadline = platform::monotonicMs() + 30000;
    while (platform::monotonicMs() < deadline) {
        ClientEvent ev;
        while (client.poll(&ev)) {
            std::printf("event %d: %s\n", int(ev.type), ev.text.c_str());
            if (ev.type == ClientEvent::Type::Error) {
                client.stop();
                return 0;
            }
        }
        platform::sleepMs(50);
    }
    client.stop();
    std::printf("no terminal event\n");
    return 1;
}
