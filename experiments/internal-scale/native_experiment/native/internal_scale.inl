// SPDX-License-Identifier: GPL-3.0-or-later
// Canonical guest-facing images stay full-sized. Only selected drawing scopes
// use smaller attachments; publish back before resolves or EDRAM operations.
RenderTarget* Renderer::Impl::GetScaledTarget(RenderTarget& source) {
  const uint32_t percent=REXCVAR_GET(nx_internal_scale_percent);
  if (!vkCmdBlitImage || percent>=100 || source.samples!=VK_SAMPLE_COUNT_1_BIT) return nullptr;
  const uint32_t w=std::max(1u,source.width*percent/100),h=std::max(1u,source.height*percent/100);
  auto cached=scaled_targets.find(source.key);
  if (cached!=scaled_targets.end() && cached->second && cached->second->width==w && cached->second->height==h)
    return cached->second.get();
  VkFormatProperties props{};
  device->vulkan_instance()->functions().vkGetPhysicalDeviceFormatProperties(
      device->physical_device(),source.format,&props);
  constexpr auto required=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT;
  if ((props.optimalTilingFeatures&required)!=required) {
    if (scale_warnings++<8) REXLOG_WARN("[gl-internal-scale] vk format {} lacks blit capabilities; keep scope native",uint32_t(source.format));
    return nullptr;
  }
  auto& stored=scaled_targets[source.key];
  if (stored && stored->width==w && stored->height==h) return stored.get();
  EndPass();
  auto rt=std::make_unique<RenderTarget>();
  rt->key=source.key; rt->width=w; rt->height=h; rt->format=source.format;
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType=VK_IMAGE_TYPE_2D;info.format=rt->format;info.extent={w,h,1};
  info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;
  info.tiling=VK_IMAGE_TILING_OPTIMAL;
  info.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|
      (source.key.is_depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  if (!rex::ui::vulkan::util::CreateDedicatedAllocationImage(device,info,
      rex::ui::vulkan::util::MemoryPurpose::kDeviceLocal,rt->image,rt->memory)) return nullptr;
  VkImageAspectFlags aspect=source.key.is_depth?
      VK_IMAGE_ASPECT_DEPTH_BIT|(HasStencil(source.format)?VK_IMAGE_ASPECT_STENCIL_BIT:0):VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view.image=rt->image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=rt->format;
  view.subresourceRange=rex::ui::vulkan::util::InitializeSubresourceRange(aspect);
  if (dfn().vkCreateImageView(vk,&view,nullptr,&rt->view)!=VK_SUCCESS) {
    dfn().vkDestroyImage(vk,rt->image,nullptr);dfn().vkFreeMemory(vk,rt->memory,nullptr);return nullptr;
  }
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.image=rt->image;
  barrier.subresourceRange=view.subresourceRange;
  dfn().vkCmdPipelineBarrier(cmd(),VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,0,nullptr,0,nullptr,1,&barrier);
  if (stored) retired_render_targets.push_back({frame,stored->image,stored->view,
      VK_NULL_HANDLE,VK_NULL_HANDLE,stored->memory});
  stored=std::move(rt);
  REXLOG_INFO("[gl-internal-scale] allocate guest {}x{} -> host {}x{} vk{} base={} depth={}",
      source.width,source.height,w,h,uint32_t(source.format),source.key.base_tiles,source.key.is_depth);
  return stored.get();
}

uint32_t Renderer::Impl::PrepareInternalScale(const PassTargets& targets) {
  const uint32_t percent=REXCVAR_GET(nx_internal_scale_percent);
  // All draws sharing main depth must use the same sample positions, including
  // the depth prepass. Scaling only the final material pass breaks EQUAL tests.
  const bool main_color=targets.color[0] && targets.color[0]->key.base_tiles==1328 &&
      targets.color[0]->key.pitch==1280 && targets.color[0]->height>=480;
  const bool main_depth=targets.depth && targets.depth->key.base_tiles==0 &&
      targets.depth->key.pitch==1280 && targets.depth->height>=480;
  if ((percent!=50 && percent!=75) || (!main_color && !main_depth)) return 100;
  for (auto* rt:targets.color) if (rt && !GetScaledTarget(*rt)) return 100;
  if (targets.depth && !GetScaledTarget(*targets.depth)) return 100;
  return percent;
}

void Renderer::Impl::TransferScaledPass(bool down) {
  if (pass_scale_percent==100) return;
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER,nullptr,
      VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
      VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};
  auto c=cmd();
  dfn().vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,1,&barrier,0,nullptr,0,nullptr);
  auto copy=[&](RenderTarget* canonical) {
    if (!canonical) return;
    auto it=scaled_targets.find(canonical->key);
    if (it==scaled_targets.end() || !it->second) return;
    auto& low=*it->second;
    for (uint32_t bit: {uint32_t(VK_IMAGE_ASPECT_COLOR_BIT),uint32_t(VK_IMAGE_ASPECT_DEPTH_BIT),uint32_t(VK_IMAGE_ASPECT_STENCIL_BIT)}) {
      if (canonical->key.is_depth?(bit==VK_IMAGE_ASPECT_COLOR_BIT ||
          (bit==VK_IMAGE_ASPECT_STENCIL_BIT && !HasStencil(canonical->format))):bit!=VK_IMAGE_ASPECT_COLOR_BIT) continue;
      VkImageBlit region{};
      region.srcSubresource=region.dstSubresource={bit,0,0,1};
      VkOffset3D full{int32_t(pass_extent.width),int32_t(pass_extent.height),1};
      VkOffset3D scaled_extent{int32_t(pass_extent.width*pass_scale_percent/100),
          int32_t(pass_extent.height*pass_scale_percent/100),1};
      region.srcOffsets[1]=down?full:scaled_extent;region.dstOffsets[1]=down?scaled_extent:full;
      vkCmdBlitImage(c,down?canonical->image:low.image,VK_IMAGE_LAYOUT_GENERAL,
          down?low.image:canonical->image,VK_IMAGE_LAYOUT_GENERAL,1,&region,VK_FILTER_NEAREST);
      ++scale_blits;
    }
  };
  for (auto* rt:pass.color) copy(rt);
  copy(pass.depth);
  std::swap(barrier.srcAccessMask,barrier.dstAccessMask);
  dfn().vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      0,1,&barrier,0,nullptr,0,nullptr);
}

VkShaderModule Renderer::Impl::ScaledPixelShader(VulkanShader::VulkanTranslation* shader,uint32_t percent) {
  auto& modules=scaled_pixel_modules[percent];
  auto found=modules.find(shader);if (found!=modules.end()) return found->second;
  std::vector<uint32_t> binary;uint32_t patched=0;
  if (!scaling::FixFragCoord(shader->translated_binary(),100.0f/float(percent),binary,patched)) {
    REXLOG_ERROR("[gl-internal-scale] unsupported FragCoord module; keep draw native");
    modules[shader]=VK_NULL_HANDLE;return VK_NULL_HANDLE;
  }
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize=binary.size()*4;info.pCode=binary.data();VkShaderModule module=VK_NULL_HANDLE;
  if (dfn().vkCreateShaderModule(vk,&info,nullptr,&module)!=VK_SUCCESS) module=VK_NULL_HANDLE;
  modules[shader]=module;
  REXLOG_INFO("[gl-internal-scale] fragment variant percent={} FragCoord_loads={} created={}",percent,patched,module!=VK_NULL_HANDLE);
#if defined(_WIN32)
  if (module && REXCVAR_GET(nx_internal_scale_dump_shaders)) {
    std::filesystem::create_directories("logs/scaled-shaders");
    const auto name=fmt::format("logs/scaled-shaders/{:016X}_{}_{}.spv",shader->shader().ucode_data_hash(),percent,modules.size());
    if (auto* file=std::fopen(name.c_str(),"wb")) {std::fwrite(binary.data(),4,binary.size(),file);std::fclose(file);}
  }
#endif
  return module;
}
