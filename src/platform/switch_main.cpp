// Nintendo Switch entry point: framebuffer, gamepad/touch input, swkbd and
// the Telegram client event loop.
#include <cmath>
#include <initializer_list>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <switch.h>

#include "core/client.h"
#include "platform/platform.h"
#include "ui/app.h"
#include "ui/canvas.h"

using namespace tgnx;

namespace {

ClientConfig g_config;
TelegramClient g_client;
ui::App g_app;
std::vector<api::PeerRef> g_peers;
std::vector<api::DialogInfo> g_dialogs;
api::PeerRef g_currentPeer;
std::string g_currentTitle;

std::string formatTime(int timestamp) {
    if (timestamp <= 0) return "";
    time_t t = time_t(timestamp);
    struct tm tmv {};
    localtime_r(&t, &tmv);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M", &tmv);
    return buf;
}

bool loadFontFrom(ui::Font& font, std::initializer_list<const char*> paths, float size) {
    std::string data;
    for (const char* path : paths) {
        if (platform::readFile(path, &data)) return font.load(data, size);
    }
    return false;
}

std::string keyboardInput(const char* guide, const std::string& initial, int maxLen = 512,
                          bool password = false) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return {};
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    if (!initial.empty()) swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetStringLenMax(&kbd, u32(maxLen));
    if (password) swkbdConfigSetPasswordFlag(&kbd, 1);
    std::vector<char> buf(maxLen + 1, 0);
    Result rc = swkbdShow(&kbd, buf.data(), buf.size());
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return {};
    return std::string(buf.data());
}

void convertDialogs(const std::vector<api::DialogInfo>& dialogs) {
    g_peers.clear();
    std::vector<ui::DialogItem> items;
    items.reserve(dialogs.size());
    for (const auto& d : dialogs) {
        ui::DialogItem item;
        item.title = d.title;
        item.preview = d.preview;
        item.time = formatTime(d.lastDate);
        item.unread = d.unread;
        item.channel = d.isChannel;
        items.push_back(std::move(item));
        g_peers.push_back(d.peer);
    }
    g_dialogs = dialogs;
    g_app.setDialogs(std::move(items));
}

void convertMessages(const std::vector<api::MessageInfo>& messages) {
    std::vector<ui::MessageItem> items;
    items.reserve(messages.size());
    for (const auto& m : messages) {
        ui::MessageItem item;
        item.text = m.service ? "Сервисное сообщение" : m.text;
        item.time = formatTime(m.date);
        item.out = m.out;
        item.service = m.service;
        items.push_back(std::move(item));
    }
    g_app.setMessages(std::move(items));
}

void startClient() {
    g_app.setScreen(ui::Screen::Login);
    g_app.setLogin("", "Подключение…");
    g_client.start(g_config);
}

void setupAccount() {
    std::string id = keyboardInput("API ID (my.telegram.org)", g_config.apiId ? std::to_string(g_config.apiId) : "", 16);
    if (id.empty()) return;
    std::string hash = keyboardInput("API Hash", g_config.apiHash, 64);
    if (hash.empty()) return;
    g_config.apiId = std::atoi(id.c_str());
    g_config.apiHash = hash;
    g_config.authKeyHex.clear();
    g_config.dcId = 2;
    g_config.dcIp = "149.154.167.51";
    saveConfig(g_config);
    g_app.setApiInfo(id, "…");
    startClient();
}

void handleClientEvents() {
    ClientEvent ev;
    while (g_client.poll(&ev)) {
        switch (ev.type) {
            case ClientEvent::Type::Qr:
                g_app.setLogin(ev.text, "Ожидание сканирования…");
                break;
            case ClientEvent::Type::LoginOk:
                g_app.setUser(ev.text);
                g_app.setStatus("");
                g_app.setLogin("", "Загрузка чатов…");
                g_client.requestDialogs(30);
                break;
            case ClientEvent::Type::Dialogs:
                convertDialogs(ev.dialogs);
                g_app.setScreen(ui::Screen::Dialogs);
                break;
            case ClientEvent::Type::History:
                g_currentPeer = ev.peer;
                convertMessages(ev.messages);
                if (g_app.screen() == ui::Screen::Chat) g_app.setChatTitle(g_currentTitle, "");
                break;
            case ClientEvent::Type::Sent:
                g_app.setStatus("");
                g_client.requestHistory(g_currentPeer, 40);
                break;
            case ClientEvent::Type::Error:
                g_app.setStatus(ev.text, true);
                break;
            case ClientEvent::Type::NeedPassword: {
                std::string guide = ev.text.empty() ? "Пароль облачной защиты" : ev.text;
                std::string pwd = keyboardInput(guide.c_str(), "", 256, true);
                if (!pwd.empty()) {
                    g_client.submitPassword(pwd);
                    g_app.setStatus("Проверка пароля…");
                } else {
                    g_app.setStatus("Пароль не введён — перезапуск входа", true);
                    g_client.stop();
                    g_client.start(g_config);
                }
                break;
            }
            case ClientEvent::Type::Info:
                g_app.setStatus(ev.text);
                break;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    platform::initNetwork();
    hidInitializeTouchScreen();

    ui::Fonts fonts;
    bool fontsOk = loadFontFrom(fonts.regular,
                                {"sdmc:/switch/telegramnx/fonts/NotoSans-Regular.ttf",
                                 "sdmc:/switch/telegramnx/NotoSans-Regular.ttf",
                                 "romfs:/fonts/NotoSans-Regular.ttf"},
                                24);
    fontsOk = loadFontFrom(fonts.bold,
                           {"sdmc:/switch/telegramnx/fonts/NotoSans-SemiBold.ttf",
                            "sdmc:/switch/telegramnx/NotoSans-SemiBold.ttf",
                            "romfs:/fonts/NotoSans-SemiBold.ttf"},
                           26) &&
             fontsOk;
    fontsOk = loadFontFrom(fonts.small,
                           {"sdmc:/switch/telegramnx/fonts/NotoSans-Regular.ttf",
                            "sdmc:/switch/telegramnx/NotoSans-Regular.ttf",
                            "romfs:/fonts/NotoSans-Regular.ttf"},
                           18) &&
             fontsOk;
    if (!fontsOk) {
        consoleInit(nullptr);
        std::printf("TelegramNX: fonts not found.\nCopy NotoSans-Regular.ttf and "
                    "NotoSans-SemiBold.ttf to sdmc:/switch/telegramnx/fonts/\n");
        consoleExit(nullptr);
        return 1;
    }

    NWindow* win = nwindowGetDefault();
    Framebuffer fb;
    if (R_FAILED(framebufferCreate(&fb, win, 1280, 720, PIXEL_FORMAT_BGRA_8888, 2)) ||
        R_FAILED(framebufferMakeLinear(&fb))) {
        return 1;
    }

    g_app.init(&fonts);

    g_config = loadConfig();
    if (g_config.apiId == 0) {
        g_app.setScreen(ui::Screen::Setup);
    } else {
        g_app.setApiInfo(std::to_string(g_config.apiId), "…");
        startClient();
    }

    g_app.onChatSelected = [](int index) {
        if (index < 0 || index >= int(g_peers.size())) return;
        g_currentPeer = g_peers[index];
        g_currentTitle = g_dialogs[index].title;
        g_app.setChatTitle(g_currentTitle, "загрузка…");
        g_client.requestHistory(g_currentPeer, 40);
    };
    g_app.onSendText = []() {
        std::string text = g_app.composerText();
        if (text.empty() || g_currentPeer.id == 0) return;
        g_client.sendText(g_currentPeer, text);
        g_app.setComposerText("");
        g_app.setStatus("Отправка…");
    };
    g_app.onCompose = []() {
        std::string text = keyboardInput("Сообщение", g_app.composerText());
        if (!text.empty()) g_app.setComposerText(g_app.composerText() + text);
    };
    g_app.onSetup = []() { setupAccount(); };
    g_app.onRefreshQr = []() {};
    g_app.onLogout = []() {
        g_client.stop();
        g_config.authKeyHex.clear();
        saveConfig(g_config);
        g_app.setLogin("", "Вход…");
        g_client.start(g_config);
    };

    ui::Canvas canvas(1280, 720);
    PadState pad;
    padInitializeDefault(&pad);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        ui::Input in;
        in.a = (down & HidNpadButton_A) != 0;
        in.b = (down & HidNpadButton_B) != 0;
        in.x = (down & HidNpadButton_X) != 0;
        in.y = (down & HidNpadButton_Y) != 0;
        in.up = (down & HidNpadButton_Up) != 0;
        in.down = (down & HidNpadButton_Down) != 0;
        in.left = (down & HidNpadButton_Left) != 0;
        in.right = (down & HidNpadButton_Right) != 0;

        HidTouchScreenState touch{};
        if (hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0) {
            in.touch = true;
            in.touchX = int(touch.touches[0].x);
            in.touchY = int(touch.touches[0].y);
        }

        handleClientEvents();
        g_app.update(in);
        g_app.render(canvas);

        u32 stride = 0;
        void* fbptr = framebufferBegin(&fb, &stride);
        const uint32_t* src = canvas.pixels();
        for (int y = 0; y < canvas.height(); ++y) {
            std::memcpy(static_cast<uint8_t*>(fbptr) + size_t(y) * stride, src + size_t(y) * canvas.width(),
                        size_t(canvas.width()) * 4);
        }
        framebufferEnd(&fb);
    }

    g_client.stop();
    framebufferClose(&fb);
    platform::shutdownNetwork();
    return 0;
}
