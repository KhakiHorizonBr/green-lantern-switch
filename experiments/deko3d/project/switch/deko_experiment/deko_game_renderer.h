// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../native_experiment/native/native_renderer.h"
namespace green_lantern::deko {
// Consumed directly by the existing PM4 graphics system in the deko NRO.
// Bookkeeping shares the register-generation code; Vulkan is never initialized.
class GameRenderer : public native::Renderer {
 public:
  GameRenderer();~GameRenderer();
  bool InitializePresentation();
  bool Initialize(rex::ui::vulkan::VulkanProvider*,rex::memory::Memory*,rex::graphics::RegisterFile*);
  void Shutdown();
  void LoadShader(rex::graphics::xenos::ShaderType,const uint32_t*,uint32_t,uint32_t);
  void Draw(const uint32_t*,uint32_t);
  void Resolve();
  void Swap(rex::ui::Presenter*,uint32_t,uint32_t,uint32_t);
  void Flush();void KickRecorder(){}
  void InitializeDriverCache(const std::filesystem::path&,uint32_t){}
  void SetGammaRamp256(const uint32_t*);
  const native::RendererStats& stats()const;
  std::string TakeTextureUploadReport(){return {};}
 private:
  struct Impl;std::unique_ptr<Impl> dk_;
};
}
