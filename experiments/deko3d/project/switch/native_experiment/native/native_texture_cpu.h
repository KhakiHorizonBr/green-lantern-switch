// SPDX-License-Identifier: GPL-3.0-or-later
// CPU texture decoder shared by Vulkan and deko3d. No graphics API types.
// The caller validates guest storage, dimensions and staging extents before
// constructing a job. Source pointers refer to word-aligned guest mappings.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/util.h>

namespace green_lantern::native::texture_cpu {
namespace xenos = rex::graphics::xenos;
namespace texture_util = rex::graphics::texture_util;
namespace texture_conversion = rex::graphics::texture_conversion;
// How guest texels become host texels.
enum class Conversion : uint8_t {
  kUnsupported,
  // Byte order swap only.
  kCopy,
  // 16-bit packed with red in the low bits: red and blue fields swapped into
  // the Vulkan packed layout.
  kSwapRB565,
  kSwapRB1555,
  // RGBA4 with red in the low bits into Vulkan B4G4R4A4 (blue in the high bits).
  kRGBA4ToBGRA4,
  // Unsigned 10:11:11 and 11:11:10 (red first) into RGBA16 UNORM.
  kR10G11B11ToRGBA16,
  kR11G11B10ToRGBA16,
  // 4x4 blocks decompressed into R8G8 texels.
  kCTX1,
  // 4x4 4-bit alpha blocks into BC2 blocks with black color.
  kDXT3A,
};

enum class StorageFormat {
  UNDEFINED,
  R8_UNORM,
  R8_SNORM,
  A1R5G5B5_UNORM_PACK16,
  R5G6B5_UNORM_PACK16,
  R8G8B8A8_UNORM,
  R8G8B8A8_SNORM,
  A2B10G10R10_UNORM_PACK32,
  R8G8_UNORM,
  R8G8_SNORM,
  B4G4R4A4_UNORM_PACK16,
  R16G16B16A16_UNORM,
  BC1_RGBA_UNORM_BLOCK,
  BC2_UNORM_BLOCK,
  BC3_UNORM_BLOCK,
  BC5_UNORM_BLOCK,
  BC5_SNORM_BLOCK,
  BC4_UNORM_BLOCK,
  BC4_SNORM_BLOCK,
  R16_UNORM,
  R16_SNORM,
  R16G16_UNORM,
  R16G16_SNORM,
  R16G16B16A16_SNORM,
  R16_SFLOAT,
  R16G16_SFLOAT,
  R16G16B16A16_SFLOAT,
  R32_SFLOAT,
  R32G32_SFLOAT,
  R32G32B32A32_SFLOAT,
};

struct HostFormat {
  StorageFormat unorm = StorageFormat::UNDEFINED;
  StorageFormat snorm = StorageFormat::UNDEFINED;
  Conversion conversion = Conversion::kUnsupported;
  // The host components for the guest components XYZW, in the xenos swizzle
  // encoding. From ReXGlue's VulkanTextureCache::kBestHostFormats.
  uint32_t swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
  // The host image is block-compressed with the same 4x4 blocks as the guest.
  bool compressed = false;
  // Host bytes per guest block for the uncompressed conversions that change
  // the size (0 = same as the guest).
  uint32_t host_bytes_per_texel = 0;
};

constexpr uint32_t kSwizzleRRRR = xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR;
constexpr uint32_t kSwizzleRGGG = xenos::XE_GPU_TEXTURE_SWIZZLE_RGGG;
constexpr uint32_t kSwizzleRGBB = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBB;
constexpr uint32_t kSwizzleRGBA = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;

inline HostFormat GetHostFormat(xenos::TextureFormat format) {
  using F = xenos::TextureFormat;
  HostFormat h;
  switch (format) {
    case F::k_8:
    case F::k_8_A:
    case F::k_8_B:
      h = {StorageFormat::R8_UNORM, StorageFormat::R8_SNORM, Conversion::kCopy, kSwizzleRRRR};
      break;
    case F::k_1_5_5_5:
      h = {StorageFormat::A1R5G5B5_UNORM_PACK16, StorageFormat::UNDEFINED, Conversion::kSwapRB1555,
           kSwizzleRGBA};
      break;
    case F::k_5_6_5:
      h = {StorageFormat::R5G6B5_UNORM_PACK16, StorageFormat::UNDEFINED, Conversion::kSwapRB565,
           kSwizzleRGBB};
      break;
    case F::k_8_8_8_8:
    case F::k_8_8_8_8_A:
    case F::k_8_8_8_8_AS_16_16_16_16:
      h = {StorageFormat::R8G8B8A8_UNORM, StorageFormat::R8G8B8A8_SNORM, Conversion::kCopy, kSwizzleRGBA};
      break;
    case F::k_2_10_10_10:
    case F::k_2_10_10_10_AS_16_16_16_16:
      h = {StorageFormat::A2B10G10R10_UNORM_PACK32, StorageFormat::UNDEFINED, Conversion::kCopy,
           kSwizzleRGBA};
      break;
    case F::k_8_8:
      h = {StorageFormat::R8G8_UNORM, StorageFormat::R8G8_SNORM, Conversion::kCopy, kSwizzleRGGG};
      break;
    case F::k_4_4_4_4:
      h = {StorageFormat::B4G4R4A4_UNORM_PACK16, StorageFormat::UNDEFINED, Conversion::kRGBA4ToBGRA4,
           kSwizzleRGBA};
      break;
    case F::k_10_11_11:
    case F::k_10_11_11_AS_16_16_16_16:
      h = {StorageFormat::R16G16B16A16_UNORM, StorageFormat::UNDEFINED, Conversion::kR10G11B11ToRGBA16,
           kSwizzleRGBB, false, 8};
      break;
    case F::k_11_11_10:
    case F::k_11_11_10_AS_16_16_16_16:
      h = {StorageFormat::R16G16B16A16_UNORM, StorageFormat::UNDEFINED, Conversion::kR11G11B10ToRGBA16,
           kSwizzleRGBB, false, 8};
      break;
    case F::k_DXT1:
    case F::k_DXT1_AS_16_16_16_16:
      h = {StorageFormat::BC1_RGBA_UNORM_BLOCK, StorageFormat::UNDEFINED, Conversion::kCopy, kSwizzleRGBA,
           true};
      break;
    case F::k_DXT2_3:
    case F::k_DXT2_3_AS_16_16_16_16:
      h = {StorageFormat::BC2_UNORM_BLOCK, StorageFormat::UNDEFINED, Conversion::kCopy, kSwizzleRGBA, true};
      break;
    case F::k_DXT4_5:
    case F::k_DXT4_5_AS_16_16_16_16:
      h = {StorageFormat::BC3_UNORM_BLOCK, StorageFormat::UNDEFINED, Conversion::kCopy, kSwizzleRGBA, true};
      break;
    case F::k_DXN:
      h = {StorageFormat::BC5_UNORM_BLOCK, StorageFormat::BC5_SNORM_BLOCK, Conversion::kCopy, kSwizzleRGGG,
           true};
      break;
    case F::k_DXT5A:
      h = {StorageFormat::BC4_UNORM_BLOCK, StorageFormat::BC4_SNORM_BLOCK, Conversion::kCopy, kSwizzleRRRR,
           true};
      break;
    case F::k_DXT3A:
      // The alpha of a BC2 block: all four components read it.
      h = {StorageFormat::BC2_UNORM_BLOCK, StorageFormat::UNDEFINED, Conversion::kDXT3A,
           XE_GPU_MAKE_TEXTURE_SWIZZLE(A, A, A, A), true};
      break;
    case F::k_CTX1:
      h = {StorageFormat::R8G8_UNORM, StorageFormat::UNDEFINED, Conversion::kCTX1, kSwizzleRGGG, false, 2};
      break;
    case F::k_16:
      h = {StorageFormat::R16_UNORM, StorageFormat::R16_SNORM, Conversion::kCopy, kSwizzleRRRR};
      break;
    case F::k_16_16:
      h = {StorageFormat::R16G16_UNORM, StorageFormat::R16G16_SNORM, Conversion::kCopy, kSwizzleRGGG};
      break;
    case F::k_16_16_16_16:
      h = {StorageFormat::R16G16B16A16_UNORM, StorageFormat::R16G16B16A16_SNORM, Conversion::kCopy,
           kSwizzleRGBA};
      break;
    case F::k_16_EXPAND:
    case F::k_16_FLOAT:
      h = {StorageFormat::R16_SFLOAT, StorageFormat::UNDEFINED, Conversion::kCopy, kSwizzleRRRR};
      break;
    case F::k_16_16_EXPAND:
    case F::k_16_16_FLOAT:
      h = {StorageFormat::R16G16_SFLOAT, StorageFormat::UNDEFINED, Conversion::kCopy, kSwizzleRGGG};
      break;
    case F::k_16_16_16_16_EXPAND:
    case F::k_16_16_16_16_FLOAT:
      h = {StorageFormat::R16G16B16A16_SFLOAT, StorageFormat::UNDEFINED, Conversion::kCopy, kSwizzleRGBA};
      break;
    case F::k_32_FLOAT:
      h = {StorageFormat::R32_SFLOAT, StorageFormat::UNDEFINED, Conversion::kCopy, kSwizzleRRRR};
      break;
    case F::k_32_32_FLOAT:
      h = {StorageFormat::R32G32_SFLOAT, StorageFormat::UNDEFINED, Conversion::kCopy, kSwizzleRGGG};
      break;
    case F::k_32_32_32_32_FLOAT:
      h = {StorageFormat::R32G32B32A32_SFLOAT, StorageFormat::UNDEFINED, Conversion::kCopy, kSwizzleRGBA};
      break;
    default:
      break;
  }
  return h;
}

// The byte at logical address a of swapped guest memory is at physical
// address a ^ SwapXor(endian): the swaps exchange bytes within aligned 16- or
// 32-bit words.
inline uint32_t SwapXor(xenos::Endian endian) {
  switch (endian) {
    case xenos::Endian::k8in16:
      return 1;
    case xenos::Endian::k8in32:
      return 3;
    case xenos::Endian::k16in32:
      return 2;
    default:
      return 0;
  }
}
inline void CopyPacked16(xenos::Endian endian, void* out, const uint8_t* in) {
  auto* bytes = static_cast<uint8_t*>(out);
  for (uint32_t b = 0; b < 2; ++b)
    bytes[b] = *reinterpret_cast<const uint8_t*>((reinterpret_cast<uintptr_t>(in) + b) ^ SwapXor(endian));
}

inline void ConvertBlock(Conversion conversion, xenos::Endian endian, uint8_t* out, const uint8_t* in,
                  uint32_t guest_bytes, uint32_t out_row_pitch) {
  switch (conversion) {
    case Conversion::kCopy:
      if (guest_bytes < 4 && endian != xenos::Endian::kNone) {
        // Smaller than the swap unit: CopySwapBlock swaps whole units and
        // copied nothing, leaving these texels as whatever the staging
        // memory held. Gather each byte from where the swap puts it; `in`
        // is at a guest_bytes-aligned offset from the 4 KB-aligned base.
        const uintptr_t x = SwapXor(endian);
        for (uint32_t i = 0; i < guest_bytes; ++i) {
          out[i] = *reinterpret_cast<const uint8_t*>((reinterpret_cast<uintptr_t>(in) + i) ^ x);
        }
      } else {
        texture_conversion::CopySwapBlock(endian, out, in, guest_bytes);
      }
      break;
    case Conversion::kSwapRB565: {
      uint16_t v;
      CopyPacked16(endian, &v, in);
      v = uint16_t(((v & 0x1F) << 11) | (v & 0x7E0) | (v >> 11));
      std::memcpy(out, &v, 2);
    } break;
    case Conversion::kSwapRB1555: {
      uint16_t v;
      CopyPacked16(endian, &v, in);
      v = uint16_t((v & 0x83E0) | ((v & 0x1F) << 10) | ((v >> 10) & 0x1F));
      std::memcpy(out, &v, 2);
    } break;
    case Conversion::kRGBA4ToBGRA4: {
      uint16_t v;
      CopyPacked16(endian, &v, in);
      uint32_t r = v & 0xF, g = (v >> 4) & 0xF, b = (v >> 8) & 0xF, a = v >> 12;
      v = uint16_t((b << 12) | (g << 8) | (r << 4) | a);
      std::memcpy(out, &v, 2);
    } break;
    case Conversion::kR10G11B11ToRGBA16:
    case Conversion::kR11G11B10ToRGBA16: {
      uint32_t v;
      texture_conversion::CopySwapBlock(endian, &v, in, 4);
      uint32_t r, g, b;
      uint16_t o[4];
      if (conversion == Conversion::kR10G11B11ToRGBA16) {
        r = v & 0x3FF;
        g = (v >> 10) & 0x7FF;
        b = v >> 21;
        o[0] = uint16_t(r * 65535 / 0x3FF);
        o[1] = uint16_t(g * 65535 / 0x7FF);
        o[2] = uint16_t(b * 65535 / 0x7FF);
      } else {
        r = v & 0x7FF;
        g = (v >> 11) & 0x7FF;
        b = v >> 22;
        o[0] = uint16_t(r * 65535 / 0x7FF);
        o[1] = uint16_t(g * 65535 / 0x7FF);
        o[2] = uint16_t(b * 65535 / 0x3FF);
      }
      o[3] = 0xFFFF;
      std::memcpy(out, o, 8);
    } break;
    case Conversion::kCTX1:
      texture_conversion::ConvertTexelCTX1ToR8G8(endian, out, in, out_row_pitch);
      break;
    case Conversion::kDXT3A:
      texture_conversion::ConvertTexelDXT3AToDXT3(endian, out, in, 16);
      break;
    default:
      break;
  }
}

// Host layout of one level: every layer tightly packed, rows in whole blocks.
struct LevelLayout {
  uint32_t width, height;       // texels
  uint32_t blocks_x, blocks_y;  // guest blocks
  uint32_t host_row_bytes;      // one row of host blocks or texels
  uint32_t host_rows;           // rows of host blocks or texels
  uint64_t offset;          // of layer 0
  uint64_t layer_bytes;
};

// Everything the untiling of one texture needs, so it can run on a worker.
struct UntileJob {
  LevelLayout levels[xenos::kTextureMaxMips];
  uint32_t level_count, layers, width, height;
  texture_util::TextureGuestLayout layout;
  const uint8_t* base_data;
  const uint8_t* mip_data;
  uint8_t* staging;
  bool tiled, host_blocks;
  xenos::Endian endianness;
  xenos::TextureFormat format;
  Conversion conversion;
  uint32_t block_width, block_height, guest_bytes_per_block, bpb_log2, host_texel_bytes;
};

inline void RunUntile(const UntileJob& j) {
    const auto* levels = j.levels;
    const uint32_t level_count = j.level_count, layers = j.layers, width = j.width,
                   height = j.height;
    const texture_util::TextureGuestLayout& layout = j.layout;
    const uint8_t* base_data = j.base_data;
    const uint8_t* mip_data = j.mip_data;
    uint8_t* staging = j.staging;
    const bool host_blocks = j.host_blocks;
    const xenos::TextureFormat format = j.format;
    const uint32_t block_width = j.block_width, block_height = j.block_height,
                   guest_bytes_per_block = j.guest_bytes_per_block, bpb_log2 = j.bpb_log2,
                   host_texel_bytes = j.host_texel_bytes;
    for (uint32_t level = 0; level < level_count; ++level) {
      const LevelLayout& l = levels[level];
      // Which storage holds this level, and where in it.
      const uint8_t* level_source;
      const texture_util::TextureGuestLayout::Level* guest_level;
      bool is_packed = layout.packed_level != UINT32_MAX && level >= layout.packed_level;
      if (level == 0) {
        level_source = base_data;
        guest_level = &layout.base;
      } else {
        uint32_t stored = is_packed ? layout.packed_level : level;
        level_source = mip_data ? mip_data + layout.mip_offsets_bytes[stored] : nullptr;
        guest_level = &layout.mips[stored];
      }
      uint32_t offset_x_blocks = 0, offset_y_blocks = 0, offset_z = 0;
      if (is_packed) {
        texture_util::GetPackedMipOffset(width, height, 1, format, level, offset_x_blocks,
                                         offset_y_blocks, offset_z);
      }
      uint32_t row_pitch_bytes = guest_level->row_pitch_bytes;
      uint32_t pitch_blocks = row_pitch_bytes >> bpb_log2;
      // Tiled memory keeps aligned runs of blocks contiguous: 16 bytes, or 8
      // blocks of one byte. A plain copy moves a whole run per tiled-offset
      // computation instead of one block, 4 times fewer for 32-bit texels,
      // which was most of the time untiling a texture that changes every
      // frame. A run is 8 or 16 bytes at an offset aligned to its size, so it
      // is made of whole swap units and swapping it is swapping the memory
      // it came from, whatever the size of a texel.
      const uint32_t run_blocks = bpb_log2 == 0 ? 8 : (16u >> std::min(bpb_log2, 4u));
      const bool run_copy =
          j.tiled && j.conversion == Conversion::kCopy && run_blocks > 1 &&
          guest_bytes_per_block == (1u << bpb_log2) &&
          (host_blocks || block_width * host_texel_bytes == guest_bytes_per_block);
      for (uint32_t layer = 0; layer < layers; ++layer) {
        uint8_t* out = staging + l.offset + l.layer_bytes * layer;
        if (!level_source) {
          std::memset(out, 0, size_t(l.layer_bytes));
          continue;
        }
        const uint8_t* src = level_source + size_t(guest_level->array_slice_stride_bytes) * layer;
        for (uint32_t by = 0; by < l.blocks_y; ++by) {
          uint32_t gy = by + offset_y_blocks;
          uint8_t* out_row = out + size_t(by) * (host_blocks ? l.host_row_bytes
                                                             : l.host_row_bytes * block_height);
          // Linear: the row is contiguous, one copy (and swap) does it. The
          // video planes of the cutscenes (1280x720 and two 640x360, 8-bit,
          // every frame) took 15 ms a frame one texel at a time.
          if (!j.tiled && j.conversion == Conversion::kCopy &&
              (host_blocks || block_width * host_texel_bytes == guest_bytes_per_block)) {
            const size_t row_bytes = size_t(l.blocks_x) * guest_bytes_per_block;
            const uint8_t* src_row = src + size_t(gy) * row_pitch_bytes +
                                     size_t(offset_x_blocks) * guest_bytes_per_block;
            const uintptr_t start = reinterpret_cast<uintptr_t>(src_row);
            if (j.endianness == xenos::Endian::kNone ||
                (!(start & 3) && !(row_bytes & 3))) {
              texture_conversion::CopySwapBlock(j.endianness, out_row, src_row, row_bytes);
              continue;
            }
          }
          for (uint32_t bx = 0; bx < l.blocks_x; ++bx) {
            uint32_t gx = bx + offset_x_blocks;
            if (run_copy && !(gx & (run_blocks - 1)) && bx + run_blocks <= l.blocks_x) {
              size_t run_offset = size_t(texture_util::GetTiledOffset2D(
                  int32_t(gx), int32_t(gy), pitch_blocks, bpb_log2));
              texture_conversion::CopySwapBlock(j.endianness,
                                                out_row + size_t(bx) * guest_bytes_per_block,
                                                src + run_offset,
                                                run_blocks * guest_bytes_per_block);
              bx += run_blocks - 1;
              continue;
            }
            size_t src_offset;
            if (j.tiled) {
              src_offset = size_t(texture_util::GetTiledOffset2D(int32_t(gx), int32_t(gy),
                                                                 pitch_blocks, bpb_log2));
            } else {
              src_offset = size_t(gy) * row_pitch_bytes + size_t(gx) * guest_bytes_per_block;
            }
            uint8_t* out_block;
            if (host_blocks) {
              out_block = out_row + size_t(bx) *
                  (j.conversion == Conversion::kDXT3A ? 16 : guest_bytes_per_block);
            } else {
              out_block = out_row + size_t(bx) * block_width * host_texel_bytes;
            }
            ConvertBlock(j.conversion, j.endianness, out_block, src + src_offset,
                         guest_bytes_per_block, l.host_row_bytes);
          }
        }
      }
    }
}

// Prepared once per fetch layout; storage ranges refer to the base and mip
// guest addresses separately. Missing base data is black, as in Vulkan.
struct DecodePlan {
  UntileJob job{};
  StorageFormat storage = StorageFormat::UNDEFINED;
  uint32_t swizzle = 0, base_page = 0, mip_page = 0;
  uint32_t base_bytes = 0, mip_bytes = 0;
  uint64_t staging_bytes = 0;
};
inline bool Prepare(const xenos::xe_gpu_texture_fetch_t& fetch, bool is_signed,
                    DecodePlan& result) {
  result = {};
  // 3D uses different addressing, not array layers. Do not silently decode it
  // with the 2D path. Cube faces use array slices and are supported here.
  if (fetch.dimension != xenos::DataDimension::k2DOrStacked &&
      fetch.dimension != xenos::DataDimension::kCube) return false;
  if (fetch.dimension == xenos::DataDimension::kCube &&
      (fetch.stacked || fetch.size_2d.stack_depth != 5)) return false;
  uint32_t w, h, d, base, mip, min_level, max_level;
  texture_util::GetSubresourcesFromFetchConstant(fetch, &w, &h, &d, &base, &mip,
                                                &min_level, &max_level);
  if (w >= 8192 || h >= 8192 || d >= 6 || (!base && !mip) ||
      max_level >= xenos::kTextureMaxMips) return false;
  const auto format = rex::graphics::GetBaseFormat(fetch.format);
  const auto host = GetHostFormat(format);
  const auto* info = rex::graphics::FormatInfo::Get(format);
  if (!info || host.conversion == Conversion::kUnsupported) return false;
  // Match Vulkan: when no signed storage exists the translator performs the
  // conversion; selecting a different texel representation here is incorrect.
  result.storage = is_signed && host.snorm != StorageFormat::UNDEFINED ? host.snorm : host.unorm;
  if (result.storage == StorageFormat::UNDEFINED) return false;
  result.swizzle = host.swizzle; result.base_page = base; result.mip_page = mip;
  auto& j = result.job;
  j.width = w + 1; j.height = h + 1; j.layers = d + 1; j.level_count = max_level + 1;
  j.layout = texture_util::GetGuestTextureLayout(fetch.dimension, fetch.pitch,
      j.width, j.height, j.layers, fetch.tiled, format, fetch.packed_mips, base != 0, max_level);
  result.base_bytes = base ? j.layout.base.level_data_extent_bytes : 0;
  result.mip_bytes = mip ? j.layout.mips_total_extent_bytes : 0;
  // Swapping a sub-word texel can gather bytes elsewhere in its final word.
  // Require those bytes in the supplied spans, including 1x1 mip tails.
  if (fetch.endianness != xenos::Endian::kNone) {
    result.base_bytes = (result.base_bytes + 3u) & ~3u;
    result.mip_bytes = (result.mip_bytes + 3u) & ~3u;
  }
  if ((uint64_t(base) << 12) + result.base_bytes > 0x20000000ull ||
      (uint64_t(mip) << 12) + result.mip_bytes > 0x20000000ull) return false;
  j.tiled = fetch.tiled; j.endianness = fetch.endianness; j.format = format;
  j.conversion = host.conversion; j.host_blocks = host.compressed;
  j.block_width = info->block_width; j.block_height = info->block_height;
  j.guest_bytes_per_block = info->bytes_per_block();
  j.bpb_log2 = rex::log2_floor(j.guest_bytes_per_block);
  j.host_texel_bytes = host.host_bytes_per_texel ? host.host_bytes_per_texel : j.guest_bytes_per_block;
  for (uint32_t level = 0; level < j.level_count; ++level) {
    auto& l = j.levels[level];
    l.width = std::max(j.width >> level, 1u); l.height = std::max(j.height >> level, 1u);
    l.blocks_x = (l.width + j.block_width - 1) / j.block_width;
    l.blocks_y = (l.height + j.block_height - 1) / j.block_height;
    l.host_row_bytes = j.host_blocks ? l.blocks_x *
        (j.conversion == Conversion::kDXT3A ? 16 : j.guest_bytes_per_block) :
        l.blocks_x * j.block_width * j.host_texel_bytes;
    l.host_rows = j.host_blocks ? l.blocks_y : l.blocks_y * j.block_height;
    l.layer_bytes = uint64_t(l.host_row_bytes) * l.host_rows;
    l.offset = result.staging_bytes;
    result.staging_bytes += (l.layer_bytes * j.layers + 15) & ~uint64_t(15);
  }
  return result.staging_bytes && result.staging_bytes <= (512ull << 20);
}
inline bool Decode(const DecodePlan& plan, std::span<const uint8_t> base,
                   std::span<const uint8_t> mips, std::span<uint8_t> output) {
  if (!plan.staging_bytes || output.size() < plan.staging_bytes ||
      base.size() < plan.base_bytes || mips.size() < plan.mip_bytes ||
      (plan.base_bytes && (reinterpret_cast<uintptr_t>(base.data()) & 3)) ||
      (plan.mip_bytes && (reinterpret_cast<uintptr_t>(mips.data()) & 3))) return false;
  auto job = plan.job;
  job.base_data = plan.base_bytes ? base.data() : nullptr;
  job.mip_data = plan.mip_bytes ? mips.data() : nullptr;
  job.staging = output.data();
  std::memset(output.data(), 0, size_t(plan.staging_bytes));
  RunUntile(job);
  return true;
}
} // namespace green_lantern::native::texture_cpu
