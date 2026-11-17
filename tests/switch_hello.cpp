#include <cstdio>
#include <switch.h>

int main(int argc, char** argv) {
    consoleInit(nullptr);
    std::printf("TelegramNX build check\n");
    std::printf("libnx ABI smoke test\n\n");
    std::printf("If you can read this, libnx init + console work.\n");
    consoleUpdate(nullptr);
    svcSleepThread(8'000'000'000LL);
    consoleExit(nullptr);
    return 0;
}
