#include "generated/hinted/green_lantern_init.h"

#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/assert.h>

#include "memory_telemetry.h"
#if defined(GREEN_LANTERN_NATIVE_RENDERER)
#include "../native_experiment/native/native_system.h"
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

extern "C" void RexGmSetReadOnlyMapped(bool enabled);
extern "C" void RexGmSetCpuReadOnlyMapped(bool enabled);
extern "C" size_t __nx_heap_size;

#if defined(GREEN_LANTERN_XENOS_FSI)
static std::atomic<bool> fsi_capture_requested{false};
extern "C" void GreenLanternRequestGraphicsCapture(void) {
  fsi_capture_requested.store(true, std::memory_order_relaxed);
}
extern "C" bool GreenLanternConsumeGraphicsCapture(void) {
  return fsi_capture_requested.exchange(false, std::memory_order_relaxed);
}
// Benchmark variant must not inherit intrusive sampling from a previous INI.
extern "C" bool RexSwitchPerfForceNoStackSampling(void) { return true; }
#endif

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
#if defined(GREEN_LANTERN_XENOS_FSI)
    paths.cache_root = paths.user_data_root / "cache_xenos_fsi";
#endif
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
#if defined(GREEN_LANTERN_XENOS_FSI)
    REXLOG_INFO("[gl-fsi-variant] Xenos/Vulkan FSI capture v5 L+R+Minus; guest video mode; CPU-only mapped reads; heap={} MiB; native renderer excluded",
                __nx_heap_size >> 20);
    RexGmSetReadOnlyMapped(false);
    RexGmSetCpuReadOnlyMapped(true);
    const auto require_flag = [](const char* name, const char* value) {
      const bool applied = rex::cvar::SetFlagByName(name, value);
      REXLOG_INFO("[gl-fsi-variant] {}={} applied={}", name, value, applied);
      if (!applied) rex::FatalError(std::string("FSI variant could not set flag: ") + name);
    };
    require_flag("render_target_path_vulkan", "fsi");
    require_flag("vulkan_fsi_required", "true");
    require_flag("switch_unwatch_hot_reads", "false");
    require_flag("async_shader_compilation", "false");
    require_flag("gpu_allow_invalid_fetch_constants", "false");
    require_flag("gpu_compare_ps_hash", "");
    require_flag("gpu_compare_resolve_address", "");
    require_flag("readback_resolve", "none");
    require_flag("draw_resolution_scale_x", "1");
    require_flag("draw_resolution_scale_y", "1");
    require_flag("resolution_scale", "1");
    require_flag("native_2x_msaa", "true");
    require_flag("gpu_timestamp_queries", "true");
    // Change the video mode reported to the game, not just the final window
    // blit. The game still decides its internal target sizes; IssueSwap logs
    // actual frontbuffer dimensions and the profiler records draw widths/area.
    // 540p is above the SDK's minimum height of 480 and keeps a 16:9 aspect.
    unsigned video_mode = 720;
    if (FILE* video_ini = std::fopen("sdmc:/switch/green_lantern/fsi_video.ini", "r")) {
      char line[128];
      while (std::fgets(line, sizeof(line), video_ini)) {
        unsigned requested_mode = 0;
        char trailing = 0;
        if (std::sscanf(line, " video_mode = %u %c", &requested_mode, &trailing) == 1) {
          if (requested_mode == 540 || requested_mode == 720) video_mode = requested_mode;
          else REXLOG_WARN("[gl-fsi-video] Unsupported video_mode={}; keeping {}p", requested_mode, video_mode);
        }
      }
      std::fclose(video_ini);
    }
    video_mode = 720; // Paired capture baseline, preserve the v4 NRO for resolution experiments.
    const unsigned video_width = video_mode == 540 ? 960 : 1280;
    const std::string video_width_text = std::to_string(video_width);
    const std::string video_height_text = std::to_string(video_mode);
    require_flag("video_mode_width", video_width_text.c_str());
    require_flag("video_mode_height", video_height_text.c_str());
    REXLOG_INFO("[gl-fsi-video] Requested guest display {}x{}; FSI/MSAA unchanged; heap unchanged. Actual target size is controlled by the game; inspect swap/draw dimensions.",
                video_width, video_mode);
    REXLOG_INFO("[gl-vk-timing] Stage timers enabled; GPU timestamps requested (nonblocking results). Nested wall times overlap; GPU frame span is not GPU busy time.");
    REXLOG_INFO("[gl-fsi-variant] readonly_mapped=false cpu_readonly_mapped=true; physical backing and ALL GPU aliases keep stock Xenos write tracking; CPU stack sampling forced off");
    config.gpu_plugin = "xenos";
    return;
#endif
#if defined(GREEN_LANTERN_NATIVE_RENDERER)
    rex::cvar::SetFlagByName("nx_renderer", "native");
    if (green_lantern::native::Enabled()) {
      // O perfil 041 mede dezenas de milhares de leituras emuladas/s.
      // A verificação de escritas por quadro é feita por NativeGraphicsSystem.
      bool hot_read_tracking = false;
      bool readonly_mapped = true;
      bool async_pipelines = false;
      unsigned native_stream_mib = 0;
      bool auto_memory = true;
      bool resolve_atlas = true;
      bool signed_resolve_views = true;
      bool depth_stencil_restore = true;
      bool edram_color_cache = true;
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
          if (std::sscanf(line, " resolve_atlas = %u %c", &value, &trailing) == 1 && value <= 1) {
            resolve_atlas = value != 0;
          }
          if (std::sscanf(line, " signed_resolve_views = %u %c", &value, &trailing) == 1 && value <= 1) {
            signed_resolve_views = value != 0;
          }
          if (std::sscanf(line, " depth_stencil_restore = %u %c", &value, &trailing) == 1 && value <= 1) {
            depth_stencil_restore = value != 0;
          }
          if (std::sscanf(line, " edram_color_cache = %u %c", &value, &trailing) == 1 && value <= 1) {
            edram_color_cache = value != 0;
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
      rex::cvar::SetFlagByName("nx_resolve_atlas", resolve_atlas ? "true" : "false");
      rex::cvar::SetFlagByName("nx_signed_resolve_views", signed_resolve_views ? "true" : "false");
      const bool stencil_applied = rex::cvar::SetFlagByName("nx_restore_depth_stencil",
          depth_stencil_restore ? "true" : "false");
      const bool edram_applied = rex::cvar::SetFlagByName("nx_edram_color_cache",
          edram_color_cache ? "true" : "false");
      REXLOG_INFO("[gl-edram] candidate v2 enabled={} applied={}; heap unchanged; color cache requests10MiB",
          edram_color_cache, edram_applied);
      REXLOG_INFO("[gl-stencil] candidate v1 preserve float24 stencil + packed words + known restore pass: enabled={} applied={}; heap unchanged",
          depth_stencil_restore, stencil_applied);
      REXLOG_INFO("Green Lantern signed resolve views: enabled={}", signed_resolve_views);
      REXLOG_INFO("Green Lantern resolve atlas assembly: enabled={}", resolve_atlas);
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
