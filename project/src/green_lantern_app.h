// green_lantern - ReXGlue Recompiled Project
//
// Customize the app by overriding virtual hooks from rex::ReXApp.
#pragma once

#include <rex/rex_app.h>
#include <rex/filesystem.h>
#include <filesystem>
#if defined(_WIN32)
#include <thread>
#include <chrono>
#include <cstdio>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/presenter.h>
REXCVAR_DEFINE_STRING(pc_capture_directory, "", "Development",
                     "Explicit desktop comparison screenshots every 5 seconds; empty disables");
#endif
#if defined(GREEN_LANTERN_NATIVE_PC)
#include "switch/native_experiment/native/native_system.h"
#include <rex/logging.h>
#endif

class GreenLanternApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
#if defined(_WIN32)
  void OnPostSetup() override {
    const auto directory = REXCVAR_GET(pc_capture_directory);
    if (directory.empty()) return;
    std::filesystem::create_directories(directory);
    auto* presenter = runtime()->graphics_system()->presenter();
    if (!presenter) return;
    screenshot_worker_ = std::jthread([presenter, directory](std::stop_token stop) {
      uint32_t ordinal = 0;
      auto next = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!stop.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (std::chrono::steady_clock::now() < next) continue;
        next += std::chrono::seconds(5);
        rex::ui::RawImage image;
        if (!presenter->CaptureGuestOutput(image) || !image.width || !image.height) continue;
        const auto path = std::filesystem::path(directory) /
            ("frame_" + std::to_string(ordinal++) + ".ppm");
        FILE* out = std::fopen(path.string().c_str(), "wb");
        if (!out) continue;
        std::fprintf(out, "P6\n%u %u\n255\n", image.width, image.height);
        for (uint32_t y = 0; y < image.height; ++y) {
          for (uint32_t x = 0; x < image.width; ++x)
            std::fwrite(image.data.data() + y * image.stride + x * 4, 1, 3, out);
        }
        std::fclose(out);
      }
    });
  }
  void OnShutdown() override {
    screenshot_worker_.request_stop();
    if (screenshot_worker_.joinable()) screenshot_worker_.join();
  }
#endif

  void OnPreSetup(rex::RuntimeConfig& config) override {
#if defined(GREEN_LANTERN_NATIVE_PC)
    if (green_lantern::native::Enabled()) {
      REXLOG_INFO("[gl-native-pc] Same native renderer sources as Switch; desktop Vulkan driver");
      config.graphics = green_lantern::native::CreateGraphicsSystem();
      return;
    }
#endif
    config.gpu_plugin = "xenos";
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto configured_game_root = std::filesystem::path(GREEN_LANTERN_GAME_DATA_DIR);
    if (std::filesystem::exists(configured_game_root)) {
      paths.game_data_root = configured_game_root;
      return;
    }

    // Portable fallback for the CrossOver transfer bundle. Keep game data
    // outside the bundle and place it in a `game` folder beside the EXE.
    paths.game_data_root = rex::filesystem::GetExecutableFolder() / "game";
  }

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<GreenLanternApp>(
        new GreenLanternApp(ctx, "green_lantern", PPCImageConfig));
  }
#if defined(_WIN32)
 private:
  std::jthread screenshot_worker_;
#endif
};


