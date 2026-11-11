#include <switch.h>

#include <cstdio>

int main(int, char**) {
    const Result mount_result = fsdevMountSdmc();
    std::FILE* log = nullptr;
    if (mount_result == 0) {
        log = std::fopen("sdmc:/switch/green_lantern_launch_test.log", "w");
        if (log) {
            std::fputs("main reached\n", log);
            std::fflush(log);
        }
    }

    consoleInit(nullptr);
    std::printf("Homebrew launch test\n\n");
    std::printf("The NRO reached main().\n");
    std::printf("SD mount result: 0x%08X\n", static_cast<unsigned>(mount_result));
    std::printf("Log: sdmc:/switch/green_lantern_launch_test.log\n\n");
    std::printf("Press + to exit.\n");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad{};
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    if (log) {
        std::fclose(log);
    }
    return 0;
}
