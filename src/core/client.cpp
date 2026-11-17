#include "core/client.h"

#include <cstring>
#include <fstream>
#include <sstream>

#include "core/auth.h"
#include "core/mtproto.h"
#include "core/sha.h"
#include "platform/platform.h"

namespace tgnx {

namespace {

std::string dcIpFor(int dcId) {
    switch (dcId) {
        case 1: return "149.154.175.53";
        case 2: return "149.154.167.51";
        case 3: return "149.154.175.100";
        case 4: return "149.154.167.91";
        case 5: return "91.108.56.130";
        default: return "149.154.167.51";
    }
}

uint64_t authKeyIdFor(const Bytes& key) {
    auto digest = sha1(key.data(), key.size());
    return loadLe64(digest.data() + 12);
}

std::string trimCopy(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    size_t b = 0;
    while (b < s.size() && s[b] == ' ') ++b;
    return s.substr(b);
}

}  // namespace

std::string configPath() { return platform::configDir() + "/config.ini"; }

ClientConfig loadConfig() {
    ClientConfig c;
    std::string data;
    if (!platform::readFile(configPath(), &data)) return c;
    std::istringstream in(data);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trimCopy(line.substr(0, eq));
        std::string value = trimCopy(line.substr(eq + 1));
        if (key == "api_id") c.apiId = std::atoi(value.c_str());
        else if (key == "api_hash") c.apiHash = value;
        else if (key == "dc_id") c.dcId = std::atoi(value.c_str());
        else if (key == "dc_ip") c.dcIp = value;
        else if (key == "auth_key") c.authKeyHex = value;
        else if (key == "salt") c.salt = std::strtoull(value.c_str(), nullptr, 16);
        else if (key == "time_offset") c.timeOffsetSec = std::atoll(value.c_str());
        else if (key == "user_name") c.userName = value;
    }
    return c;
}

void saveConfig(const ClientConfig& c) {
    std::ostringstream out;
    out << "api_id=" << c.apiId << "\n";
    out << "api_hash=" << c.apiHash << "\n";
    out << "dc_id=" << c.dcId << "\n";
    out << "dc_ip=" << c.dcIp << "\n";
    out << "auth_key=" << c.authKeyHex << "\n";
    out << "salt=" << std::hex << c.salt << std::dec << "\n";
    out << "time_offset=" << c.timeOffsetSec << "\n";
    out << "user_name=" << c.userName << "\n";
    platform::writeFile(configPath(), out.str());
}

void TelegramClient::start(ClientConfig config) {
    if (running_.exchange(true)) return;
    {
        platform::Lock lock(mutex_);
        config_ = config;
    }
    thread_.start([this, config]() { run(config); });
}

void TelegramClient::stop() {
    running_ = false;
    thread_.join();
}

ClientConfig TelegramClient::config() const {
    platform::Lock lock(mutex_);
    return config_;
}

void TelegramClient::push(ClientEvent event) {
    platform::Lock lock(mutex_);
    events_.push_back(std::move(event));
}

bool TelegramClient::poll(ClientEvent* event) {
    platform::Lock lock(mutex_);
    if (events_.empty()) return false;
    *event = std::move(events_.front());
    events_.pop_front();
    return true;
}

void TelegramClient::requestDialogs(int limit) {
    platform::Lock lock(mutex_);
    commands_.push_back({Command::Type::Dialogs, limit, {}, {}});
}

void TelegramClient::requestHistory(api::PeerRef peer, int limit) {
    platform::Lock lock(mutex_);
    commands_.push_back({Command::Type::History, limit, peer, {}});
}

void TelegramClient::sendText(api::PeerRef peer, std::string text) {
    platform::Lock lock(mutex_);
    commands_.push_back({Command::Type::Send, 0, peer, std::move(text)});
}

void TelegramClient::submitPassword(std::string password) {
    platform::Lock lock(mutex_);
    commands_.push_back({Command::Type::Password, 0, {}, std::move(password)});
}

bool TelegramClient::popCommand(Command* cmd, int waitMs) {
    for (int waited = 0; waited < waitMs && running_; waited += 5) {
        {
            platform::Lock lock(mutex_);
            if (!commands_.empty()) {
                *cmd = std::move(commands_.front());
                commands_.pop_front();
                return true;
            }
        }
        platform::sleepMs(5);
    }
    platform::Lock lock(mutex_);
    if (commands_.empty()) return false;
    *cmd = std::move(commands_.front());
    commands_.pop_front();
    return true;
}

void TelegramClient::run(ClientConfig config) {
    platform::initNetwork();
    std::unique_ptr<mt::ObfuscatedTransport> transport = std::make_unique<mt::ObfuscatedTransport>();
    std::unique_ptr<mt::RpcClient> rpc;
    std::string error;

    if (!transport->connect(config.dcIp, 443, 15000, &error)) {
        push({ClientEvent::Type::Error, "Нет соединения: " + error});
        running_ = false;
        return;
    }
    mt::AuthKey key;
    if (!config.authKeyHex.empty()) {
        // Resume with the saved authorization key: no DH handshake needed.
        key.key = fromHex(config.authKeyHex);
        key.id = authKeyIdFor(key.key);
        key.salt = config.salt;
        key.timeOffsetSec = config.timeOffsetSec;
    } else {
        if (!mt::generateAuthKey(*transport, config.dcId, &key, &error)) {
            push({ClientEvent::Type::Error, "Ошибка рукопожатия: " + error});
            running_ = false;
            return;
        }
    }
    rpc = std::make_unique<mt::RpcClient>(*transport, key);
    rpc->setApiCredentials(config.apiId, config.apiHash);
    rpc->setClientInfo("Nintendo Switch", "Horizon", "0.1.0");

    bool loggedIn = false;
    if (!config.authKeyHex.empty()) {
        TlWriter w;
        w.u32(0xedd4882a);  // updates.getState
        auto res = rpc->invoke(w.take(), 15000);
        if (res.ok) {
            loggedIn = true;
            push({ClientEvent::Type::LoginOk, config.userName});
        } else if (res.error.code != 401 && res.error.code != 0) {
            push({ClientEvent::Type::Error, "Сессия: " + res.error.message});
        }
    }

    if (!loggedIn) {
        auto persistLogin = [&](const mt::AuthKey& k, const std::string& name) {
            config.userName = name;
            config.authKeyHex = hex(k.key);
            config.salt = k.salt;
            config.timeOffsetSec = k.timeOffsetSec;
            saveConfig(config);
            {
                platform::Lock lock(mutex_);
                config_ = config;
            }
            push({ClientEvent::Type::LoginOk, name});
        };

        auto handlePassword = [&](mt::RpcClient& client, const mt::AuthKey& k) {
            std::string perr;
            SrpParams params;
            if (!api::fetchPasswordParams(client, &params, &perr)) {
                push({ClientEvent::Type::Error, "Пароль: " + perr});
                return false;
            }
            push({ClientEvent::Type::NeedPassword, params.hint});
            while (running_) {
                Command cmd;
                if (!popCommand(&cmd, 250)) continue;
                if (cmd.type != Command::Type::Password) continue;
                api::LoginToken lt = api::checkPassword(client, params, cmd.text);
                if (lt.kind == api::LoginToken::Kind::Success) {
                    persistLogin(k, lt.userName);
                    return true;
                }
                push({ClientEvent::Type::Error, "Пароль: " + lt.errorText});
                push({ClientEvent::Type::NeedPassword, params.hint});
            }
            return false;
        };

        bool done = false;
        while (running_ && !done) {
            api::LoginToken token = api::exportLoginToken(*rpc, config.apiId, config.apiHash);
            if (token.kind == api::LoginToken::Kind::Token) {
                push({ClientEvent::Type::Qr, api::qrLoginUrl(token.token)});
                for (int i = 0; i < 20 && running_; ++i) {
                    platform::sleepMs(100);
                }
                continue;
            }
            if (token.kind == api::LoginToken::Kind::Success) {
                persistLogin(key, token.userName);
                done = true;
                break;
            }
            if (token.kind == api::LoginToken::Kind::MigrateTo) {
                push({ClientEvent::Type::Info, "Перенос авторизации на DC" + std::to_string(token.dcId)});
                transport->close();
                auto newTransport = std::make_unique<mt::ObfuscatedTransport>();
                std::string ip = dcIpFor(token.dcId);
                if (!newTransport->connect(ip, 443, 15000, &error)) {
                    push({ClientEvent::Type::Error, "DC: " + error});
                    break;
                }
                mt::AuthKey newKey;
                if (!mt::generateAuthKey(*newTransport, token.dcId, &newKey, &error)) {
                    push({ClientEvent::Type::Error, "DC handshake: " + error});
                    break;
                }
                transport = std::move(newTransport);
                rpc = std::make_unique<mt::RpcClient>(*transport, newKey);
                rpc->setApiCredentials(config.apiId, config.apiHash);
                rpc->setClientInfo("Nintendo Switch", "Horizon", "0.1.0");
                config.dcId = token.dcId;
                config.dcIp = ip;
                api::LoginToken imported = api::importLoginToken(*rpc, token.token);
                if (imported.kind == api::LoginToken::Kind::Success) {
                    persistLogin(newKey, imported.userName);
                    done = true;
                } else if (imported.errorText.find("PASSWORD") != std::string::npos) {
                    if (handlePassword(*rpc, newKey)) done = true;
                } else {
                    push({ClientEvent::Type::Error, "Импорт токена: " + imported.errorText});
                }
                continue;
            }
            if (token.errorText.find("PASSWORD") != std::string::npos) {
                if (handlePassword(*rpc, key)) done = true;
                break;
            }
            push({ClientEvent::Type::Error,
                  "Вход: " + std::to_string(token.errorCode) + " " + token.errorText});
            break;
        }
        if (!done) {
            running_ = false;
            return;
        }
        push({ClientEvent::Type::Info, "Авторизация успешна"});
    }

    bool haveActivePeer = false;
    api::PeerRef activePeer;
    uint64_t lastHistoryPoll = 0;
    uint64_t lastDialogsPoll = platform::monotonicMs();

    while (running_) {
        Command cmd;
        bool gotCommand = popCommand(&cmd, 150);
        try {
            if (gotCommand) {
                if (cmd.type == Command::Type::Dialogs) {
                    ClientEvent ev;
                    ev.type = ClientEvent::Type::Dialogs;
                    ev.dialogs = api::loadDialogs(*rpc, cmd.limit);
                    push(std::move(ev));
                    lastDialogsPoll = platform::monotonicMs();
                } else if (cmd.type == Command::Type::History) {
                    activePeer = cmd.peer;
                    haveActivePeer = true;
                    ClientEvent ev;
                    ev.type = ClientEvent::Type::History;
                    ev.peer = cmd.peer;
                    ev.messages = api::loadHistory(*rpc, cmd.peer, cmd.limit);
                    push(std::move(ev));
                    lastHistoryPoll = platform::monotonicMs();
                } else if (cmd.type == Command::Type::Send) {
                    uint8_t rnd[8];
                    platform::secureRandom(rnd, sizeof(rnd));
                    bool ok = api::sendText(*rpc, cmd.peer, cmd.text, loadLe64(rnd));
                    ClientEvent ev;
                    ev.type = ok ? ClientEvent::Type::Sent : ClientEvent::Type::Error;
                    ev.text = ok ? cmd.text : "Не удалось отправить сообщение";
                    ev.peer = cmd.peer;
                    push(std::move(ev));
                    activePeer = cmd.peer;
                    haveActivePeer = true;
                }
            }
            uint64_t now = platform::monotonicMs();
            if (haveActivePeer && now - lastHistoryPoll > 3000) {
                ClientEvent ev;
                ev.type = ClientEvent::Type::History;
                ev.peer = activePeer;
                ev.messages = api::loadHistory(*rpc, activePeer, 40);
                push(std::move(ev));
                lastHistoryPoll = now;
            }
            if (now - lastDialogsPoll > 30000) {
                ClientEvent ev;
                ev.type = ClientEvent::Type::Dialogs;
                ev.dialogs = api::loadDialogs(*rpc, 30);
                push(std::move(ev));
                lastDialogsPoll = now;
            }
        } catch (const ProtocolError& e) {
            push({ClientEvent::Type::Error, std::string("TL: ") + e.what()});
        }
    }
    transport->close();
}

}  // namespace tgnx
