// The Lanterna shader ABI on deko3d. This is deliberately independent of
// RenderDk's HLSL constants ABI: the two binding layouts must not be mixed.
#pragma once
#include <deko3d.h>
#include <array>
#include <cstdint>
#include <span>
#include <memory>
#include <vector>
#include <rex/ui/deko3d/nucleo.h>
#include "draw_upload_plan.h"

namespace green_lantern::deko {
struct StageBindings {
  // Bindings: system=0, VS float=1, PS float=2, bool/loop=3, fetch=4.
  std::array<DkBufExtents,5> constants{};
  uint32_t constant_mask=0;
  // XeSharedMemory: raw vertex/index data, not a hardware vertex declaration.
  std::array<DkBufExtents,16> storage{};
  uint32_t storage_count=0;
  std::array<DkResHandle,32> textures{};
  uint32_t texture_count=0;
};
// All ranges must remain alive until the submission's fence signals.
// Validation is performed before recording anything; invalid input returns false.
bool BindStage(DkCmdBuf commands,DkStage stage,const StageBindings& bindings);
bool UploadSystem(rex::deko3d::Nucleo& recorder,const void* original,uint32_t bytes,DkBufExtents& result);
struct DrawPacket {
  const DkShader* vertex=nullptr;
  const DkShader* fragment=nullptr;
  const DkShader* geometry=nullptr;
  StageBindings vs,ps,gs;
  std::array<const DkImageView*,4> colors{};
  uint32_t color_count=0;
  const DkImageView* depth=nullptr;
  DkRasterizerState raster{};
  DkMultisampleState samples{};
  DkColorState color{};
  DkColorWriteState writes{};
  std::array<DkBlendState,4> blend{};
  DkDepthStencilState depth_stencil{};
  DkViewport viewport{};
  DkScissor scissor{};
  DkViewportSwizzle swizzle{DkSwizzle_PositiveX,DkSwizzle_PositiveY,DkSwizzle_PositiveZ,DkSwizzle_PositiveW};
  std::array<float,4> blend_constant{};
  // write mask, function reference, compare mask, respectively.
  std::array<uint32_t,3> front_stencil{},back_stencil{};
  float depth_bias=0,depth_bias_clamp=0,depth_bias_slope=0;
  DkPrimitive primitive=DkPrimitive_Triangles;
  uint32_t count=0,first_vertex=0,first_index=0;
  int32_t vertex_offset=0;
  DkGpuAddr indices=0;
  DkIdxFormat index_format=DkIdxFormat_Uint16;
  bool indexed=false,restart=false;
  uint32_t restart_index=UINT32_MAX;
};
// Records one already-prepared draw. Does not parse PM4, decode textures,
// implement EDRAM ownership, submit or present. Caller owns resource lifetimes.
bool RecordDraw(rex::deko3d::Nucleo& recorder,const DrawPacket& draw);
enum class UploadedDrawResult {Recorded,Invalid,NeedsSubmission};
// Uploads the prepared CPU constants, raw fetch memory and indices, then calls
// RecordDraw. NeedsSubmission records nothing; submit/reuse a work slot first.
UploadedDrawResult RecordUploadedDraw(rex::deko3d::Nucleo& recorder,
    const DrawPacket& state,const DrawUploadSources& sources);
// Owns control sections for as long as their DkShader objects are used.
class ShaderLibrary {
 public:
  explicit ShaderLibrary(rex::deko3d::Nucleo& recorder):recorder_(recorder){}
  ShaderLibrary(const ShaderLibrary&)=delete;
  ShaderLibrary& operator=(const ShaderLibrary&)=delete;
  const DkShader* Load(std::span<const uint8_t> dksh,const char* name);
 private:
  struct Entry { std::vector<uint8_t> dksh; DkShader shader{}; };
  rex::deko3d::Nucleo& recorder_;
  std::vector<std::unique_ptr<Entry>> entries_;
};
} // namespace green_lantern::deko
