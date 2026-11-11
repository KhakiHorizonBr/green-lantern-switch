#include <switch.h>
#include <switch/runtime/pad.h>

#include <rex/image_info.h>
#include <rex/runtime.h>
#include <rex/graphics/vulkan/graphics_system.h>
#include <rex/system/xtypes.h>
#include "green_lantern_init.h"

#include <cstdio>
#include <filesystem>
#include <memory>

extern "C" char* fake_heap_start;
extern "C" char* fake_heap_end;
extern size_t __nx_heap_size;

// Heap sizing and crash/performance hooks come from rexglue_switch_startup.
extern "C" void RexSwitchRuntimeSetupDiagnostic(const char* reason);
static FILE* g_runtime_boot_log = nullptr;

namespace {

// POSIX/newlib on Horizon resolves /switch/... on the SD device. The
// std::filesystem implementation does not understand devoptab's "sdmc:/"
// prefix; absolute("sdmc:/...") treats it as a relative path and breaks
// Runtime::SetupVfs even though fopen/stat can access it directly.
constexpr const char* kGameRoot = "/switch/green_lantern/game";
constexpr const char* kXexPath = "game:\\default.xex";
constexpr const char* kLogPath = "sdmc:/switch/green_lantern_runtime_boot.log";
constexpr u32 kAutoExitFrames = 900;  // 15 seconds at the 60 Hz probe loop.
constexpr u64 kRuntimeHeapTarget = 0x40000000;

void Log(FILE* log, const char* message) {
  consoleUpdate(NULL);
  std::printf("%s\n", message);
  if (log) {
    std::fprintf(log, "%s\n", message);
    std::fflush(log);
  }
}

void ConfigureRuntimeHeap(FILE* log) {
  const bool has_override = envHasHeapOverride();
  const u64 current_size = has_override ? envGetHeapOverrideSize() : __nx_heap_size;
  char message[128];
  std::snprintf(message, sizeof(message), "Heap override: %s, size: 0x%llX",
                has_override ? "yes" : "no",
                static_cast<unsigned long long>(current_size));
  Log(log, message);
  if (current_size <= kRuntimeHeapTarget) {
    Log(log, "Heap resize not needed (already at or below 1 GiB)");
    return;
  }

  void* new_heap = nullptr;
  const Result result = svcSetHeapSize(&new_heap, kRuntimeHeapTarget);
  std::snprintf(message, sizeof(message), "svcSetHeapSize(1 GiB): 0x%08X",
                static_cast<unsigned>(result));
  Log(log, message);
  if (R_SUCCEEDED(result)) {
    fake_heap_start = static_cast<char*>(new_heap);
    fake_heap_end = fake_heap_start + kRuntimeHeapTarget;
  }
}

}  // namespace

extern "C" void RexSwitchRuntimeSetupDiagnostic(const char* reason) {
  if (!g_runtime_boot_log) return;
  std::fprintf(g_runtime_boot_log, "Runtime setup detail: %s\n",
               reason ? reason : "(null)");
  std::fflush(g_runtime_boot_log);
}

int main(int, char**) {
  consoleInit(NULL);
  // libnx initializes HID automatically in applet mode; in title takeover it
  // may not. Its service guard makes this safe in either launch mode.
  const Result hid_result = hidInitialize();
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad{};
  padInitializeAny(&pad);
  FILE* log = nullptr;
  Log(log, "Green Lantern ReXGlue runtime boot probe build 2026-10-09-diag2");
  Log(log, "Phase 1: checking libnx SD mount");
  // libnx attempts fsdevMountSdmc() before main. Do not mount it a second time.
  if (!fsdevGetDeviceFileSystem("sdmc")) {
    const Result fs_result = fsdevGetLastResult();
    char message[128];
    std::snprintf(message, sizeof(message),
                  "FAIL: sdmc is not registered (last FS result 0x%08X)",
                  static_cast<unsigned>(fs_result));
    Log(log, message);
    goto wait_for_exit;
  }
  log = std::fopen(kLogPath, "w");
  g_runtime_boot_log = log;
  Log(log, "Green Lantern runtime boot probe build 2026-10-09-diag9");
  Log(log, "SD card mount is available.");
  {
    char message[80];
    std::snprintf(message, sizeof(message), "HID init result: 0x%08X",
                  static_cast<unsigned>(hid_result));
    Log(log, message);
  }

  Log(log, "Phase 2: checking XEX path");
  {
    const std::filesystem::path xex = std::filesystem::path(kGameRoot) / "default.xex";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(xex, ec)) {
      char message[192];
      std::snprintf(message, sizeof(message),
                    "FAIL: XEX missing/unreadable at %s (filesystem error %d)",
                    xex.string().c_str(), ec.value());
      Log(log, message);
      Log(log, "Copy your legally dumped default.xex and game files into that SD folder.");
      goto wait_for_exit;
    }
  }

  Log(log, "Phase 3: configuring runtime with NVK/Xenos GPU (no guest launch)");
  ConfigureRuntimeHeap(log);
  {
    rex::Runtime runtime(kGameRoot);
    rex::RuntimeConfig config;
    config.tool_mode = false;
    config.graphics = std::make_unique<rex::graphics::vulkan::VulkanGraphicsSystem>();
    // HLE initialization and guest-thread launch remain disabled. This stage
    // only initializes NVK/Xenos, then confirms the game image still loads.
    size_t function_count = 0;
    for (const PPCFuncMapping* mapping = PPCImageConfig.func_mappings;
         mapping && mapping->guest != 0; ++mapping) {
      ++function_count;
    }
    char function_message[96];
    std::snprintf(function_message, sizeof(function_message),
                  "Generated ARM64 function mappings: %u",
                  static_cast<unsigned>(function_count));
    Log(log, function_message);
    const rex::X_STATUS setup = runtime.Setup(PPCImageConfig, std::move(config));
    if (XFAILED(setup)) {
      char message[96];
      std::snprintf(message, sizeof(message), "FAIL: Runtime base setup = 0x%08X",
                    static_cast<unsigned>(setup));
      Log(log, message);
      goto wait_for_exit;
    }
    Log(log, "PASS: Runtime + NVK/Xenos GPU + function table + ARM64 mappings");
    Log(log, "Phase 4: loading XEX into guest memory");
    const rex::X_STATUS load = runtime.LoadXexImage(kXexPath);
    if (XFAILED(load)) {
      char message[96];
      std::snprintf(message, sizeof(message), "FAIL: LoadXexImage = 0x%08X",
                    static_cast<unsigned>(load));
      Log(log, message);
      goto wait_for_exit;
    }
    Log(log, "PASS: XEX parsed and loaded. Guest code was not launched.");
  }

wait_for_exit:
  Log(log, "Log: sdmc:/switch/green_lantern_runtime_boot.log");
  Log(log, "Press +, A, or B to exit; automatic exit in 15 seconds.");
  {
    u32 frames = 0;
    while (appletMainLoop() && frames++ < kAutoExitFrames) {
    padUpdate(&pad);
    const u64 buttons = padGetButtonsDown(&pad);
    if (buttons & (HidNpadButton_Plus | HidNpadButton_A | HidNpadButton_B)) {
      break;
    }
    consoleUpdate(NULL);
      svcSleepThread(16'000'000);
    }
  }
  if (log) std::fclose(log);
  g_runtime_boot_log = nullptr;
  consoleExit(NULL);
  return 0;
}
