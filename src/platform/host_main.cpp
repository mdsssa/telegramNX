// Interactive macOS build of the same UI/client, for fast iteration without
// touching the Switch. Uses SDL2 for the window, keyboard and mouse.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "core/client.h"
#include "platform/platform.h"
#include "ui/app.h"
#include "ui/canvas.h"

using namespace tgnx;

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

ui::Fonts g_fonts;
ui::App g_app;
TelegramClient g_client;
ClientConfig g_config;
std::vector<api::PeerRef> g_peers;
std::vector<api::DialogInfo> g_dialogs;
api::PeerRef g_currentPeer;
std::string g_currentTitle;
SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_texture = nullptr;

std::string formatTime(int timestamp) {
    if (timestamp <= 0) return "";
    time_t t = time_t(timestamp);
    struct tm tmv {};
    localtime_r(&t, &tmv);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M", &tmv);
    return buf;
}

void dropLastUtf8(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    s.erase(i);
}

void present(ui::Canvas& canvas) {
    if (!g_renderer || !g_texture) return;
    SDL_UpdateTexture(g_texture, nullptr, canvas.pixels(), canvas.width() * 4);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
}

bool loadFont(ui::Font& font, const char* path, float size) {
    std::string data;
    if (!platform::readFile(path, &data)) {
        std::fprintf(stderr, "missing font: %s\n", path);
        return false;
    }
    return font.load(data, size);
}

void drawText(ui::Canvas& c, const ui::Font& f, int x, int y, const std::string& text,
              ui::Color color) {
    f.draw(c, float(x), float(y) + f.ascent(), text, color);
}

std::string prompt(const std::string& title, std::string value, bool password = false) {
    ui::Canvas canvas(kWidth, kHeight);
    const ui::Color bg = ui::rgb(14, 22, 33);
    const ui::Color panel = ui::rgb(23, 33, 43);
    const ui::Color accent = ui::rgb(51, 144, 236);
    const ui::Color text = ui::rgb(240, 244, 248);
    const ui::Color muted = ui::rgb(125, 142, 158);
    std::string shown = value;
    while (true) {
        canvas.clear(bg);
        ui::Rect card{kWidth / 2 - 340, kHeight / 2 - 130, 680, 260};
        canvas.fillRoundRect(card, 22, panel);
        drawText(canvas, g_fonts.bold, card.x + 28, card.y + 26, title, text);
        ui::Rect field{card.x + 28, card.y + 92, card.w - 56, 58};
        canvas.fillRoundRect(field, 14, ui::rgb(32, 43, 54));
        std::string display = password ? std::string(shown.size(), '*') : shown;
        drawText(canvas, g_fonts.regular, field.x + 18, field.y + 16,
                 g_fonts.regular.ellipsize(display, float(field.w - 36)), text);
        // cursor
        float w = g_fonts.regular.measure(password ? std::string(shown.size(), '*') : shown);
        if (w < field.w - 40) {
            canvas.fillRect({field.x + 18 + int(w) + 2, field.y + 14, 2, 30}, accent);
        }
        drawText(canvas, g_fonts.small, card.x + 28, card.y + 174,
                 "Enter — подтвердить, Esc — отмена", muted);
        present(canvas);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return {};
            if (e.type == SDL_TEXTINPUT) {
                value += e.text.text;
                shown = value;
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    return value;
                }
                if (e.key.keysym.sym == SDLK_ESCAPE) return {};
                if (e.key.keysym.sym == SDLK_BACKSPACE) {
                    dropLastUtf8(value);
                    shown = value;
                }
            }
        }
        SDL_Delay(10);
    }
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
    std::string id = prompt("API ID (my.telegram.org)", g_config.apiId ? std::to_string(g_config.apiId) : "");
    if (id.empty()) return;
    std::string hash = prompt("API Hash", g_config.apiHash);
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
            case ClientEvent::Type::PhoneCode: {
                g_app.setStatus("Введите код из Telegram");
                std::string code = prompt("Код из Telegram", "", false);
                if (!code.empty()) {
                    g_client.submitLoginCode(code);
                    g_app.setStatus("Проверка кода…");
                } else {
                    g_app.setStatus("Код не введён", true);
                }
                break;
            }
            case ClientEvent::Type::NeedPassword: {
                std::string pwd = prompt(ev.text.empty() ? "Пароль 2FA" : ev.text, "", true);
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
            case ClientEvent::Type::Error:
                g_app.setStatus(ev.text, true);
                break;
            case ClientEvent::Type::Info:
                g_app.setStatus(ev.text);
                break;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    g_window = SDL_CreateWindow("TelegramNX (macOS host)", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, kWidth, kHeight, SDL_WINDOW_SHOWN);
    if (!g_window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    if (!g_renderer) g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    if (!g_renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(g_renderer, kWidth, kHeight);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
    if (!g_texture) {
        std::fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return 1;
    }

    if (!loadFont(g_fonts.regular, "assets/fonts/NotoSans-Regular.ttf", 24) ||
        !loadFont(g_fonts.bold, "assets/fonts/NotoSans-SemiBold.ttf", 26) ||
        !loadFont(g_fonts.small, "assets/fonts/NotoSans-Regular.ttf", 18)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TelegramNX",
                                 "Не найдены шрифты assets/fonts/*.ttf. Запустите из корня проекта.",
                                 g_window);
        return 1;
    }

    platform::initNetwork();
    g_app.init(&g_fonts);
    g_config = loadConfig();
    if (g_config.apiId == 0) {
        g_app.setScreen(ui::Screen::Setup);
    } else {
        g_app.setApiInfo(std::to_string(g_config.apiId), "…");
        startClient();
    }

    g_app.onSetup = []() { setupAccount(); };
    g_app.onPhoneLogin = []() {
        std::string phone = prompt("Номер телефона (например +79991234567)", "");
        if (phone.empty()) return;
        g_app.setStatus("Запрос кода…");
        g_client.loginWithPhone(phone);
    };
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
    g_app.onCompose = []() {};
    g_app.onLogout = []() {
        g_client.stop();
        g_config.authKeyHex.clear();
        saveConfig(g_config);
        g_app.setLogin("", "Вход…");
        g_client.start(g_config);
    };
    g_app.onRefreshQr = []() {};

    SDL_StartTextInput();
    ui::Canvas canvas(kWidth, kHeight);
    bool running = true;

    while (running) {
        ui::Input in;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_UP: in.up = true; break;
                    case SDLK_DOWN: in.down = true; break;
                    case SDLK_LEFT: in.left = true; break;
                    case SDLK_RIGHT: in.right = true; break;
                    case SDLK_z: in.a = true; break;
                    case SDLK_x: in.b = true; break;
                    case SDLK_c: in.x = true; break;
                    case SDLK_v: in.y = true; break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (g_app.screen() == ui::Screen::Chat) {
                            g_app.onSendText();
                        } else {
                            in.a = true;
                        }
                        break;
                    case SDLK_BACKSPACE:
                        if (g_app.screen() == ui::Screen::Chat) g_app.backspaceComposer();
                        break;
                    case SDLK_ESCAPE:
                        in.b = true;
                        break;
                    default:
                        break;
                }
            } else if (e.type == SDL_TEXTINPUT) {
                if (g_app.screen() == ui::Screen::Chat) g_app.setComposerText(g_app.composerText() + e.text.text);
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                float lx = 0, ly = 0;
                SDL_RenderWindowToLogical(g_renderer, e.button.x, e.button.y, &lx, &ly);
                in.touch = true;
                in.touchX = int(lx);
                in.touchY = int(ly);
            }
        }

        handleClientEvents();
        g_app.update(in);
        g_app.render(canvas);
        present(canvas);
        SDL_Delay(4);
    }

    g_client.stop();
    SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
