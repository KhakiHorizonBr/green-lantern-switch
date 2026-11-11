#pragma once
#include "deko_bindings.h"
#include "deko_images.h"
namespace green_lantern::deko {
// Direct deko3d presentation, called on the owner thread of the supplied
// NWindow. The game must submit its producer work before Present().
class Output {
 public:
  bool Initialize(rex::deko3d::Dispositivo& device,uint32_t width,uint32_t height,
                  NWindow* window,std::span<const uint8_t> vertex,std::span<const uint8_t> fragment);
  bool Present(const Image& source,std::span<const uint32_t,256> gamma,
               uint32_t visible_width=0,uint32_t visible_height=0,uint32_t guest_swizzle=0x688);
  void Shutdown();
  ~Output(){Shutdown();}
 private:
  rex::deko3d::Dispositivo* device_=nullptr;
  rex::deko3d::Nucleo recorder_;
  rex::deko3d::Presentacion swapchain_;
  std::unique_ptr<ShaderLibrary> shaders_;
  const DkShader* vertex_=nullptr;
  const DkShader* fragment_=nullptr;
  uint64_t work_=0;
};
}
