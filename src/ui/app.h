// UI state machine and rendering for the Telegram client.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/canvas.h"
#include "ui/font.h"

namespace tgnx::ui {

struct Fonts {
    Font regular;
    Font bold;
    Font small;
};

struct Input {
    bool up = false, down = false, left = false, right = false;
    bool a = false, b = false, x = false, y = false;
    bool touch = false;
    int touchX = 0, touchY = 0;
};

enum class Screen { Setup, Login, Dialogs, Chat, Settings };

struct DialogItem {
    std::string title;
    std::string preview;
    std::string time;
    int unread = 0;
    bool online = false;
    bool channel = false;
    Color color = 0;
    std::string initials;
};

struct MessageItem {
    std::string text;
    std::string time;
    std::string sender;
    bool out = false;
    bool service = false;
};

class App {
public:
    void init(Fonts* fonts);
    void showMock(Screen screen);
    void setScreen(Screen screen) { screen_ = screen; selected_ = 0; }
    Screen screen() const { return screen_; }

    void setDialogs(std::vector<DialogItem> dialogs);
    void setChatTitle(std::string title, std::string subtitle);
    void setMessages(std::vector<MessageItem> messages);
    void setLogin(const std::string& qrUrl, const std::string& status);
    void setUser(const std::string& name) { userName_ = name; }
    void setApiInfo(const std::string& apiId, const std::string& apiHash);
    void setStatus(std::string text, bool error = false);

    void update(const Input& input);
    void render(Canvas& canvas);

    std::function<void(int)> onChatSelected;
    std::function<void()> onSendText;
    std::function<void()> onCompose;
    std::function<void()> onRefreshQr;
    std::function<void()> onLogout;
    std::function<void()> onSetup;
    std::function<void()> onPhoneLogin;

    const std::string& composerText() const { return composer_; }
    void setComposerText(std::string text) { composer_ = std::move(text); }
    void appendComposer(char32_t cp);
    void backspaceComposer();

private:
    void renderDialogsScreen(Canvas& c);
    void renderChatScreen(Canvas& c);
    void renderLoginScreen(Canvas& c);
    void renderSettingsScreen(Canvas& c);
    void renderSetupScreen(Canvas& c);
    void renderSidebar(Canvas& c, const Rect& area);
    void renderChat(Canvas& c, const Rect& area);
    void renderMessageBubble(Canvas& c, const MessageItem& m, int rightEdge, int& y);
    void renderQr(Canvas& c, const Rect& area, const std::string& url);
    void selectDialog(int index, bool open);

    Fonts* fonts_ = nullptr;
    Screen screen_ = Screen::Dialogs;
    std::vector<DialogItem> dialogs_;
    std::vector<MessageItem> messages_;
    std::string chatTitle_ = "Telegram";
    std::string chatSubtitle_;
    std::string qrUrl_;
    std::string loginStatus_ = "Ожидание сканирования…";
    std::string userName_;
    std::string apiId_;
    std::string apiHashLabel_;
    std::string composer_;
    std::string status_;
    bool statusError_ = false;
    int selected_ = 0;
    int scroll_ = 0;  // message scroll offset in pixels
    int dialogScroll_ = 0;
    bool wide_ = true;
};

}  // namespace tgnx::ui
