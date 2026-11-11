#include "deko_output.h"
#include "presentation_plan.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace green_lantern::deko {
bool Output::Initialize(rex::deko3d::Dispositivo& device,uint32_t width,uint32_t height,
    NWindow* window,std::span<const uint8_t> vertex,std::span<const uint8_t> fragment) {
  if(device_ || !width || !height || width>3840 || height>2160 || vertex.empty() || fragment.empty())return false;
  device_=&device;
  rex::deko3d::ConfigNucleo config;config.ranuras=3;config.mb_subida=1;config.mb_codigo=1;
  config.kb_comandos=512;config.descriptores_imagen=3;config.descriptores_sampler=1;
  recorder_.Crear(device,config);shaders_=std::make_unique<ShaderLibrary>(recorder_);
  vertex_=shaders_->Load(vertex,"lanterna_output_vs");
  fragment_=shaders_->Load(fragment,"lanterna_output_ps");
  if(!vertex_ || !fragment_ || dkShaderGetStage(vertex_)!=DkStage_Vertex ||
      dkShaderGetStage(fragment_)!=DkStage_Fragment){Shutdown();return false;}
  swapchain_.Crear(device,width,height,1,window);return swapchain_.creada();
}
bool Output::Present(const Image& source,std::span<const uint32_t,256> gamma,
    uint32_t visible_width,uint32_t visible_height,uint32_t guest_swizzle) {
  if(!device_ || !swapchain_.creada() || source.spec.depth ||
      source.spec.layers!=1 || source.spec.levels!=1 ||
      source.spec.samples!=DkMsMode_1x || !source.spec.width || !source.spec.height)return false;
  PresentationPlan plan;
  if(!PlanPresentation(source.spec.width,source.spec.height,visible_width,visible_height,
      swapchain_.ancho(),swapchain_.alto(),plan))return false;
  recorder_.EmpezarTrabajo(++work_);
  auto upload=recorder_.Reservar(4096,256);
  if(!upload){recorder_.TerminarTrabajo();return false;}
  const int index=swapchain_.Adquirir();
  for(uint32_t i=0;i<256;++i) {
    float values[4]={float(gamma[i]&1023)/1023.0f,float((gamma[i]>>10)&1023)/1023.0f,
        float((gamma[i]>>20)&1023)/1023.0f,1.0f};
    std::memcpy(upload.cpu+i*16,values,sizeof(values));
  }
  DkImageView target;dkImageViewDefaults(&target,&swapchain_.Imagen(index));
  auto cmd=recorder_.comandos();
  auto source_view=source.view;
  for(uint32_t c=0;c<4;++c) {
    const uint32_t guest=(guest_swizzle>>(3*c))&7;
    const uint32_t channel=guest<4?(source.spec.host_swizzle>>(3*guest))&7:guest&5;
    source_view.swizzle[c]=channel<4?DkImageSwizzle(DkImageSwizzle_Red+channel):
        channel==5?DkImageSwizzle_One:DkImageSwizzle_Zero;
  }
  DkImageDescriptor descriptor;dkImageDescriptorInitialize(&descriptor,&source_view,false,false);
  DkSampler sampler;dkSamplerDefaults(&sampler);sampler.minFilter=DkFilter_Linear;sampler.magFilter=DkFilter_Linear;
  sampler.wrapMode[0]=sampler.wrapMode[1]=DkWrapMode_ClampToEdge;
  DkSamplerDescriptor sd;dkSamplerDescriptorInitialize(&sd,&sampler);
  const uint32_t slot=uint32_t(work_%3);
  dkCmdBufPushData(cmd,recorder_.DescriptoresImagen()+slot*sizeof(descriptor),&descriptor,sizeof(descriptor));
  dkCmdBufPushData(cmd,recorder_.DescriptoresSampler(),&sd,sizeof(sd));
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors);
  const DkImageView* targets[]={&target};dkCmdBufBindRenderTargets(cmd,targets,1,nullptr);
  const DkScissor full{0,0,swapchain_.ancho(),swapchain_.alto()};dkCmdBufSetScissors(cmd,0,&full,1);
  dkCmdBufClearColorFloat(cmd,0,DkColorMask_RGBA,0,0,0,1);
  DrawPacket draw;draw.vertex=vertex_;draw.fragment=fragment_;draw.colors[0]=&target;draw.color_count=1;
  draw.count=4;draw.primitive=DkPrimitive_TriangleStrip;
  draw.viewport={0,0,plan.viewport_width,plan.viewport_height,0,1};
  draw.scissor={0,0,plan.width,plan.height};
  draw.swizzle.y=DkSwizzle_NegativeY;
  dkRasterizerStateDefaults(&draw.raster);draw.raster.cullMode=DkFace_None;
  dkMultisampleStateDefaults(&draw.samples);dkColorStateDefaults(&draw.color);
  dkColorWriteStateDefaults(&draw.writes);dkBlendStateDefaults(&draw.blend[0]);
  dkDepthStencilStateDefaults(&draw.depth_stencil);draw.depth_stencil.depthTestEnable=false;
  draw.depth_stencil.depthWriteEnable=false;draw.depth_stencil.stencilTestEnable=false;
  draw.ps.constant_mask=1;draw.ps.constants[0]={upload.gpu,4096};
  draw.ps.textures[0]=dkMakeTextureHandle(slot,0);draw.ps.texture_count=1;
  // The output shader has a different UBO0 (gamma), not XeSystemConstants.
  // Bind it explicitly rather than weakening the game ABI's size validation.
  recorder_.InvalidarCodigoSiHaceFalta();recorder_.BindDescriptores();
  const DkShader* pair[]={vertex_,fragment_};dkCmdBufBindShaders(cmd,DkStageFlag_GraphicsMask,pair,2);
  dkCmdBufBindUniformBuffer(cmd,DkStage_Fragment,0,upload.gpu,4096);
  dkCmdBufBindTextures(cmd,DkStage_Fragment,0,draw.ps.textures.data(),1);
  dkCmdBufBindRasterizerState(cmd,&draw.raster);dkCmdBufBindMultisampleState(cmd,&draw.samples);
  dkCmdBufBindColorState(cmd,&draw.color);dkCmdBufBindColorWriteState(cmd,&draw.writes);
  dkCmdBufBindDepthStencilState(cmd,&draw.depth_stencil);
  dkCmdBufSetViewports(cmd,0,&draw.viewport,1);dkCmdBufSetViewportSwizzles(cmd,0,&draw.swizzle,1);
  dkCmdBufSetScissors(cmd,0,&draw.scissor,1);dkCmdBufSetPrimitiveRestart(cmd,false,UINT32_MAX);
  dkCmdBufDraw(cmd,DkPrimitive_TriangleStrip,4,1,0,0);
  recorder_.TerminarTrabajo();swapchain_.Presentar(index);return true;
}
void Output::Shutdown() {
  if(!device_)return;
  device_->EsperarInactiva();swapchain_.Destruir();shaders_.reset();recorder_.Destruir();
  vertex_=fragment_=nullptr;device_=nullptr;work_=0;
}
}
