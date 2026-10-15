#include "generated/hinted/green_lantern_init.h"

#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/logging.h>

#include "memory_telemetry.h"
#if defined(GREEN_LANTERN_NATIVE_RENDERER)
#include "../native_experiment/native/native_system.h"
#endif

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

extern "C" void RexGmSetReadOnlyMapped(bool enabled);
extern "C" size_t __nx_heap_size;

namespace {

class GreenLanternSwitchApp final : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  void OnConfigurePaths(rex::PathConfig& paths) override {
    // Store the extracted, user-owned Xbox 360 files beside the NRO's folder.
    // The path is rooted at the SD default device so std::filesystem and newlib
    // agree about it on Horizon.
    paths.game_data_root = std::filesystem::path("/switch/green_lantern/game");
    paths.user_data_root = std::filesystem::path("/switch/green_lantern/user");
    paths.cache_root = paths.user_data_root / "cache";
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
#if defined(GREEN_LANTERN_NATIVE_RENDERER)
    if (green_lantern::native::Enabled()) {
      // O perfil 041 mede dezenas de milhares de leituras emuladas/s.
      // A verificação de escritas por quadro é feita por NativeGraphicsSystem.
      bool hot_read_tracking = false;
      bool readonly_mapped = true;
      bool async_pipelines = false;
      unsigned native_stream_mib = 0;
      bool auto_memory = true;
      if (FILE* tuning = std::fopen("sdmc:/switch/green_lantern/tuning.ini", "r")) {
        char line[128];
        while (std::fgets(line, sizeof(line), tuning)) {
          unsigned value = 0;
          char trailing = 0;
          if (std::sscanf(line, " hot_read_tracking = %u %c", &value, &trailing) == 1 &&
              value <= 1) {
            hot_read_tracking = value != 0;
          }
          if (std::sscanf(line, " auto_memory = %u %c", &value, &trailing) == 1 && value <= 1) {
            auto_memory = value != 0;
          }
          if (std::sscanf(line, " native_stream_mib = %u %c", &value, &trailing) == 1 &&
              (value == 0 || (value >= 64 && value <= 192 && value % 16 == 0))) {
            native_stream_mib = value;
          }
          if (std::sscanf(line, " async_pipelines = %u %c", &value, &trailing) == 1 && value <= 1) {
            async_pipelines = value != 0;
          }
          if (std::sscanf(line, " readonly_mapped = %u %c", &value, &trailing) == 1 &&
              value <= 1) {
            readonly_mapped = value != 0;
          }
        }
        std::fclose(tuning);
      }
      if (readonly_mapped) hot_read_tracking = false;
      const bool applied = rex::cvar::SetFlagByName(
          "switch_unwatch_hot_reads", hot_read_tracking ? "true" : "false");
      REXLOG_INFO("Green Lantern native hot-read tracking: enabled={}, applied={}",
                   hot_read_tracking, applied);
      if (!native_stream_mib) native_stream_mib = auto_memory && (__nx_heap_size >> 20) >= 1792 ? 160 : 96;
      const bool auto_applied = rex::cvar::SetFlagByName("nx_auto_memory", auto_memory ? "true" : "false");
      REXLOG_INFO("Green Lantern adaptive memory: enabled={}, applied={}", auto_memory, auto_applied);
      const std::string stream_value = std::to_string(native_stream_mib);
      const bool stream_applied = rex::cvar::SetFlagByName("nx_stream_mib", stream_value);
      REXLOG_INFO("Green Lantern native upload stream: {} MiB, applied={}",
                   native_stream_mib, stream_applied);
      const bool pipelines_applied = rex::cvar::SetFlagByName(
          "nx_async_pipelines", async_pipelines ? "true" : "false");
      REXLOG_INFO("Green Lantern async pipelines: enabled={}, applied={}",
                   async_pipelines, pipelines_applied);
      RexGmSetReadOnlyMapped(readonly_mapped);
      REXLOG_INFO("Green Lantern native read-only mapped: enabled={}", readonly_mapped);
      config.graphics = green_lantern::native::CreateGraphicsSystem();
      return;
    }
#endif
    config.gpu_plugin = "xenos";
  }

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    green_lantern::horizon::StartMemoryTelemetry();
    return std::unique_ptr<GreenLanternSwitchApp>(
        new GreenLanternSwitchApp(ctx, "green_lantern", PPCImageConfig));
  }
};

}  // namespace

// Some generated PPC units reference the C23 rounding function explicitly.
extern "C" float roundevenf(float value) { return std::nearbyintf(value); }

REX_DEFINE_APP(green_lantern, GreenLanternSwitchApp::Create)
