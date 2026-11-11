// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "deko_images.h"
#include "../native_experiment/native/native_texture_cpu.h"
namespace green_lantern::deko {
// Upload a complete guest texture through the same CPU codec as Vulkan.
// Guest ranges must be resident and word-aligned. Resolves/EDRAM aliases are
// handled separately: never re-read stale guest bytes for a GPU-owned target.
Image* UploadGuestTexture(Images& images,
    const native::texture_cpu::DecodePlan& plan,
    std::span<const uint8_t> base,std::span<const uint8_t> mips,
    uint32_t fetch_swizzle,bool cube);
DkImageFormat TextureFormat(native::texture_cpu::StorageFormat format);
} // namespace green_lantern::deko
