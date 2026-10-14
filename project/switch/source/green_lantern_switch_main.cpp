#include "generated/hinted/green_lantern_init.h"

#include <rex/filesystem.h>
#include <rex/rex_app.h>

#include "memory_telemetry.h"
#if defined(GREEN_LANTERN_NATIVE_RENDERER)
#include "../native_experiment/native/native_system.h"
#endif

#include <cmath>
#include <filesystem>
#include <memory>

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
