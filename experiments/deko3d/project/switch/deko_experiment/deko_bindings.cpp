#include "deko_bindings.h"
#include "system_upload_layout.h"
#include "attachment_plan.h"
#include <cstring>
#include <memory>

namespace green_lantern::deko {
static bool ValidateStage(DkStage stage,const StageBindings& b) {
  if((stage!=DkStage_Vertex && stage!=DkStage_Fragment && stage!=DkStage_Geometry) ||
      b.constant_mask & ~31u || b.storage_count>b.storage.size() ||
      b.texture_count>b.textures.size()) return false;
  for(uint32_t i=0;i<5;++i) if(b.constant_mask&(1u<<i)) {
    const auto& r=b.constants[i];
    if(!r.addr || (r.addr&255) || !r.size || r.size>65536 ||
        (i==0 && r.size!=kSystemDestinationBytes)) return false;
  }
  for(uint32_t i=0;i<b.storage_count;++i)
    if(!b.storage[i].addr || (b.storage[i].addr&15) || !b.storage[i].size) return false;
  return true;
}
bool BindStage(DkCmdBuf commands,DkStage stage,const StageBindings& b) {
  if(!commands || !ValidateStage(stage,b)) return false;
  // No implicit binding of VS constants into PS slot 1: preserve the stage's
  // reflected mask. This avoids inheriting RenderDk's unrelated shared UBO.
  for(uint32_t i=0;i<5;++i) if(b.constant_mask&(1u<<i))
    dkCmdBufBindUniformBuffers(commands,stage,i,&b.constants[i],1);
  if(b.storage_count) dkCmdBufBindStorageBuffers(commands,stage,0,b.storage.data(),b.storage_count);
  if(b.texture_count) dkCmdBufBindTextures(commands,stage,0,b.textures.data(),b.texture_count);
  return true;
}
bool RecordDraw(rex::deko3d::Nucleo& recorder,const DrawPacket& p) {
  if(!recorder.TrabajoAbierto() || !p.vertex || !dkShaderIsValid(p.vertex) ||
      dkShaderGetStage(p.vertex)!=DkStage_Vertex || p.color_count>4 ||
      !p.count || !p.scissor.width || !p.scissor.height ||
      !ValidateStage(DkStage_Vertex,p.vs) ||
      (p.fragment && (!dkShaderIsValid(p.fragment) ||
        dkShaderGetStage(p.fragment)!=DkStage_Fragment || !ValidateStage(DkStage_Fragment,p.ps))) ||
      (p.geometry && (!dkShaderIsValid(p.geometry) ||
        dkShaderGetStage(p.geometry)!=DkStage_Geometry || !ValidateStage(DkStage_Geometry,p.gs))) ||
      (p.indexed && (!p.indices || (p.index_format!=DkIdxFormat_Uint16 &&
        p.index_format!=DkIdxFormat_Uint32) ||
        p.indices%(p.index_format==DkIdxFormat_Uint16?2:4)))) return false;
  // Unlike Vulkan's null attachment slots, deko3d dereferences every view in
  // [0,color_count). PM4 may write RT1/RT2 while RT0 is disabled. Fill holes
  // with a valid view and explicitly disable their writes/blending.
  auto targets=p.colors;auto writes=p.writes;auto color=p.color;
  uint32_t holes=0;if(!PlanAttachments(targets,p.color_count,holes))return false;
  for(uint32_t i=0;i<p.color_count;++i)if(holes&(1u<<i)) {
    dkColorWriteStateSetMask(&writes,i,0);color.blendEnableMask&=~(1u<<i);
  }
  recorder.InvalidarCodigoSiHaceFalta();
  recorder.BindDescriptores();
  DkCmdBuf cmd=recorder.comandos();
  const DkShader* shaders[3]={p.vertex};uint32_t count=1;
  if(p.fragment)shaders[count++]=p.fragment;
  if(p.geometry)shaders[count++]=p.geometry;
  dkCmdBufBindShaders(cmd,DkStageFlag_GraphicsMask,shaders,count);
  BindStage(cmd,DkStage_Vertex,p.vs);
  if(p.fragment) BindStage(cmd,DkStage_Fragment,p.ps);
  if(p.geometry) BindStage(cmd,DkStage_Geometry,p.gs);
  dkCmdBufBindRenderTargets(cmd,targets.data(),p.color_count,p.depth);
  dkCmdBufBindRasterizerState(cmd,&p.raster);
  dkCmdBufBindMultisampleState(cmd,&p.samples);
  dkCmdBufBindColorState(cmd,&color);
  dkCmdBufBindColorWriteState(cmd,&writes);
  dkCmdBufBindBlendStates(cmd,0,p.blend.data(),p.color_count);
  dkCmdBufBindDepthStencilState(cmd,&p.depth_stencil);
  dkCmdBufSetViewports(cmd,0,&p.viewport,1);
  dkCmdBufSetViewportSwizzles(cmd,0,&p.swizzle,1);
  dkCmdBufSetScissors(cmd,0,&p.scissor,1);
  dkCmdBufSetBlendConst(cmd,p.blend_constant[0],p.blend_constant[1],p.blend_constant[2],p.blend_constant[3]);
  dkCmdBufSetDepthBias(cmd,p.depth_bias,p.depth_bias_clamp,p.depth_bias_slope);
  dkCmdBufSetStencil(cmd,DkFace_Front,p.front_stencil[0],p.front_stencil[1],p.front_stencil[2]);
  dkCmdBufSetStencil(cmd,DkFace_Back,p.back_stencil[0],p.back_stencil[1],p.back_stencil[2]);
  dkCmdBufSetPrimitiveRestart(cmd,p.restart,p.restart_index);
  if(p.indexed) {
    dkCmdBufBindIdxBuffer(cmd,p.index_format,p.indices);
    dkCmdBufDrawIndexed(cmd,p.primitive,p.count,1,p.first_index,p.vertex_offset,0);
  } else dkCmdBufDraw(cmd,p.primitive,p.count,1,p.first_vertex,0);
  return true;
}
UploadedDrawResult RecordUploadedDraw(rex::deko3d::Nucleo& recorder,
    const DrawPacket& state,const DrawUploadSources& sources) {
  if(!recorder.TrabajoAbierto())return UploadedDrawResult::Invalid;
  DrawUploadRequirements requirements;
  requirements.constant_masks={state.vs.constant_mask,state.fragment?state.ps.constant_mask:0u,
      state.geometry?state.gs.constant_mask:0u};
  requirements.storage_counts={state.vs.storage_count,state.fragment?state.ps.storage_count:0u,
      state.geometry?state.gs.storage_count:0u};
  requirements.count=state.count;requirements.first_index=state.first_index;requirements.indexed=state.indexed;
  if(state.indexed)requirements.index_bytes=state.index_format==DkIdxFormat_Uint16?2:
      state.index_format==DkIdxFormat_Uint32?4:0;
  DrawUploadPlan plan;if(!PlanDrawUploads(requirements,sources,plan))return UploadedDrawResult::Invalid;
  // Include the worst-case alignment of the first upload. No submission is
  // made in the middle of this draw, and the same resource cannot age out.
  if(plan.bytes && plan.bytes+255>recorder.SubidaLibre())return UploadedDrawResult::NeedsSubmission;
  DrawPacket draw=state;
  for(uint32_t n=0;n<plan.count;++n) {
    const auto& allocation=plan.allocations[n];
    auto upload=recorder.Reservar(allocation.destination_bytes,256);
    if(!upload)return UploadedDrawResult::Invalid;
    if(allocation.kind==UploadKind::Constant && !allocation.binding)
      PackSystem(sources.constants[0].data(),upload.cpu);
    else {
      const auto source=allocation.kind==UploadKind::Constant?sources.constants[allocation.binding]:
          allocation.kind==UploadKind::Storage?sources.storage[allocation.binding]:sources.indices;
      std::memset(upload.cpu,0,allocation.destination_bytes);
      std::memcpy(upload.cpu,source.data(),allocation.source_bytes);
    }
    const DkBufExtents range{upload.gpu,allocation.destination_bytes};
    StageBindings* stages[]={&draw.vs,&draw.ps,&draw.gs};
    for(uint32_t stage=0;stage<3;++stage) {
      if(allocation.kind==UploadKind::Constant && (requirements.constant_masks[stage]&(1u<<allocation.binding)))
        stages[stage]->constants[allocation.binding]=range;
      if(allocation.kind==UploadKind::Storage && allocation.binding<requirements.storage_counts[stage])
        stages[stage]->storage[allocation.binding]=range;
    }
    if(allocation.kind==UploadKind::Indices)draw.indices=upload.gpu;
  }
  return RecordDraw(recorder,draw)?UploadedDrawResult::Recorded:UploadedDrawResult::Invalid;
}
bool UploadSystem(rex::deko3d::Nucleo& recorder,const void* original,uint32_t bytes,DkBufExtents& out) {
  out={};
  if(!original || bytes<kSystemSourceBytes || !recorder.TrabajoAbierto()) return false;
  const auto upload=recorder.Reservar(kSystemDestinationBytes,256);
  if(!upload) return false;
  PackSystem(original,upload.cpu);
  out={upload.gpu,kSystemDestinationBytes};
  return true;
}
const DkShader* ShaderLibrary::Load(std::span<const uint8_t> dksh,const char* name) {
  if(dksh.size()<24 || dksh.size()>16u*1024*1024 || !name) return nullptr;
  uint32_t header[6];std::memcpy(header,dksh.data(),sizeof(header));
  if(header[0]!=0x48534B44 || header[1]<24 || header[2]<header[1] ||
      header[5]!=1 || uint64_t(header[2])+header[3]!=dksh.size() ||
      header[4]>=header[2] || !header[3]) return nullptr;
  auto entry=std::make_unique<Entry>();entry->dksh.assign(dksh.begin(),dksh.end());
  if(!recorder_.CargarShader(entry->shader,entry->dksh,name)) return nullptr;
  const DkShader* shader=&entry->shader;entries_.push_back(std::move(entry));return shader;
}
} // namespace green_lantern::deko
