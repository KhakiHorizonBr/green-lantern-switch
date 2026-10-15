/**
 * @file        ui/windowed_app_main_switch.cpp
 * @brief       Entry point for windowed applications on Nintendo Switch
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

#include <cstdio>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_switch.h>

// Only the syscall header: switch.h drags typedefs that collide with rex.
extern "C" {
#include <switch/types.h>
#include <switch/kernel/svc.h>
#include <switch/runtime/env.h>
// libnx runtime internals used for the exit below.
extern u32 __nx_applet_exit_mode;
NX_NORETURN void __libnx_exit(int rc);
extern size_t __nx_heap_size;
extern char* fake_heap_start;
extern char* fake_heap_end;
#if defined(GREEN_LANTERN_NATIVE_RENDERER)
bool RexSwitchInitNativeThreadStackReserve(void);
#endif
#if defined(GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB)
bool RexGmBackingArenaInit(size_t size);
#endif
}

namespace {

// Returning from main is not a clean exit for this runtime on Switch. libnx
// never runs .fini_array, so static destructors do not run, and any thread
// owned by a global object (the TimerQueue dispatcher, and in a title the
// guest, GPU and audio threads) outlives the NRO. hbloader then loads hbmenu
// into the same process while those threads still own stacks aliased out of
// the heap, and hbmenu data-aborts on the first heap page it touches (seen on
// console with uiprobe; the Atmosphere report showed the TimerQueue thread
// still waiting in TimerThreadMain). The process has to end instead, which
// takes every thread down with it.
//
// Ending it directly is not right either: svcExitProcess, with or without
// __appExit first, is reported by the system as "closed because an error
// occurred", with no crash report (seen on console). An application must ask
// the system to close it. libnx does that when __nx_applet_exit_mode is 1: its
// applet cleanup installs _appletExitProcess as the exit function, which sends
// ISelfController::Exit and sleeps until the system terminates the process
// (read from libnx's _appletCleanup in the linked binary). __libnx_exit runs
// that path after closing the services, without returning to hbloader.
[[noreturn]] void ExitProcess(int result) {
  REXLOG_INFO("Switch: exiting process ({})", result);
  rex::FlushLogging();
  std::fflush(nullptr);
  __nx_applet_exit_mode = 1;
  __libnx_exit(result);
}

}  // namespace

extern "C" void RexSwitchExitProcess(int result) { ExitProcess(result); }

int main(int argc, char* argv[]) {
  auto remaining = rex::cvar::Init(argc, argv);
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

  // Title-takeover launchers such as Sphaira can give an NRO a heap override
  // larger than __nx_heap_size. libnx honors that override during startup, so
  // setting __nx_heap_size alone does not leave room for the guest memory
  // aliases. Shrink the heap before initializing Vulkan or the game runtime.
  if (envHasHeapOverride() && __nx_heap_size != 0 &&
      envGetHeapOverrideSize() > __nx_heap_size) {
    void* heap_address = nullptr;
    const u32 result = svcSetHeapSize(&heap_address, __nx_heap_size);
    if (R_FAILED(result)) {
      REXLOG_ERROR("Switch: could not shrink loader heap from 0x{:X} to 0x{:X}: "
                   "result 0x{:08X}",
                   envGetHeapOverrideSize(), __nx_heap_size, result);
      ExitProcess(EXIT_FAILURE);
    }
    fake_heap_start = static_cast<char*>(heap_address);
    fake_heap_end = fake_heap_start + __nx_heap_size;
    REXLOG_INFO("Switch: shrank loader heap to 0x{:X} bytes before app initialization",
                 __nx_heap_size);
  }

  REXLOG_INFO("Switch heap: configured={} MiB, malloc range={} MiB, loader override={} MiB",
               __nx_heap_size >> 20, size_t(fake_heap_end - fake_heap_start) >> 20,
               envHasHeapOverride() ? envGetHeapOverrideSize() >> 20 : 0);

#if defined(GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB)
  // Keep guest backing out of the malloc heap before graphics/runtime init can
  // fragment it. The game target opts into this at compile time; other ReXGlue
  // Switch apps retain their existing allocator behavior.
  size_t backing_arena_mib = GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB;
  // Permite ajustar a reserva sem redistribuir o NRO. O SD já está montado
  // pelo startup libnx; esta leitura precede NVK e os workers do aplicativo.
  constexpr const char* kTuningPath = "sdmc:/switch/green_lantern/tuning.ini";
  if (FILE* tuning = std::fopen(kTuningPath, "r")) {
    char line[128];
    while (std::fgets(line, sizeof(line), tuning)) {
      unsigned requested = 0;
      char trailing = 0;
      if (std::sscanf(line, " backing_arena_mib = %u %c", &requested, &trailing) == 1) {
        if (requested >= 256 && requested <= 512 && requested % 2 == 0) {
          backing_arena_mib = requested;
        } else {
          REXLOG_WARN("Switch tuning: invalid backing_arena_mib={}; expected even 256..512",
                       requested);
        }
      }
    }
    std::fclose(tuning);
  }
  const size_t kBackingArenaSize = backing_arena_mib * 1024 * 1024;
  REXLOG_INFO("Switch tuning: backing_arena_mib={} (default={}), path={}",
               backing_arena_mib, GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB, kTuningPath);
  if (!RexGmBackingArenaInit(kBackingArenaSize)) {
    REXLOG_ERROR("Switch: could not reserve the guest backing arena; stopping cleanly");
    ExitProcess(EXIT_FAILURE);
  }
#endif

#if defined(GREEN_LANTERN_NATIVE_RENDERER)
  if (!RexSwitchInitNativeThreadStackReserve()) {
    ExitProcess(EXIT_FAILURE);
  }
#endif

  int result;
  {
    rex::ui::SwitchWindowedAppContext app_context;
    if (!app_context.Initialize()) {
      ExitProcess(EXIT_FAILURE);
    }

    std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(app_context);

    // Match remaining positional args to the app's expected options.
    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));

    const bool initialized = app->OnInitialize();
    if (!initialized) {
      REXLOG_ERROR("Switch: app initialization failed");
    }
    result = initialized ? app_context.RunMainMessageLoop() : EXIT_FAILURE;

    app->InvokeOnDestroy();
  }
  ExitProcess(result);
}
