// Renders every screen with mock data to BMP files for visual checks.
#include <cstdio>
#include <string>

#include "platform/platform.h"
#include "ui/app.h"
#include "ui/canvas.h"

using namespace tgnx;

static bool loadFont(ui::Font& font, const char* path, float size) {
    std::string data;
    if (!platform::readFile(path, &data)) {
        std::printf("missing font: %s\n", path);
        return false;
    }
    if (!font.load(data, size)) {
        std::printf("failed to load font: %s\n", path);
        return false;
    }
    return true;
}

int main() {
    ui::Fonts fonts;
    if (!loadFont(fonts.regular, "assets/fonts/NotoSans-Regular.ttf", 24)) return 1;
    if (!loadFont(fonts.bold, "assets/fonts/NotoSans-SemiBold.ttf", 26)) return 1;
    if (!loadFont(fonts.small, "assets/fonts/NotoSans-Regular.ttf", 18)) return 1;

    ui::App app;
    app.init(&fonts);
    app.setUser("Алексей");
    app.setApiInfo("1234567", "…");

    struct Shot { ui::Screen screen; const char* name; };
    Shot shots[] = {
        {ui::Screen::Dialogs, "dialogs"},
        {ui::Screen::Chat, "chat"},
        {ui::Screen::Login, "login"},
        {ui::Screen::Settings, "settings"},
        {ui::Screen::Setup, "setup"},
    };
    for (const auto& shot : shots) {
        app.showMock(shot.screen);
        if (shot.screen == ui::Screen::Login) {
            app.setLogin("tg://login?token=1B2M2Y8AsgTpgAmY7PhCfg==", "Ожидание сканирования…");
        }
        ui::Canvas canvas(1280, 720);
        app.render(canvas);
        std::string path = std::string("build/shots/") + shot.name + ".bmp";
        if (!ui::saveBmp(path, canvas)) {
            std::printf("failed to write %s\n", path.c_str());
            return 1;
        }
        std::printf("wrote %s\n", path.c_str());
    }
    return 0;
}
