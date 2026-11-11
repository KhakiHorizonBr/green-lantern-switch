// SPDX-License-Identifier: GPL-3.0-or-later
#include "deko_depth.h"
#include <cstring>
namespace green_lantern::deko {
bool DepthResolve::Initialize(std::span<const uint8_t> dksh) {
  if(shader_)return false;
  const auto* shader=shaders_.Load(dksh,"lanterna_depth_resolve");
  if(!shader || dkShaderGetStage(shader)!=DkStage_Compute)return false;
  shader_=shader;return true;
}
ImageTransfer DepthResolve::Record(uint64_t source,const DkImageRect& from,uint64_t depth,
    uint64_t packed_words,const DkImageRect& to,bool guest_float20e4) {
  auto* s=images_.Find(source);auto* d=images_.Find(depth);auto* w=images_.Find(packed_words);
  auto contains=[](const Image& image,const DkImageRect& rect) {
    return rect.width && rect.height && !rect.z && rect.depth==1 &&
        uint64_t(rect.x)+rect.width<=image.spec.width && uint64_t(rect.y)+rect.height<=image.spec.height;
  };
  if(!shader_ || !recorder_.TrabajoAbierto() || !s || !d || !w ||
      source==depth || source==packed_words || depth==packed_words ||
      !contains(*s,from) || !contains(*d,to) || !contains(*w,to) ||
      from.width!=to.width || from.height!=to.height)return ImageTransfer::Invalid;
  for(const auto* image:{s,d,w})
    if(image->spec.layers!=1 || image->spec.levels!=1 || image->spec.samples!=DkMsMode_1x)
      return ImageTransfer::NeedsConversion;
  if(!s->spec.depth || (s->spec.format!=DkImageFormat_ZF32_X24S8 && s->spec.format!=DkImageFormat_Z24S8) ||
      d->spec.format!=DkImageFormat_R32_Float || w->spec.format!=DkImageFormat_RGBA8_Unorm ||
      !d->spec.storage || !w->spec.storage)return ImageTransfer::NeedsConversion;
  // The final sampler slot is reserved by this conversion. Even texelFetch
  // receives a valid immutable combined sampler descriptor on deko3d.
  if(!recorder_.config().descriptores_sampler)return ImageTransfer::Invalid;
  const uint32_t sampler_slot=recorder_.config().descriptores_sampler-1;
  DkSampler sampler;dkSamplerDefaults(&sampler);
  sampler.minFilter=sampler.magFilter=DkFilter_Nearest;
  sampler.wrapMode[0]=sampler.wrapMode[1]=DkWrapMode_ClampToEdge;
  if(!images_.Sampler(sampler_slot,sampler))return ImageTransfer::Invalid;
  struct alignas(16) Params {int32_t sx,sy,dx,dy;uint32_t width,height,flags,pad;};
  static_assert(sizeof(Params)==32);
  const Params params{int32_t(from.x),int32_t(from.y),int32_t(to.x),int32_t(to.y),
      to.width,to.height,guest_float20e4?1u:0u,0};
  auto upload=recorder_.Reservar(sizeof(params),256);if(!upload)return ImageTransfer::Invalid;
  std::memcpy(upload.cpu,&params,sizeof(params));
  const DescriptorRequest requests[]={{source,DkDsSource_Depth,false},{source,DkDsSource_Stencil,false},
      {depth,DkDsSource_Depth,true},{packed_words,DkDsSource_Depth,true}};
  std::array<DkResHandle,4> handles;
  if(!images_.TransientViews(requests,handles))return ImageTransfer::Invalid;
  handles[0]|=dkMakeSamplerHandle(sampler_slot);handles[1]|=dkMakeSamplerHandle(sampler_slot);
  recorder_.InvalidarCodigoSiHaceFalta();recorder_.BindDescriptores();auto cmd=recorder_.comandos();
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors);
  const DkShader* shaders[]={shader_};dkCmdBufBindShaders(cmd,DkStageFlag_Compute,shaders,1);
  dkCmdBufBindUniformBuffer(cmd,DkStage_Compute,0,upload.gpu,sizeof(params));
  dkCmdBufBindTextures(cmd,DkStage_Compute,0,handles.data(),2);
  dkCmdBufBindImages(cmd,DkStage_Compute,0,handles.data()+2,2);
  dkCmdBufDispatchCompute(cmd,(to.width+7)/8,(to.height+7)/8,1);
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
  ++d->version;++w->version;return ImageTransfer::Recorded;
}
} // namespace green_lantern::deko
