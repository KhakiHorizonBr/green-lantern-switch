// SPDX-License-Identifier: GPL-3.0-or-later
// Included after Renderer::Impl is defined. This is a bounded color32 cache,
// not full Xenos EDRAM emulation. Transfer raw words, never filtered pixels.

bool Renderer::Impl::EdramColorSupported(const RenderTarget& rt) const {
  if (rt.key.is_depth || rt.key.msaa || rt.samples != VK_SAMPLE_COUNT_1_BIT ||
      !rt.key.pitch || rt.key.pitch % 80 || rt.width != rt.key.pitch || rt.height % 16)
    return false;
  return ((rt.key.format == uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8) ||
           rt.key.format == uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA)) &&
          rt.format == VK_FORMAT_R8G8B8A8_UNORM) ||
      (rt.key.format == uint32_t(xenos::ColorRenderTargetFormat::k_16_16) &&
       rt.format == VK_FORMAT_R16G16_SNORM);
}

void Renderer::Impl::ImportEdramColor(RenderTarget& rt) {
  if (!edram_color_buffer || in_pass || !recording ||
      uint32_t(regs->Get<reg::RB_SURFACE_INFO>().msaa_samples) != 0) return;
  if (!EdramColorSupported(rt)) {
    // Uniform zero has the same bit representation in every supported color
    // encoding, including 7e3 stored as RGBA16F. No general HDR/depth conversion
    // is implied. Clear only full tiles known to have been zeroed in EDRAM.
    if (rt.key.is_depth || rt.key.msaa || rt.samples != VK_SAMPLE_COUNT_1_BIT ||
        !rt.key.pitch || rt.key.pitch % 80 || rt.width != rt.key.pitch || rt.height % 16 ||
        IsColorFormat64bpp(xenos::ColorRenderTargetFormat(rt.key.format))) return;
    std::vector<VkClearRect> rectangles;
    uint32_t zero_tiles = 0;
    const uint32_t pitch = rt.key.pitch / 80;
    for (uint32_t row = 0; row < rt.height / 16; ++row) {
      for (uint32_t col = 0; col < pitch; ++col) {
        const uint32_t tile = rt.key.base_tiles + row * pitch + col;
        if (tile >= kEdramTiles) continue;
        const auto& state = edram_color_tiles[tile];
        if (!state.version || state.unknown_write || !state.known_zero ||
            rt.edram_seen[tile] == state.version) continue;
        if (!rectangles.empty() && rectangles.back().rect.offset.y == int32_t(row * 16) &&
            uint32_t(rectangles.back().rect.offset.x) + rectangles.back().rect.extent.width == col * 80)
          rectangles.back().rect.extent.width += 80;
        else rectangles.push_back({{{int32_t(col * 80), int32_t(row * 16)}, {80, 16}}, 0, 1});
        ++zero_tiles;
        rt.edram_seen[tile] = state.version;
      }
    }
    if (rectangles.empty()) return;
    std::vector<VkClearRect> merged;
    for (const auto& rect : rectangles) {
      if (!merged.empty() && merged.back().rect.offset.x == rect.rect.offset.x &&
          merged.back().rect.extent.width == rect.rect.extent.width &&
          uint32_t(merged.back().rect.offset.y) + merged.back().rect.extent.height == uint32_t(rect.rect.offset.y))
        merged.back().rect.extent.height += rect.rect.extent.height;
      else merged.push_back(rect);
    }
    auto c = cmd();
    VkMemoryBarrier before = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
    VkRenderingAttachmentInfo attachment = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = rt.view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent = {rt.width, rt.height}; info.layerCount = 1;
    info.colorAttachmentCount = 1; info.pColorAttachments = &attachment;
    dfn().vkCmdBeginRendering(c, &info);
    VkClearAttachment clear = {}; clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    dfn().vkCmdClearAttachments(c, 1, &clear, uint32_t(merged.size()), merged.data());
    dfn().vkCmdEndRendering(c);
    VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &after, 0, nullptr, 0, nullptr);
    edram_tiles_imported += zero_tiles;
    if (Tracing()) REXLOG_INFO("[gl-zero-alias] applied {} zero tiles to color base={} format={} pitch={}",
        zero_tiles, rt.key.base_tiles, rt.key.format, rt.key.pitch);
    return;
  }
  const uint32_t pitch_tiles = rt.key.pitch / 80;
  std::vector<VkBufferImageCopy> copies;
  uint32_t first_tile = UINT32_MAX;
  for (uint32_t row = 0; row < rt.height / 16; ++row) {
    for (uint32_t col = 0; col < pitch_tiles; ++col) {
      const uint32_t tile = rt.key.base_tiles + row * pitch_tiles + col;
      if (tile >= kEdramTiles) continue; // Wrapping surfaces are not modeled.
      const auto& state = edram_color_tiles[tile];
      if (!state.version || state.unknown_write || rt.edram_seen[tile] == state.version) continue;
      VkBufferImageCopy copy = {};
      copy.bufferOffset = VkDeviceSize(tile) * kEdramTileBytes;
      copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy.imageOffset = {int32_t(col * 80), int32_t(row * 16), 0};
      copy.imageExtent = {80, 16, 1};
      copies.push_back(copy);
      rt.edram_seen[tile] = state.version;
      if (first_tile == UINT32_MAX) first_tile = tile;
    }
  }
  if (copies.empty()) return;
  VkCommandBuffer c = cmd();
  VkMemoryBarrier before = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
      VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 1, &before, 0, nullptr, 0, nullptr);
  dfn().vkCmdCopyBufferToImage(c, edram_color_buffer, rt.image, VK_IMAGE_LAYOUT_GENERAL,
      uint32_t(copies.size()), copies.data());
  VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
      VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      0, 1, &after, 0, nullptr, 0, nullptr);
  edram_tiles_imported += copies.size();
  const auto& source = edram_color_tiles[first_tile];
  const uint64_t log_key = Renderer::Mix(0xED01, source.producer_base) ^
      Renderer::Mix(0xED02, source.producer_pitch) ^ Renderer::Mix(0xED03, source.producer_format) ^
      Renderer::Mix(0xED04, rt.key.base_tiles) ^ Renderer::Mix(0xED05, rt.key.pitch) ^
      Renderer::Mix(0xED06, rt.key.format);
  if (edram_logged_imports.size() < 32 && edram_logged_imports.insert(log_key).second) {
    REXLOG_INFO("[gl-edram] import {} tiles: src base={} pitch={} format={} -> dst base={} pitch={} format={} frame={}",
        copies.size(), source.producer_base, source.producer_pitch, source.producer_format,
        rt.key.base_tiles, rt.key.pitch, rt.key.format, frame);
  }
}

void Renderer::Impl::MarkEdramWrite(RenderTarget& rt, VkRect2D rect, uint32_t guest_msaa,
                                   bool full_overwrite, bool zero_word_clear) {
  if (!edram_color_buffer || !rect.extent.width || !rect.extent.height ||
      rect.offset.x < 0 || rect.offset.y < 0) return;
  for (auto& write : edram_pending) {
    if (write.target && write.target != &rt) continue;
    if (!write.target) {
      write = {&rt, rect, guest_msaa, full_overwrite, zero_word_clear};
    } else {
      const uint32_t x = std::min(uint32_t(write.rect.offset.x), uint32_t(rect.offset.x));
      const uint32_t y = std::min(uint32_t(write.rect.offset.y), uint32_t(rect.offset.y));
      const uint32_t right = std::max(uint32_t(write.rect.offset.x) + write.rect.extent.width,
                                    uint32_t(rect.offset.x) + rect.extent.width);
      const uint32_t bottom = std::max(uint32_t(write.rect.offset.y) + write.rect.extent.height,
                                     uint32_t(rect.offset.y) + rect.extent.height);
      // A union can contain unwritten gaps. Only identical known clears retain
      // the ability to reinitialize a tile invalidated by an unsupported writer.
      write.full_overwrite &= full_overwrite &&
          std::memcmp(&write.rect, &rect, sizeof(rect)) == 0;
      write.zero_word_clear &= zero_word_clear && std::memcmp(&write.rect, &rect, sizeof(rect)) == 0;
      write.rect = {{int32_t(x), int32_t(y)}, {right - x, bottom - y}};
    }
    return;
  }
  // A pass has at most four color targets and one depth target.
  REXLOG_ERROR("[gl-edram] pending attachment overflow; invalidating color cache");
  for (auto& tile : edram_color_tiles) { tile.version = 0; tile.unknown_write = true; }
}

void Renderer::Impl::PublishEdramZeroClear(const EdramPendingWrite& write) {
  const auto& rt = *write.target;
  if (!rt.key.is_depth || rt.key.msaa || rt.samples != VK_SAMPLE_COUNT_1_BIT ||
      !rt.key.pitch || rt.key.pitch % 80) return;
  if (!edram_fill_buffer) edram_fill_buffer = reinterpret_cast<PFN_vkCmdFillBuffer>(
      device->vulkan_instance()->functions().vkGetDeviceProcAddr(vk, "vkCmdFillBuffer"));
  if (!edram_fill_buffer) return;
  const uint32_t x0 = std::min(uint32_t(write.rect.offset.x), rt.width);
  const uint32_t y0 = std::min(uint32_t(write.rect.offset.y), rt.height);
  const uint32_t x1 = std::min(uint32_t(write.rect.offset.x) + write.rect.extent.width, rt.width);
  const uint32_t y1 = std::min(uint32_t(write.rect.offset.y) + write.rect.extent.height, rt.height);
  const uint32_t pitch = rt.key.pitch / 80;
  if (++edram_color_version == 0) {
    edram_color_tiles = {};
    for (auto& pair : render_targets) pair.second->edram_seen.fill(0);
    ++edram_color_version;
  }
  auto c = cmd();
  VkMemoryBarrier before = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT};
  dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
  uint32_t count = 0;
  uint32_t run_start = 0, run_end = 0;
  const auto flush = [&]() {
    if (run_end > run_start) edram_fill_buffer(c, edram_color_buffer,
        VkDeviceSize(run_start) * kEdramTileBytes,
        VkDeviceSize(run_end - run_start) * kEdramTileBytes, 0);
  };
  for (uint32_t row = (y0 + 15) / 16; row < y1 / 16; ++row) {
    for (uint32_t col = (x0 + 79) / 80; col < x1 / 80; ++col) {
      const uint32_t tile = (rt.key.base_tiles + row * pitch + col) % kEdramTiles;
      // Whole tiles only: the depth/color within-tile permutation is irrelevant
      // when every word is zero. Partial tiles retain unknown ownership.
      if (run_end > run_start && tile == run_end) ++run_end;
      else { flush(); run_start = tile; run_end = tile + 1; }
      edram_color_tiles[tile] = {edram_color_version, rt.key.base_tiles, rt.key.pitch,
          0x80000000u | rt.key.format, false, true};
      ++count;
    }
  }
  flush();
  edram_tiles_published += count;
  if (Tracing()) REXLOG_INFO("[gl-zero-alias] published {} zero tiles from depth base={} pitch={} guest_msaa={}",
      count, rt.key.base_tiles, rt.key.pitch, write.guest_msaa);
}

void Renderer::Impl::InvalidateEdramWrite(const EdramPendingWrite& write) {
  const auto& rt = *write.target;
  // Draw/clear rectangles and pitch are already expanded to guest sample
  // coordinates when HostMsaa returned 0. Expand only actual MSAA host images.
  const uint32_t sx = rt.key.msaa && write.guest_msaa >= 2 ? 2 : 1;
  const uint32_t sy = rt.key.msaa && write.guest_msaa >= 1 ? 2 : 1;
  const uint32_t bpp_tiles = !rt.key.is_depth &&
      IsColorFormat64bpp(xenos::ColorRenderTargetFormat(rt.key.format)) ? 2 : 1;
  const uint64_t pitch = ((uint64_t(rt.key.pitch) * sx + 79) / 80) * bpp_tiles;
  const uint32_t x0 = std::min(uint32_t(write.rect.offset.x), rt.width) * sx;
  const uint32_t y0 = std::min(uint32_t(write.rect.offset.y), rt.height) * sy;
  const uint32_t x1 = std::min(uint64_t(uint32_t(write.rect.offset.x)) + write.rect.extent.width,
                              uint64_t(rt.width)) * sx;
  const uint32_t y1 = std::min(uint64_t(uint32_t(write.rect.offset.y)) + write.rect.extent.height,
                              uint64_t(rt.height)) * sy;
  if (x0 >= x1 || y0 >= y1) return;
  // Preserve rows and columns outside the write. Xenos tile addressing wraps;
  // crossing the end does not by itself mean that every tile was touched.
  uint32_t invalidated = 0;
  for (uint32_t row = y0 / 16; row < (y1 + 15) / 16; ++row) {
    for (uint32_t col = (x0 / 80) * bpp_tiles;
         col < ((x1 + 79) / 80) * bpp_tiles; ++col) {
      auto& tile = edram_color_tiles[(rt.key.base_tiles + row * pitch + col) % kEdramTiles];
      invalidated += tile.version != 0;
      tile.version = 0;
      tile.unknown_write = true;
    }
  }
  if (Tracing() && invalidated) {
    REXLOG_INFO("[gl-edram] invalidate {} known tiles: depth={} base={} pitch={} host_msaa={} guest_msaa={} sample_rect={},{}/{}x{}",
        invalidated, rt.key.is_depth, rt.key.base_tiles, rt.key.pitch, rt.key.msaa,
        write.guest_msaa, x0, y0, x1 - x0, y1 - y0);
  }
}

void Renderer::Impl::FlushEdramWrites() {
  if (!edram_color_buffer) return;
  // Each pass is published once, rather than interrupting the recording worker
  // for each draw. Unsupported and depth writes invalidate AFTER color exports.
  for (const auto& write : edram_pending) {
    // A uniform, fully replacing clear expanded to a single-sample host image
    // initializes the same words even if the guest used MSAA to express it.
    // Arbitrary multisampled shading is still unsupported and invalidates.
    if (!write.target || !EdramColorSupported(*write.target) ||
        (write.guest_msaa && !write.full_overwrite)) continue;
    auto& rt = *write.target;
    const uint32_t pitch_tiles = rt.key.pitch / 80;
    const uint32_t x0 = std::min(uint32_t(write.rect.offset.x), rt.width);
    const uint32_t y0 = std::min(uint32_t(write.rect.offset.y), rt.height);
    const uint32_t x1 = std::min(uint32_t(write.rect.offset.x) + write.rect.extent.width, rt.width);
    const uint32_t y1 = std::min(uint32_t(write.rect.offset.y) + write.rect.extent.height, rt.height);
    std::vector<VkBufferImageCopy> copies;
    if (++edram_color_version == 0) {
      edram_color_tiles = {};
      for (auto& pair : render_targets) pair.second->edram_seen.fill(0);
      ++edram_color_version;
    }
    for (uint32_t row = y0 / 16; row < (y1 + 15) / 16; ++row) {
      for (uint32_t col = x0 / 80; col < (x1 + 79) / 80; ++col) {
        const uint32_t tile = rt.key.base_tiles + row * pitch_tiles + col;
        if (tile >= kEdramTiles) continue;
        auto& state = edram_color_tiles[tile];
        const bool cleared = write.full_overwrite && x0 <= col * 80 && x1 >= (col + 1) * 80 &&
            y0 <= row * 16 && y1 >= (row + 1) * 16;
        if (state.unknown_write && !cleared) continue;
        VkBufferImageCopy copy = {};
        copy.bufferOffset = VkDeviceSize(tile) * kEdramTileBytes;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {int32_t(col * 80), int32_t(row * 16), 0};
        copy.imageExtent = {80, 16, 1};
        copies.push_back(copy);
        state = {edram_color_version, rt.key.base_tiles, rt.key.pitch, rt.key.format, false};
        rt.edram_seen[tile] = state.version;
      }
    }
    if (copies.empty()) continue;
    VkCommandBuffer c = cmd();
    VkMemoryBarrier before = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 1, &before, 0, nullptr, 0, nullptr);
    dfn().vkCmdCopyImageToBuffer(c, rt.image, VK_IMAGE_LAYOUT_GENERAL, edram_color_buffer,
        uint32_t(copies.size()), copies.data());
    VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
        VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    dfn().vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 1, &after, 0, nullptr, 0, nullptr);
    edram_tiles_published += copies.size();
  }
  for (const auto& write : edram_pending) {
    if (write.target && (!EdramColorSupported(*write.target) ||
        (write.guest_msaa && !write.full_overwrite)))
      InvalidateEdramWrite(write);
  }
  for (const auto& write : edram_pending)
    if (write.target && write.zero_word_clear) PublishEdramZeroClear(write);
  edram_pending = {};
}
