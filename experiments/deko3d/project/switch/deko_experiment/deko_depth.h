// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "deko_bindings.h"
#include "deko_images.h"
namespace green_lantern::deko {
// Exact two-output resolve used by the Vulkan native path: R32 depth for
// k_24_8 fetches and raw guest depth24/stencil bytes in RGBA8 for aliases.
// This records on the producer's recorder, preserving draw/resolve order.
class DepthResolve {
 public:
  DepthResolve(rex::deko3d::Nucleo& recorder,Images& images):recorder_(recorder),images_(images),shaders_(recorder){}
  bool Initialize(std::span<const uint8_t> compute_dksh);
  ImageTransfer Record(uint64_t source,const DkImageRect& from,uint64_t depth,
      uint64_t packed_words,const DkImageRect& to,bool guest_float20e4);
 private:
  rex::deko3d::Nucleo& recorder_;
  Images& images_;
  ShaderLibrary shaders_;
  const DkShader* shader_=nullptr;
};
} // namespace green_lantern::deko
