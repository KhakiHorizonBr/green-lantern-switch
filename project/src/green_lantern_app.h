// green_lantern - ReXGlue Recompiled Project
//
// Customize the app by overriding virtual hooks from rex::ReXApp.
#pragma once

#include <rex/rex_app.h>
#include <rex/filesystem.h>
#include <filesystem>

class GreenLanternApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  void OnPreSetup(rex::RuntimeConfig& config) override {
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
};


