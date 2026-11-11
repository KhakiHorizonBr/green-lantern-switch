from pathlib import Path
HERE=Path(__file__).resolve().parent
p=HERE/'native_experiment/native/native_renderer.cpp'
s=(HERE.parent/'perf-ablation/native_experiment/native/native_renderer.cpp').read_text(encoding='utf-8')
s=s.replace('#include "frame_diagnostics.h"','#include "frame_diagnostics.h"\n#include "scaled_fragcoord.h"')
s=s.replace('REXCVAR_DEFINE_UINT32(nx_ablation_mode,', '''REXCVAR_DEFINE_UINT32(nx_internal_scale_percent, 100, "Switch",
    "Main scene raster scale 50/75/100 percent; canonical guest surfaces remain full-sized");
REXCVAR_DEFINE_BOOL(nx_internal_scale_dump_shaders, false, "Graphics", "Dump scaled fragment SPIR-V variants on PC");
REXCVAR_DEFINE_UINT32(nx_ablation_mode,''')
s=s.replace('struct PipelineKey {','struct PipelineKey {\n  uint32_t internal_scale_percent;')
s=s.replace('  PassTargets pass;', '  PassTargets pass;') # member location differs; inject before pass_extent
s=s.replace('  VkExtent2D pass_extent = {};','''  uint32_t pass_scale_percent = 100;
  uint64_t scale_passes=0, scale_blits=0, scale_warnings=0;
  std::unordered_map<RenderTargetKey,std::unique_ptr<RenderTarget>,RenderTargetKeyHasher> scaled_targets;
  std::unordered_map<uint32_t,std::unordered_map<VulkanShader::VulkanTranslation*,VkShaderModule>> scaled_pixel_modules;
  VkExtent2D pass_extent = {};''')
s=s.replace('  PFN_vkCmdResolveImage vkCmdResolveImage = nullptr;', '  PFN_vkCmdResolveImage vkCmdResolveImage = nullptr;\n  PFN_vkCmdBlitImage vkCmdBlitImage = nullptr;')
s=s.replace('  vkCmdResolveImage =', '  vkCmdBlitImage = reinterpret_cast<PFN_vkCmdBlitImage>(get_proc(vk,"vkCmdBlitImage"));\n  vkCmdResolveImage =')
s=s.replace('    PassTargets targets;','    PassTargets targets;\n    uint32_t internal_scale_percent = 100;')
s=s.replace('  void EndPass() {','''  RenderTarget* GetScaledTarget(RenderTarget& source);
  uint32_t PrepareInternalScale(const PassTargets& targets);
  void TransferScaledPass(bool down);
  VkShaderModule ScaledPixelShader(VulkanShader::VulkanTranslation* shader,uint32_t percent);
  void EndPass() {''')
s=s.replace('      in_pass = false;\n      FlushEdramWrites();','      in_pass = false;\n      TransferScaledPass(false);\n      FlushEdramWrites();')
s=s.replace('  bool BeginPass(const PassTargets& targets);','  bool BeginPass(const PassTargets& targets, uint32_t scale_percent=100);')
s=s.replace('VkPipeline Renderer::Impl::GetPipeline(', '#include "internal_scale.inl"\n\nVkPipeline Renderer::Impl::GetPipeline(',1)
s=s.replace('  job.pixel = ps ? ps->shader_module() : VK_NULL_HANDLE;', '''  job.pixel = ps ? (key.internal_scale_percent<100?
      ScaledPixelShader(ps,key.internal_scale_percent):ps->shader_module()) : VK_NULL_HANDLE;
  if (ps && job.pixel==VK_NULL_HANDLE) return VK_NULL_HANDLE;''')
s=s.replace('bool Renderer::Impl::BeginPass(const PassTargets& targets) {','bool Renderer::Impl::BeginPass(const PassTargets& targets, uint32_t scale_percent) {')
s=s.replace('    bool compatible = true;','    bool compatible = pass_scale_percent == scale_percent;')
anchor='  VkRenderingInfo info = {};\n  info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;\n  info.renderArea.extent = {width, height};'
assert s.count(anchor)==1
s=s.replace(anchor,'''  pass_extent = {width,height}; // guest-coordinate extent for tracking and resolves.
  pass_scale_percent = scale_percent;
  // Always refresh dynamic state after a scope transition: identical guest
  // values can map to different physical pixels in native and scaled scopes.
  bound_viewport.width = NAN;
  bound_scissor.extent.width = UINT32_MAX;
  if (scale_percent<100) {
    TransferScaledPass(true);
    for (uint32_t i=0;i<4;++i) if (targets.color[i])
      colors[i].imageView=scaled_targets.at(targets.color[i]->key)->view;
    if (targets.depth) depth.imageView=scaled_targets.at(targets.depth->key)->view;
    width=width*scale_percent/100; height=height*scale_percent/100;
    ++scale_passes;
  }
  VkRenderingInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  info.renderArea.extent = {width, height};''')
s=s.replace('  pass_extent = {width, height};\n  ++stats().render_passes;', '  ++stats().render_passes;')
# Prepare low attachments before pipeline creation; fallback is all-native.
anchor='  // Pipeline.\n  PipelineKey key = {};'
assert anchor in s
s=s.replace(anchor,'''  st.internal_scale_percent = PrepareInternalScale(targets);
  if (st.internal_scale_percent<100 && ps_translation &&
      !ScaledPixelShader(ps_translation,st.internal_scale_percent)) st.internal_scale_percent=100;
  // Pipeline.
  PipelineKey key = {};
  key.internal_scale_percent = st.internal_scale_percent;''')
s=s.replace('if (!BeginPass(targets)) return skip(14);','if (!BeginPass(targets,st.internal_scale_percent)) return skip(14);')
s=s.replace('  SubmitDraw(d);\n  // Independent', '''  if (st.internal_scale_percent<100) {
    const float scale=float(st.internal_scale_percent)/100.0f;
    if (d.flags & kDrawViewport) { d.viewport.x*=scale; d.viewport.y*=scale;
      d.viewport.width*=scale; d.viewport.height*=scale; }
    if (d.flags & kDrawScissor) {
      // Scale both edges before deriving size, retaining coverage at odd edges.
      const uint32_t right=(uint32_t(d.scissor.offset.x)+d.scissor.extent.width)*st.internal_scale_percent/100;
      const uint32_t bottom=(uint32_t(d.scissor.offset.y)+d.scissor.extent.height)*st.internal_scale_percent/100;
      d.scissor.offset.x=d.scissor.offset.x*int32_t(st.internal_scale_percent)/100;
      d.scissor.offset.y=d.scissor.offset.y*int32_t(st.internal_scale_percent)/100;
      d.scissor.extent.width=right-uint32_t(d.scissor.offset.x);
      d.scissor.extent.height=bottom-uint32_t(d.scissor.offset.y);
    }
  }
  SubmitDraw(d);
  // Independent''')
# Release private surfaces/modules only after GPU idle, alongside main targets.
s=s.replace('  render_targets.clear();','''  render_targets.clear();
  for (auto& [key,rt]:scaled_targets) {
    if (!rt) continue;
    if(rt->view) dfn().vkDestroyImageView(vk,rt->view,nullptr);
    if(rt->image) dfn().vkDestroyImage(vk,rt->image,nullptr);
    if(rt->memory) dfn().vkFreeMemory(vk,rt->memory,nullptr);
  }
  scaled_targets.clear();
  for(auto& [percent,modules]:scaled_pixel_modules) for(auto& [translation,module]:modules)
    if(module) dfn().vkDestroyShaderModule(vk,module,nullptr);
  scaled_pixel_modules.clear();''',1)
s=s.replace('void Renderer::Impl::Swap(rex::ui::Presenter* presenter, uint32_t frontbuffer, uint32_t width,\n                          uint32_t height) {','void Renderer::Impl::Swap(rex::ui::Presenter* presenter, uint32_t frontbuffer, uint32_t width,\n                          uint32_t height) {')
# Locate signature generically to insert bounded progress log.
i=s.index('void Renderer::Impl::Swap(');j=s.index('{',i)+1
s=s[:j]+'''\n  if (frame%300==0) REXLOG_INFO("[gl-internal-scale] percent={} cumulative scopes={} blits={} surfaces={}",
      REXCVAR_GET(nx_internal_scale_percent),scale_passes,scale_blits,scaled_targets.size());'''+s[j:]
p.write_text(s,encoding='utf-8')

p=HERE/'source/green_lantern_switch_main.cpp';s=(HERE.parent/'perf-ablation/source/green_lantern_switch_main.cpp').read_text(encoding='utf-8')
s=s.replace('unsigned ablation_mode = 0;','unsigned ablation_mode = 0;\n      unsigned internal_scale_percent = 100;')
s=s.replace('          if (std::sscanf(line, " ablation_mode', '''          if (std::sscanf(line, " internal_scale_percent = %u %c", &value, &trailing) == 1 &&
              (value==50 || value==75 || value==100)) internal_scale_percent=value;
          if (std::sscanf(line, " ablation_mode''')
s=s.replace('      const bool ablation_applied', '''      const bool scale_applied = rex::cvar::SetFlagByName("nx_internal_scale_percent",std::to_string(internal_scale_percent).c_str());
      REXLOG_INFO("[gl-internal-scale-v1] percent={} applied={}; main scene only; guest-facing images full-sized", internal_scale_percent,scale_applied);
      const bool ablation_applied''')
p.write_text(s,encoding='utf-8')
print('Prepared isolated main-pass scale prototype')
