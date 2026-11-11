// SPDX-License-Identifier: GPL-3.0-or-later
#include "deko_textures.h"
#include <vector>
namespace green_lantern::deko {
DkImageFormat TextureFormat(native::texture_cpu::StorageFormat format) {
  using F=native::texture_cpu::StorageFormat;
  switch(format) {
    case F::R8_UNORM:return DkImageFormat_R8_Unorm;
    case F::R8_SNORM:return DkImageFormat_R8_Snorm;
    case F::R8G8_UNORM:return DkImageFormat_RG8_Unorm;
    case F::R8G8_SNORM:return DkImageFormat_RG8_Snorm;
    case F::R8G8B8A8_UNORM:return DkImageFormat_RGBA8_Unorm;
    case F::R8G8B8A8_SNORM:return DkImageFormat_RGBA8_Snorm;
    case F::R5G6B5_UNORM_PACK16:return DkImageFormat_RGB565_Unorm;
    case F::A2B10G10R10_UNORM_PACK32:return DkImageFormat_RGB10A2_Unorm;
    case F::R16_UNORM:return DkImageFormat_R16_Unorm;
    case F::R16_SNORM:return DkImageFormat_R16_Snorm;
    case F::R16G16_UNORM:return DkImageFormat_RG16_Unorm;
    case F::R16G16_SNORM:return DkImageFormat_RG16_Snorm;
    case F::R16G16B16A16_UNORM:return DkImageFormat_RGBA16_Unorm;
    case F::R16G16B16A16_SNORM:return DkImageFormat_RGBA16_Snorm;
    case F::R16_SFLOAT:return DkImageFormat_R16_Float;
    case F::R16G16_SFLOAT:return DkImageFormat_RG16_Float;
    case F::R16G16B16A16_SFLOAT:return DkImageFormat_RGBA16_Float;
    case F::R32_SFLOAT:return DkImageFormat_R32_Float;
    case F::R32G32_SFLOAT:return DkImageFormat_RG32_Float;
    case F::R32G32B32A32_SFLOAT:return DkImageFormat_RGBA32_Float;
    case F::BC1_RGBA_UNORM_BLOCK:return DkImageFormat_RGBA_BC1;
    case F::BC2_UNORM_BLOCK:return DkImageFormat_RGBA_BC2;
    case F::BC3_UNORM_BLOCK:return DkImageFormat_RGBA_BC3;
    case F::BC4_UNORM_BLOCK:return DkImageFormat_R_BC4_Unorm;
    case F::BC4_SNORM_BLOCK:return DkImageFormat_R_BC4_Snorm;
    case F::BC5_UNORM_BLOCK:return DkImageFormat_RG_BC5_Unorm;
    case F::BC5_SNORM_BLOCK:return DkImageFormat_RG_BC5_Snorm;
    // Packed 1555/4444 require verified bit-layout conversion, not a format
    // with a similar name. Refuse until that conversion is implemented.
    default:return DkImageFormat_None;
  }
}
Image* UploadGuestTexture(Images& images,const native::texture_cpu::DecodePlan& plan,
    std::span<const uint8_t> base,std::span<const uint8_t> mips,
    uint32_t fetch_swizzle,bool cube) {
  const auto format=TextureFormat(plan.storage);
  if(format==DkImageFormat_None || !plan.staging_bytes || plan.staging_bytes>(512ull<<20) ||
      (cube && plan.job.layers!=6))return nullptr;
  std::vector<uint8_t> linear(size_t(plan.staging_bytes));
  if(!native::texture_cpu::Decode(plan,base,mips,linear))return nullptr;
  ImageSpec spec;spec.guest_address=plan.base_page<<12;
  spec.width=plan.job.width;spec.height=plan.job.height;
  spec.format=format;spec.levels=plan.job.level_count;spec.layers=plan.job.layers;spec.cube=cube;
  spec.host_swizzle=plan.swizzle;
  auto* image=images.Create(spec);if(!image)return nullptr;
  for(uint32_t c=0;c<4;++c) {
    const uint32_t guest=(fetch_swizzle>>(c*3))&7;
    const uint32_t component=guest<4?(plan.swizzle>>(guest*3))&7:guest&5;
    image->view.swizzle[c]=component<4?DkImageSwizzle(DkImageSwizzle_Red+component):
        component==5?DkImageSwizzle_One:DkImageSwizzle_Zero;
  }
  for(uint32_t level=0;level<plan.job.level_count;++level) {
    const auto& l=plan.job.levels[level];
    for(uint32_t layer=0;layer<plan.job.layers;++layer) {
      const size_t offset=size_t(l.offset+l.layer_bytes*layer);
      if(images.UploadLevel(image->id,level,layer,
          std::span<const uint8_t>(linear).subspan(offset,size_t(l.layer_bytes)),l.host_row_bytes)!=ImageTransfer::Recorded) {
        images.Release(image->id);return nullptr;
      }
    }
  }
  return image;
}
} // namespace green_lantern::deko
