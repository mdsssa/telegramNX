// Background Telegram client: owns the connection and exposes a simple
// command/event interface to the UI.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "core/api.h"
#include "platform/platform.h"

namespace tgnx {

struct ClientConfig {
    int apiId = 0;
    std::string apiHash;
    int dcId = 2;
    std::string dcIp = "149.154.167.51";
    std::string authKeyHex;
    uint64_t salt = 0;
    int64_t timeOffsetSec = 0;
    std::string userName;
};

ClientConfig loadConfig();
void saveConfig(const ClientConfig& config);
std::string configPath();

struct ClientEvent {
    enum class Type {
        Qr,
        LoginOk,
        NeedPassword,
        Dialogs,
        History,
        Sent,
        Error,
        Info,
    };
    Type type = Type::Info;
    std::string text;
    std::vector<api::DialogInfo> dialogs;
    std::vector<api::MessageInfo> messages;
    api::PeerRef peer;
    int unread = 0;
};

class TelegramClient {
public:
    TelegramClient() = default;
    ~TelegramClient() { stop(); }

    void start(ClientConfig config);
    void stop();
    bool running() const { return running_.load(); }

    void requestDialogs(int limit = 30);
    void requestHistory(api::PeerRef peer, int limit = 40);
    void sendText(api::PeerRef peer, std::string text);
    void submitPassword(std::string password);

    bool poll(ClientEvent* event);
    ClientConfig config() const;

private:
    struct Command {
        enum class Type { Dialogs, History, Send, Password } type;
        int limit = 30;
        api::PeerRef peer;
        std::string text;
    };

    void run(ClientConfig config);
    void push(ClientEvent event);
    bool popCommand(Command* cmd, int waitMs);
    void handleQrLogin(ClientConfig* config);
    bool connectToDc(const std::string& ip, int dcId, ClientConfig* config, std::string* error);

    platform::Thread thread_;
    std::atomic<bool> running_{false};
    mutable platform::Mutex mutex_;
    std::deque<Command> commands_;
    std::deque<ClientEvent> events_;
    ClientConfig config_;
};

}  // namespace tgnx
