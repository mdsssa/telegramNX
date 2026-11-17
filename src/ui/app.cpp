#include "ui/app.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "qrcodegen.hpp"

namespace tgnx::ui {

namespace {

const Color kBg = rgb(14, 22, 33);
const Color kPanel = rgb(23, 33, 43);
const Color kPanelAlt = rgb(32, 43, 54);
const Color kHeader = rgb(23, 33, 43);
const Color kText = rgb(240, 244, 248);
const Color kMuted = rgb(125, 142, 158);
const Color kAccent = rgb(51, 144, 236);
const Color kBubbleIn = rgb(24, 37, 51);
const Color kBubbleOut = rgb(43, 82, 120);
const Color kDivider = rgb(16, 25, 35);
const Color kGreen = rgb(76, 201, 130);

const Color kAvatarPalette[] = {
    rgb(229, 99, 99),  rgb(235, 146, 72), rgb(214, 177, 62), rgb(103, 190, 108),
    rgb(74, 173, 190), rgb(84, 130, 217), rgb(148, 108, 214), rgb(214, 96, 167),
};

std::string encodeUtf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(char(cp));
    } else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xF0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string initialsOf(const std::string& title) {
    std::string out;
    bool wordStart = true;
    for (char32_t cp : decodeUtf8(title)) {
        if (wordStart && cp > ' ') {
            out += encodeUtf8(cp);
            wordStart = false;
            if (out.size() >= 4) break;
        }
        if (cp == ' ') wordStart = true;
    }
    return out;
}

Color avatarColorFor(const std::string& title) {
    uint32_t h = 2166136261u;
    for (unsigned char c : title) h = (h ^ c) * 16777619u;
    return kAvatarPalette[h % (sizeof(kAvatarPalette) / sizeof(kAvatarPalette[0]))];
}

void drawText(Canvas& c, const Font& f, int x, int y, const std::string& text, Color color) {
    f.draw(c, float(x), float(y) + f.ascent(), text, color);
}

void drawCentered(Canvas& c, const Font& f, const Rect& r, const std::string& text, Color color) {
    float w = f.measure(text);
    float baseline = r.y + (r.h + f.ascent()) * 0.5f - 2.0f;
    f.draw(c, r.x + (r.w - w) * 0.5f, baseline, text, color);
}

std::vector<std::string> wrapText(const Font& f, const std::string& text, float maxWidth) {
    std::vector<std::string> lines;
    std::string line;
    float width = 0;
    auto flush = [&]() {
        lines.push_back(line);
        line.clear();
        width = 0;
    };
    for (char32_t cp : decodeUtf8(text)) {
        if (cp == '\n') {
            flush();
            continue;
        }
        std::string enc = encodeUtf8(cp);
        float cw = f.measureChar(cp);
        if (width + cw > maxWidth && !line.empty()) {
            if (cp != ' ') flush();
        }
        line += enc;
        width += cw;
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.empty()) lines.push_back("");
    return lines;
}

void drawQr(Canvas& c, const Rect& area, const std::string& url) {
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    int n = qr.getSize();
    int quiet = 4;
    int total = n + quiet * 2;
    int scale = std::max(1, std::min(area.w, area.h) / total);
    int size = total * scale;
    int ox = area.x + (area.w - size) / 2;
    int oy = area.y + (area.h - size) / 2;
    c.fillRoundRect({ox, oy, size, size}, 12, rgb(255, 255, 255));
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            if (!qr.getModule(x, y)) continue;
            c.fillRect({ox + (x + quiet) * scale, oy + (y + quiet) * scale, scale, scale},
                       rgb(12, 18, 26));
        }
    }
}

}  // namespace

void App::init(Fonts* fonts) { fonts_ = fonts; }

void App::showMock(Screen screen) {
    dialogs_ = {
        {"Аня", "Привет! Ты уже собрал клиент для Switch?", "12:40", 2, true, false, 0, "А"},
        {"Работа", "Совещание в 15:00, не забудь", "11:05", 5, false, false, 0, "Р"},
        {"Telegram", "Новый вход с Nintendo Switch", "10:22", 0, false, false, 0, "T"},
        {"Мама", "Позвони, когда сможешь", "Вчера", 0, false, false, 0, "М"},
        {"Новости IT", "Вышел новый релиз домашнего клиента", "Вчера", 0, false, true, 0, "Н"},
        {"Друзья", "Миша: го в игру вечером", "Пн", 0, false, false, 0, "Д"},
    };
    for (auto& d : dialogs_) {
        d.color = avatarColorFor(d.title);
        if (d.initials.empty()) d.initials = initialsOf(d.title);
    }
    messages_ = {
        {"Привет! Ты уже собрал клиент для Switch?", "12:38", "", false, false},
        {"Почти. Осталось прикрутить QR-логин и список чатов.", "12:39", "", true, false},
        {"Круто! А картинки и стикеры будут?", "12:40", "", false, false},
        {"Сначала текст, фото и кнопки. Стикеры потом.", "12:40", "", true, false},
        {"Жду!", "12:41", "", false, false},
    };
    chatTitle_ = dialogs_.empty() ? "Telegram" : dialogs_[0].title;
    chatSubtitle_ = "в сети";
    screen_ = screen;
    selected_ = 0;
    scroll_ = 0;
}

void App::setDialogs(std::vector<DialogItem> dialogs) {
    dialogs_ = std::move(dialogs);
    for (auto& d : dialogs_) {
        if (!d.color) d.color = avatarColorFor(d.title);
        if (d.initials.empty()) d.initials = initialsOf(d.title);
    }
    if (selected_ >= int(dialogs_.size())) selected_ = std::max(0, int(dialogs_.size()) - 1);
}

void App::setChatTitle(std::string title, std::string subtitle) {
    chatTitle_ = std::move(title);
    chatSubtitle_ = std::move(subtitle);
    screen_ = Screen::Chat;
    scroll_ = 0;
}

void App::setMessages(std::vector<MessageItem> messages) {
    messages_ = std::move(messages);
    scroll_ = 0;
}

void App::setLogin(const std::string& qrUrl, const std::string& status) {
    qrUrl_ = qrUrl;
    loginStatus_ = status;
    screen_ = Screen::Login;
}

void App::setApiInfo(const std::string& apiId, const std::string& apiHash) {
    apiId_ = apiId;
    apiHashLabel_ = apiHash;
}

void App::setStatus(std::string text, bool error) {
    status_ = std::move(text);
    statusError_ = error;
    if (screen_ == Screen::Login) loginStatus_ = status_;
}

void App::selectDialog(int index, bool open) {
    if (dialogs_.empty()) return;
    index = std::max(0, std::min(index, int(dialogs_.size()) - 1));
    selected_ = index;
    if (open) {
        screen_ = Screen::Chat;
        scroll_ = 0;
        if (onChatSelected) onChatSelected(index);
    }
}

void App::appendComposer(char32_t cp) { composer_ += encodeUtf8(cp); }

void App::backspaceComposer() {
    auto cps = decodeUtf8(composer_);
    if (cps.empty()) return;
    cps.pop_back();
    composer_.clear();
    for (char32_t cp : cps) composer_ += encodeUtf8(cp);
}

void App::update(const Input& input) {
    wide_ = true;  // recomputed during render; keep navigation valid
    if (screen_ == Screen::Dialogs || screen_ == Screen::Chat) {
        if (input.up) selectDialog(selected_ - 1, false);
        if (input.down) selectDialog(selected_ + 1, false);
        if (input.a) {
            if (screen_ == Screen::Dialogs) selectDialog(selected_, true);
            else if (onCompose) onCompose();
        }
        if (input.b && screen_ == Screen::Chat) screen_ = Screen::Dialogs;
        if (input.y) screen_ = Screen::Settings;
        if (input.touch) {
            // Touch handling mirrors render layout: sidebar width 380.
            if (input.touchX < 380) {
                int row = (input.touchY - 64) / 76;
                if (row >= 0 && row < int(dialogs_.size())) selectDialog(row, true);
            } else if (screen_ == Screen::Chat) {
                if (input.touchY > 720 - 72) {
                    if (input.touchX > 1280 - 90) {
                        if (onSendText) onSendText();
                    } else if (onCompose) {
                        onCompose();
                    }
                }
            }
        }
    } else if (screen_ == Screen::Login) {
        if (input.touch && input.touchY > 520 && input.touchY < 580 && input.touchX > 490 &&
            input.touchX < 790) {
            if (onRefreshQr) onRefreshQr();
        }
        if (input.b) screen_ = Screen::Settings;
    } else if (screen_ == Screen::Settings) {
        if (input.b) screen_ = dialogs_.empty() ? Screen::Login : Screen::Dialogs;
        if (input.touch) {
            // Logout row.
            if (input.touchY > 300 && input.touchY < 356) {
                if (onLogout) onLogout();
            }
        }
    } else if (screen_ == Screen::Setup) {
        if (input.a && onSetup) onSetup();
        if (input.b) screen_ = Screen::Login;
    }
}

void App::render(Canvas& c) {
    wide_ = c.width() >= 900;
    c.clear(kBg);
    switch (screen_) {
        case Screen::Login: renderLoginScreen(c); break;
        case Screen::Setup: renderSetupScreen(c); break;
        case Screen::Settings: renderSettingsScreen(c); break;
        case Screen::Chat:
        case Screen::Dialogs:
        default: renderDialogsScreen(c); break;
    }
}

void App::renderDialogsScreen(Canvas& c) {
    int w = c.width();
    int h = c.height();
    int sidebarW = wide_ ? std::min(400, int(w * 0.34f)) : w;
    Rect sidebar{0, 0, sidebarW, h};
    renderSidebar(c, sidebar);
    if (wide_) {
        Rect chat{sidebarW, 0, w - sidebarW, h};
        renderChat(c, chat);
    } else if (screen_ == Screen::Chat) {
        Rect chat{0, 0, w, h};
        renderChat(c, chat);
    }
    if (!status_.empty()) {
        Rect banner{sidebarW + 16, 76, w - sidebarW - 32, 44};
        if (!wide_) banner = {16, 76, w - 32, 44};
        c.fillRoundRect(banner, 12, statusError_ ? withAlpha(rgb(229, 99, 99), 220) : withAlpha(kPanelAlt, 235));
        drawText(c, fonts_->regular, banner.x + 16, banner.y + 9,
                 fonts_->regular.ellipsize(status_, float(banner.w - 32)), kText);
    }

}

void App::renderSidebar(Canvas& c, const Rect& area) {
    c.fillRect(area, kPanel);
    c.fillRect({area.x, area.y, area.w, 64}, kHeader);
    drawText(c, fonts_->bold, area.x + 20, area.y + 14, "Telegram", kText);
    // Search icon.
    int sx = area.right() - 40, sy = 32;
    c.fillCircle(sx, sy, 9, withAlpha(kMuted, 255));
    c.fillCircle(sx, sy, 6, kPanel);
    c.line(sx + 7, sy + 7, sx + 13, sy + 13, kMuted);
    c.line(sx + 8, sy + 7, sx + 14, sy + 13, kMuted);
    c.fillRect({area.x, area.y + 64, area.w, 1}, kDivider);

    Rect list{area.x, area.y + 65, area.w, area.h - 65};
    c.setClip(list);
    int y = list.y + 6;
    for (int i = 0; i < int(dialogs_.size()); ++i) {
        const auto& d = dialogs_[i];
        Rect row{area.x + 6, y, area.w - 12, 76};
        if (i == selected_) c.fillRoundRect(row, 12, kPanelAlt);
        if (row.bottom() > list.bottom()) break;
        // Avatar.
        Rect av{row.x + 10, row.y + 12, 52, 52};
        c.fillCircle(av.x + 26, av.y + 26, 26, d.color);
        drawCentered(c, fonts_->bold, av, d.initials, rgb(255, 255, 255));
        if (d.online) {
            c.fillCircle(av.x + 44, av.y + 42, 7, kPanel);
            c.fillCircle(av.x + 44, av.y + 42, 5, kGreen);
        }
        int textX = row.x + 74;
        int rightPad = 16;
        int timeW = 64;
        drawText(c, fonts_->bold, textX, row.y + 16,
                 fonts_->bold.ellipsize(d.title, float(row.w - 74 - timeW)), kText);
        c.setClip({textX, row.y + 40, row.w - 74 - timeW, 30});
        drawText(c, fonts_->regular, textX, row.y + 40,
                 fonts_->regular.ellipsize(d.preview, float(row.w - 74 - timeW)), kMuted);
        c.setClip(list);
        drawText(c, fonts_->small, row.right() - rightPad - int(fonts_->small.measure(d.time)),
                 row.y + 18, d.time, kMuted);
        if (d.unread > 0) {
            std::string badge = d.unread > 99 ? "99+" : std::to_string(d.unread);
            float bw = std::max(26.0f, fonts_->small.measure(badge) + 14);
            Rect br{row.right() - rightPad - int(bw), row.y + 44, int(bw), 24};
            c.fillRoundRect(br, 12, d.channel ? kAccent : kAccent);
            drawCentered(c, fonts_->small, br, badge, rgb(255, 255, 255));
        }
        y += 78;
    }
    c.resetClip();
}

void App::renderChat(Canvas& c, const Rect& area) {
    c.fillRect(area, kBg);
    c.fillRect({area.x, area.y, area.w, 64}, kHeader);
    c.fillRect({area.x, area.y + 64, area.w, 1}, kDivider);

    if (!wide_) {
        // Back arrow.
        int bx = area.x + 26, by = 32;
        c.line(bx + 8, by - 9, bx, by, kText);
        c.line(bx, by, bx + 8, by + 9, kText);
        c.line(bx + 1, by - 9, bx + 1, by + 9, kText);
    }
    int titleX = area.x + (wide_ ? 20 : 56);
    drawText(c, fonts_->bold, titleX, area.y + 10, chatTitle_, kText);
    drawText(c, fonts_->small, titleX, area.y + 36, chatSubtitle_, chatSubtitle_.empty() ? kMuted : kGreen);

    Rect messages{area.x, area.y + 65, area.w, area.h - 65 - 72};
    c.setClip(messages);
    // Background pattern: subtle dots.
    for (int y = messages.y; y < messages.bottom(); y += 48) {
        for (int x = messages.x + 24; x < messages.right(); x += 48) {
            c.blendPixel(x, y, rgb(30, 45, 60), 120);
        }
    }

    // Measure from the bottom.
    int pad = 14;
    int maxBubble = std::min(int(messages.w * 0.72f), 520);
    int textMax = maxBubble - pad * 2;
    std::vector<int> heights(messages_.size());
    for (size_t i = 0; i < messages_.size(); ++i) {
        if (messages_[i].service) {
            heights[i] = 34;
            continue;
        }
        auto lines = wrapText(fonts_->regular, messages_[i].text, float(textMax));
        heights[i] = pad * 2 + int(lines.size()) * int(fonts_->regular.lineHeight()) + 20;
    }
    int bottom = messages.bottom() - 12 + scroll_;
    std::vector<int> tops(messages_.size());
    for (int i = int(messages_.size()) - 1; i >= 0; --i) {
        bottom -= heights[i] + 8;
        tops[i] = bottom;
    }
    int rightEdge = messages.right() - 18;
    for (size_t i = 0; i < messages_.size(); ++i) {
        if (tops[i] + heights[i] < messages.y - 40 || tops[i] > messages.bottom() + 40) continue;
        const auto& m = messages_[i];
        if (m.service) {
            std::string t = m.text.empty() ? "Сервисное сообщение" : m.text;
            float tw = fonts_->small.measure(t) + 24;
            Rect pill{int(messages.x + messages.w / 2 - tw / 2), tops[i] + 4, int(tw), 26};
            c.fillRoundRect(pill, 13, withAlpha(rgb(30, 44, 58), 220));
            drawCentered(c, fonts_->small, pill, t, kMuted);
            continue;
        }
        auto lines = wrapText(fonts_->regular, m.text, float(textMax));
        int bubbleW = 0;
        for (const auto& line : lines) bubbleW = std::max(bubbleW, int(fonts_->regular.measure(line)));
        bubbleW = std::min(maxBubble, bubbleW + pad * 2);
        int bubbleH = pad * 2 + int(lines.size()) * int(fonts_->regular.lineHeight()) + 20;
        int x = m.out ? rightEdge - bubbleW : messages.x + 18;
        Rect bubble{x, tops[i], bubbleW, bubbleH};
        if (bubble.bottom() < messages.y || bubble.y > messages.bottom()) continue;
        c.fillRoundRect(bubble, 16, m.out ? kBubbleOut : kBubbleIn);
        if (m.out) {
            // Tail.
            c.fillRoundRect({bubble.right() - 6, bubble.bottom() - 18, 6, 18}, 3, kBubbleOut);
        }
        int ty = bubble.y + pad;
        for (const auto& line : lines) {
            drawText(c, fonts_->regular, bubble.x + pad, ty, line, kText);
            ty += int(fonts_->regular.lineHeight());
        }
        if (!m.sender.empty() && !m.out) {
            drawText(c, fonts_->small, bubble.x + pad, bubble.y + pad - 2, m.sender, kAccent);
        }
        int tw = int(fonts_->small.measure(m.time));
        drawText(c, fonts_->small, bubble.right() - pad - tw, bubble.bottom() - 22, m.time, kMuted);
    }
    c.resetClip();

    // Composer.
    Rect comp{area.x, area.bottom() - 72, area.w, 72};
    c.fillRect(comp, kHeader);
    c.fillRect({comp.x, comp.y, comp.w, 1}, kDivider);
    Rect field{comp.x + 16, comp.y + 12, comp.w - 16 - 80, 48};
    c.fillRoundRect(field, 24, kPanelAlt);
    std::string placeholder = composer_.empty() ? "Сообщение" : composer_;
    drawText(c, fonts_->regular, field.x + 18, field.y + 10,
             fonts_->regular.ellipsize(placeholder, float(field.w - 36)), composer_.empty() ? kMuted : kText);
    // Send button.
    Rect send{comp.right() - 64, comp.y + 12, 48, 48};
    c.fillCircle(send.x + 24, send.y + 24, 24, kAccent);
    c.line(send.x + 16, send.y + 24, send.x + 32, send.y + 24, rgb(255, 255, 255));
    c.line(send.x + 24, send.y + 17, send.x + 32, send.y + 24, rgb(255, 255, 255));
    c.line(send.x + 24, send.y + 31, send.x + 32, send.y + 24, rgb(255, 255, 255));
}

void App::renderLoginScreen(Canvas& c) {
    int w = c.width(), h = c.height();
    c.fillRect({0, 0, w, h}, kBg);
    c.fillCircle(w / 2, 80, 220, withAlpha(kAccent, 22));
    Rect card{w / 2 - 300, 56, 600, h - 112};
    c.fillRoundRect(card, 22, kPanel);
    drawCentered(c, fonts_->bold, {card.x, card.y + 20, card.w, 40}, "Вход в Telegram", kText);
    drawCentered(c, fonts_->small, {card.x, card.y + 62, card.w, 24},
                 "Откройте Telegram на телефоне", kMuted);
    Rect qr{card.x + (card.w - 300) / 2, card.y + 96, 300, 300};
    c.fillRoundRect({qr.x - 12, qr.y - 12, qr.w + 24, qr.h + 24}, 18, rgb(255, 255, 255));
    if (qrUrl_.empty()) {
        drawCentered(c, fonts_->regular, qr, "Загрузка…", kMuted);
    } else {
        drawQr(c, qr, qrUrl_);
    }
    drawCentered(c, fonts_->small, {card.x, card.y + 420, card.w, 24},
                 "Настройки > Устройства > Подключить устройство", kMuted);
    drawCentered(c, fonts_->regular, {card.x, card.y + 452, card.w, 28}, loginStatus_,
                 statusError_ ? rgb(240, 120, 120) : kText);
    Rect refresh{card.x + card.w / 2 - 150, card.y + 500, 300, 48};
    c.fillRoundRect(refresh, 24, kPanelAlt);
    drawCentered(c, fonts_->regular, refresh, "Обновить QR-код", kText);
}

void App::renderSettingsScreen(Canvas& c) {
    int w = c.width(), h = c.height();
    c.fillRect({0, 0, w, h}, kBg);
    Rect panel{0, 0, std::min(w, 760), h};
    c.fillRect(panel, kPanel);
    drawText(c, fonts_->bold, 32, 28, "Настройки", kText);
    drawText(c, fonts_->small, 32, 72, "Аккаунт", kMuted);
    Rect user{24, 100, panel.w - 48, 76};
    c.fillRoundRect(user, 14, kPanelAlt);
    c.fillCircle(user.x + 40, user.y + 38, 24, kAccent);
    drawCentered(c, fonts_->bold, {user.x + 16, user.y + 14, 48, 48},
                 initialsOf(userName_.empty() ? "Switch" : userName_), rgb(255, 255, 255));
    drawText(c, fonts_->bold, user.x + 84, user.y + 16, userName_.empty() ? "Nintendo Switch" : userName_, kText);
    drawText(c, fonts_->small, user.x + 84, user.y + 44, "Ваш аккаунт", kMuted);

    drawText(c, fonts_->small, 32, 210, "Приложение", kMuted);
    Rect api{24, 238, panel.w - 48, 52};
    c.fillRoundRect(api, 12, kPanelAlt);
    drawText(c, fonts_->regular, api.x + 18, api.y + 12, "API ID", kText);
    drawText(c, fonts_->regular, api.right() - 20 - int(fonts_->regular.measure(apiId_)),
             api.y + 12, apiId_.empty() ? "не задан" : apiId_, kMuted);

    Rect logout{24, 310, panel.w - 48, 52};
    c.fillRoundRect(logout, 12, withAlpha(rgb(229, 99, 99), 40));
    drawText(c, fonts_->regular, logout.x + 18, logout.y + 12, "Выйти из аккаунта",
             rgb(240, 120, 120));

    drawText(c, fonts_->small, 32, h - 50, "TelegramNX 0.1 — homebrew для Nintendo Switch", kMuted);
}

void App::renderSetupScreen(Canvas& c) {
    int w = c.width(), h = c.height();
    c.fillRect({0, 0, w, h}, kBg);
    Rect card{w / 2 - 320, h / 2 - 190, 640, 380};
    c.fillRoundRect(card, 22, kPanel);
    drawCentered(c, fonts_->bold, {card.x, card.y + 24, card.w, 40}, "Первый запуск", kText);
    drawCentered(c, fonts_->small, {card.x, card.y + 70, card.w, 26},
                 "Нужны api_id и api_hash вашего приложения", kMuted);
    drawCentered(c, fonts_->regular, {card.x, card.y + 120, card.w, 26},
                 "Создайте его на my.telegram.org", kText);
    drawCentered(c, fonts_->small, {card.x, card.y + 150, card.w, 26},
                 "и положите в config.ini рядом с приложением", kMuted);
    Rect btn{card.x + card.w / 2 - 150, card.y + 230, 300, 48};
    c.fillRoundRect(btn, 24, kAccent);
    drawCentered(c, fonts_->regular, btn, "Продолжить", rgb(255, 255, 255));
    drawCentered(c, fonts_->small, {card.x, card.y + 300, card.w, 24},
                 "Нажмите A, чтобы войти по QR-коду", kMuted);
}

}  // namespace tgnx::ui
