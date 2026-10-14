#include <switch.h>
#include <deko3d.h>

#include <cstdint>
#include <cstdio>

namespace {

constexpr int kFramebufferCount = 2;
constexpr uint32_t kWidth = 1280;
constexpr uint32_t kHeight = 720;
constexpr uint32_t kCommandMemorySize = 16 * 1024;

DkDevice g_device;
DkMemBlock g_framebuffer_memory;
DkImage g_framebuffers[kFramebufferCount];
DkSwapchain g_swapchain;
DkMemBlock g_command_memory;
DkCmdBuf g_command_buffer;
DkCmdList g_bind_framebuffer[kFramebufferCount];
DkCmdList g_clear_screen;
DkQueue g_render_queue;

const char* g_init_stage = "startup";
std::FILE* g_log = nullptr;

void set_init_stage(const char* stage) {
    g_init_stage = stage;
    if (g_log) {
        std::fputs(stage, g_log);
        std::fputc(10, g_log);
        std::fflush(g_log);
    }
}

void probe_shared_memory_capability() {
    // ReXGlue needs simultaneous aliases over shared backing memory. Probe the
    // least privileged relevant operation before designing that runtime path.
    // This diagnostic allocates only one page and immediately closes it.
    constexpr unsigned kSvcCreateSharedMemory = 0x50;
    if (!envIsSyscallHinted(kSvcCreateSharedMemory)) {
        if (g_log) {
            std::fprintf(g_log,
                         "svcCreateSharedMemory(4 KiB): skipped; SVC 0x50 is not hinted for this process\n");
            std::fflush(g_log);
        }
        return;
    }

    Handle handle = INVALID_HANDLE;
    const Result result = svcCreateSharedMemory(&handle, 0x1000, Perm_Rw, Perm_DontCare);
    if (g_log) {
        std::fprintf(g_log, "svcCreateSharedMemory(4 KiB): result=0x%08X\n",
                     static_cast<unsigned>(result));
        if (R_SUCCEEDED(result)) {
            std::fprintf(g_log, "shared-memory handle created; mapping aliases not tested yet\n");
        }
        std::fflush(g_log);
    }
    if (R_SUCCEEDED(result)) {
        svcCloseHandle(handle);
    }
}

bool graphics_initialize() {
    set_init_stage("create-device");
    DkDeviceMaker device_maker;
    dkDeviceMakerDefaults(&device_maker);
    g_device = dkDeviceCreate(&device_maker);
    if (!g_device) {
        return false;
    }

    set_init_stage("create-framebuffer-memory");
    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, g_device);
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent |
                         DkImageFlags_HwCompression;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;
    layout_maker.dimensions[0] = kWidth;
    layout_maker.dimensions[1] = kHeight;

    DkImageLayout framebuffer_layout;
    dkImageLayoutInitialize(&framebuffer_layout, &layout_maker);
    uint32_t framebuffer_size = dkImageLayoutGetSize(&framebuffer_layout);
    const uint32_t framebuffer_alignment = dkImageLayoutGetAlignment(&framebuffer_layout);
    framebuffer_size = (framebuffer_size + framebuffer_alignment - 1) &
                       ~(framebuffer_alignment - 1);

    DkMemBlockMaker memory_maker;
    dkMemBlockMakerDefaults(&memory_maker, g_device,
                            kFramebufferCount * framebuffer_size);
    memory_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    g_framebuffer_memory = dkMemBlockCreate(&memory_maker);
    if (!g_framebuffer_memory) {
        return false;
    }

    for (int i = 0; i < kFramebufferCount; ++i) {
        dkImageInitialize(&g_framebuffers[i], &framebuffer_layout,
                          g_framebuffer_memory, i * framebuffer_size);
    }

    set_init_stage("create-swapchain");
    DkImage const* swapchain_images[kFramebufferCount];
    for (int i = 0; i < kFramebufferCount; ++i) {
        swapchain_images[i] = &g_framebuffers[i];
    }
    DkSwapchainMaker swapchain_maker;
    dkSwapchainMakerDefaults(&swapchain_maker, g_device, nwindowGetDefault(),
                             swapchain_images, kFramebufferCount);
    g_swapchain = dkSwapchainCreate(&swapchain_maker);
    if (!g_swapchain) {
        return false;
    }

    set_init_stage("create-command-buffer");
    dkMemBlockMakerDefaults(&memory_maker, g_device, kCommandMemorySize);
    memory_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    g_command_memory = dkMemBlockCreate(&memory_maker);
    if (!g_command_memory) {
        return false;
    }

    DkCmdBufMaker command_buffer_maker;
    dkCmdBufMakerDefaults(&command_buffer_maker, g_device);
    g_command_buffer = dkCmdBufCreate(&command_buffer_maker);
    if (!g_command_buffer) {
        return false;
    }
    dkCmdBufAddMemory(g_command_buffer, g_command_memory, 0, kCommandMemorySize);

    set_init_stage("record-render-commands");
    for (int i = 0; i < kFramebufferCount; ++i) {
        DkImageView image_view;
        dkImageViewDefaults(&image_view, &g_framebuffers[i]);
        dkCmdBufBindRenderTarget(g_command_buffer, &image_view, nullptr);
        g_bind_framebuffer[i] = dkCmdBufFinishList(g_command_buffer);
    }

    const DkViewport viewport = {0.0f, 0.0f, static_cast<float>(kWidth),
                                 static_cast<float>(kHeight), 0.0f, 1.0f};
    const DkScissor scissor = {0, 0, kWidth, kHeight};
    dkCmdBufSetViewports(g_command_buffer, 0, &viewport, 1);
    dkCmdBufSetScissors(g_command_buffer, 0, &scissor, 1);
    dkCmdBufClearColorFloat(g_command_buffer, 0, DkColorMask_RGBA,
                            0.02f, 0.22f, 0.06f, 1.0f);
    g_clear_screen = dkCmdBufFinishList(g_command_buffer);

    set_init_stage("create-render-queue");
    DkQueueMaker queue_maker;
    dkQueueMakerDefaults(&queue_maker, g_device);
    queue_maker.flags = DkQueueFlags_Graphics;
    g_render_queue = dkQueueCreate(&queue_maker);
    if (!g_render_queue) {
        return false;
    }

    set_init_stage("graphics-ready");
    return true;
}

void graphics_update() {
    const int slot = dkQueueAcquireImage(g_render_queue, g_swapchain);
    if (slot < 0 || slot >= kFramebufferCount) {
        set_init_stage("acquire-framebuffer-failed");
        return;
    }
    dkQueueSubmitCommands(g_render_queue, g_bind_framebuffer[slot]);
    dkQueueSubmitCommands(g_render_queue, g_clear_screen);
    dkQueuePresentImage(g_render_queue, g_swapchain, slot);
}

void graphics_shutdown() {
    if (g_render_queue) {
        dkQueueWaitIdle(g_render_queue);
        dkQueueDestroy(g_render_queue);
    }
    if (g_command_buffer) {
        dkCmdBufDestroy(g_command_buffer);
    }
    if (g_command_memory) {
        dkMemBlockDestroy(g_command_memory);
    }
    if (g_swapchain) {
        dkSwapchainDestroy(g_swapchain);
    }
    if (g_framebuffer_memory) {
        dkMemBlockDestroy(g_framebuffer_memory);
    }
    if (g_device) {
        dkDeviceDestroy(g_device);
    }
}

void show_init_error(PadState* pad) {
    consoleInit(nullptr);
    std::printf("Green Lantern Switch prototype\n\n");
    std::printf("deko3d initialization failed at:\n%s\n\n", g_init_stage);
    std::printf("Log: sdmc:/switch/green_lantern_switch.log\n");
    std::printf("Press + to return to Sphaira.\n");

    while (appletMainLoop()) {
        padUpdate(pad);
        if (padGetButtonsDown(pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

}  // namespace

int main(int, char**) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad{};
    padInitializeDefault(&pad);

    const Result mount_result = fsdevMountSdmc();
    if (mount_result == 0) {
        g_log = std::fopen("sdmc:/switch/green_lantern_switch.log", "w");
    }
    set_init_stage("startup");
    probe_shared_memory_capability();

    if (!graphics_initialize()) {
        set_init_stage("graphics-initialization-failed");
        graphics_shutdown();
        show_init_error(&pad);
        if (g_log) {
            std::fclose(g_log);
        }
        return 1;
    }

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        graphics_update();
    }

    graphics_shutdown();
    if (g_log) {
        std::fclose(g_log);
    }
    return 0;
}
