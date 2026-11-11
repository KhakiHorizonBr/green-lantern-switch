// SPDX-License-Identifier: GPL-3.0-or-later
// Native PM4 frontend for the experimental deko NRO. Shader resources and
// constants use the validated Lanterna ABI, never RenderDk's unrelated HLSL ABI.
#include "deko_game_renderer.h"
#include "deko_bindings.h"
#include "deko_images.h"
#include "deko_textures.h"
#include "deko_output.h"
#include "target_plan.h"
#include "../native_experiment/native/native_hash.h"
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/pipeline/shader/interpreter.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/system/xmemory.h>
#include <rex/string/buffer.h>
#include <rex/logging.h>
#include <rex/math.h>
#include <xxhash.h>
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <set>
#include <fmt/format.h>
extern "C" void RexDekoOperationContext(const char*);
namespace green_lantern::deko {
namespace xenos=rex::graphics::xenos;
namespace reg=rex::graphics::reg;
namespace draw_util=rex::graphics::draw_util;
using rex::graphics::Shader;using rex::graphics::SpirvShaderTranslator;
using rex::graphics::RegisterFile;
namespace {
constexpr const char* kRoot="/switch/green_lantern/deko_shaders/";
constexpr uint32_t kVertexBytes=160u<<20;
std::vector<uint8_t> ReadFile(const std::string& name) {
  std::ifstream f(std::string(kRoot)+name,std::ios::binary|std::ios::ate);
  if(!f)return {};const auto length=f.tellg();if(length<=0 || length>16*1024*1024)return {};
  std::vector<uint8_t> bytes(static_cast<size_t>(length));f.seekg(0);
  if(!f.read(reinterpret_cast<char*>(bytes.data()),length))return {};return bytes;
}
DkImageFormat ColorFormat(uint32_t f) {
  switch(f) {
    case 0:case 1:return DkImageFormat_RGBA8_Unorm;
    case 2:case 10:return DkImageFormat_RGB10A2_Unorm;
    case 3:case 7:case 12:return DkImageFormat_RGBA16_Float;
    case 4:return DkImageFormat_RG16_Snorm;
    case 5:return DkImageFormat_RGBA16_Snorm;
    case 6:return DkImageFormat_RG16_Float;
    case 14:return DkImageFormat_R32_Float;
    case 15:return DkImageFormat_RG32_Float;
    default:return DkImageFormat_None;
  }
}
bool WideColor(uint32_t f){return f==3 || f==5 || f==7 || f==12 || f==15;}
DkBlendFactor Factor(xenos::BlendFactor f) {
  switch(uint32_t(f)) {
    case 0:return DkBlendFactor_Zero;case 1:return DkBlendFactor_One;
    case 4:return DkBlendFactor_SrcColor;case 5:return DkBlendFactor_InvSrcColor;
    case 6:return DkBlendFactor_SrcAlpha;case 7:return DkBlendFactor_InvSrcAlpha;
    case 8:return DkBlendFactor_DstColor;case 9:return DkBlendFactor_InvDstColor;
    case 10:return DkBlendFactor_DstAlpha;case 11:return DkBlendFactor_InvDstAlpha;
    case 12:return DkBlendFactor_ConstColor;case 13:return DkBlendFactor_InvConstColor;
    case 14:return DkBlendFactor_ConstAlpha;case 15:return DkBlendFactor_InvConstAlpha;
    case 16:return DkBlendFactor_SrcAlphaSaturate;default:return DkBlendFactor_Zero;
  }
}
DkBlendOp BlendOp(xenos::BlendOp op) {
  static const DkBlendOp table[]={DkBlendOp_Add,DkBlendOp_Sub,DkBlendOp_Min,DkBlendOp_Max,DkBlendOp_RevSub};
  return uint32_t(op)<5?table[uint32_t(op)]:DkBlendOp_Add;
}
}
struct GameRenderer::Impl {
  rex::deko3d::Dispositivo device;rex::deko3d::Nucleo recorder;
  std::unique_ptr<Images> images;std::unique_ptr<ShaderLibrary> library;
  Output output;bool device_ready=false,ready=false,output_ready=false;
  rex::memory::Memory* memory=nullptr;RegisterFile* regs=nullptr;
  native::RendererStats stats{};
  DkMemBlock shared=nullptr,edram=nullptr;
  DkGpuAddr shared_gpu=0,edram_gpu=0;
  uint64_t work=0,frame=0,mutation=0,submitted_ops=0;uint32_t gamma[256]{};
  bool diagnostic_each_draw=false;
  uint32_t batch_ops=1;
  uint32_t wait_submissions=4,submissions_since_wait=0;
  std::string previous_operation="boot";
  std::set<std::tuple<char,uint64_t,uint64_t>> captured_missing;
  uint64_t queue_idle_wait_ns=0,report_ns=0,report_frame=0,report_draws=0,
      report_resolves=0,report_submits=0,report_wait=0;
  rex::string::StringBuffer disassembly;
  std::unordered_map<uint64_t,std::unique_ptr<Shader>> shaders;
  std::set<uint64_t> logged_shapes;
  Shader* active_vs=nullptr;Shader* active_ps=nullptr;
  struct TextureRef {uint32_t slot=0,fetch=0,sign=0,dimension=5;std::string filters;};
  struct Module {uint64_t hash=0,mod=0;std::string filename;uint32_t mask=0,sizes[5]{};
    std::vector<TextureRef> textures;const DkShader* shader=nullptr;};
  std::map<std::tuple<char,uint64_t,uint64_t>,Module> modules;
  std::unordered_map<std::string,const DkShader*> helpers;
  std::set<std::string> errors;
  struct Target {uint32_t base,pitch,format;bool depth;uint64_t id;uint64_t seen[2048]{};};
  std::map<std::tuple<uint32_t,uint32_t,uint32_t,bool>,std::unique_ptr<Target>> targets;
  struct Tile {Target* producer=nullptr;uint64_t version=0;bool zero=false;};
  std::array<Tile,2048> tiles{};
  std::unordered_map<uint32_t,uint64_t> resolves;
  struct Written {DkImageRect rect;uint32_t pitch;uint64_t frame;};
  std::unordered_map<uint32_t,Written> resolve_written;
  struct Sized {uint64_t id=0,version=~0ull;};
  std::map<std::tuple<uint64_t,uint32_t,uint32_t>,Sized> sized_resolves;
  uint64_t resolve_version=0;
  struct Texture {uint64_t id=0,hash=0,checked=~0ull,last_frame=0,uses=0;
    native::ChangeDetector base_detector,mip_detector;};
  std::unordered_map<uint64_t,Texture> textures;
  std::unordered_map<uint64_t,DkResHandle> descriptors;
  std::unordered_map<uint64_t,uint32_t> samplers;
  struct VertexRange {uint64_t hash=0,checked=~0ull,content=0,uses=0;uint32_t offset=0;
    native::ChangeDetector detector;};
  std::unordered_map<uint64_t,VertexRange> vertices;
  uint32_t vertex_head=0;
  uint64_t bytes_hashed=0;
  uint64_t texture_requests=0,texture_reuses=0,resolve_texture_lookups=0;
  SpirvShaderTranslator::SystemConstants system_constants{};
  void Error(const std::string& message) {
    if(errors.insert(message).second)REXLOG_ERROR("[deko-game] {}",message);
  }
  void Begin() {
    if(recorder.TrabajoAbierto())return;
    recorder.EmpezarTrabajo(++work);
    if(!images->BeginWork(work))rex::deko3d::Fatal("Lanterna work sequence failed");
    descriptors.clear();
  }
  void Submit() {
    if(recorder.TrabajoAbierto()) {
      if(diagnostic_each_draw && stats.submissions<12) {
        REXLOG_INFO("[deko-startup-v4] submit work={} draws={} resolves={}",work,stats.draws,stats.resolves);
        rex::FlushLogging();
      }
      recorder.TerminarTrabajo();++stats.submissions;
      // Keep submissions serialized during this first performance step, but
      // no longer stop the queue after every individual draw/resolve.
      if(++submissions_since_wait>=wait_submissions || diagnostic_each_draw) {
        const auto wait_start=rex::deko3d::AhoraNs();device.EsperarInactiva();
        queue_idle_wait_ns+=rex::deko3d::AhoraNs()-wait_start;submissions_since_wait=0;
      }
      submitted_ops=stats.draws+stats.resolves;
    }
  }
  // Limit the batch between complete operations. Never call inside preparation:
  // its descriptors/uploads must remain in one live work slot until recorded.
  void Checkpoint(const char* kind) {
    const auto current=fmt::format("{} frame={} draws={} resolves={} vs={:016X} ps={:016X}",kind,
        frame,stats.draws,stats.resolves,active_vs?active_vs->ucode_data_hash():0,active_ps?active_ps->ucode_data_hash():0);
    const auto pair=previous_operation+" -> "+current;RexDekoOperationContext(pair.c_str());previous_operation=current;
    // Default 1 reproduces 007's completion boundary, including resolves
    // that record only clears and don't increment stats.resolves. 008's
    // batching is experimental and must not be the default after hardware084.
    if(!diagnostic_each_draw && batch_ops>1 && stats.draws+stats.resolves-submitted_ops<batch_ops)return;
    if(diagnostic_each_draw) {
      REXLOG_INFO("[deko-check-v8] before {} work={} frame={} draws={} resolves={} vs={:016X} ps={:016X}",
        kind,work,frame,stats.draws,stats.resolves,
        active_vs?active_vs->ucode_data_hash():0,active_ps?active_ps->ucode_data_hash():0);
      rex::FlushLogging();
    }
    Submit();
    if(diagnostic_each_draw) {
      REXLOG_INFO("[deko-check-v8] GPU completed {} draws={} resolves={}",kind,stats.draws,stats.resolves);
      rex::FlushLogging();
    }
    Begin();
  }
  rex::deko3d::Subida Upload(std::span<const uint8_t> bytes) {
    Begin();if(bytes.size()+256>recorder.SubidaLibre()){Submit();Begin();}
    if(bytes.empty() || bytes.size()>UINT32_MAX)return {};
    auto result=recorder.Reservar(uint32_t(bytes.size()),256);
    if(result){std::memcpy(result.cpu,bytes.data(),bytes.size());stats.stream_bytes+=bytes.size();}
    return result;
  }
  const DkShader* Helper(const std::string& filename) {
    auto [it,inserted]=helpers.emplace(filename,nullptr);
    if(inserted) {auto bytes=ReadFile(filename);it->second=library->Load(bytes,filename.c_str());
      if(!it->second)Error("missing helper "+filename);}
    return it->second;
  }
  Module* FindModule(char stage,Shader* shader,uint64_t modification) {
    if(!shader)return nullptr;
    auto it=modules.find({stage,shader->ucode_data_hash(),modification});
    if(it==modules.end()) {
      const auto key=std::make_tuple(stage,shader->ucode_data_hash(),modification);
      if(captured_missing.size()<512 && captured_missing.insert(key).second) {
        const std::string folder="sdmc:/switch/green_lantern/deko_missing/";
        std::error_code ec;std::filesystem::create_directories(folder,ec);
        const auto stem=fmt::format("{}s_{:016X}_{:016X}",stage,shader->ucode_data_hash(),modification);
        std::ofstream code(folder+stem+".ucode",std::ios::binary);
        code.write(reinterpret_cast<const char*>(shader->ucode_dwords()),shader->ucode_dword_count()*4);
        code.close();
        if(code) {
          std::ofstream meta(folder+stem+".json");
          meta<<fmt::format("{{\"stage\":\"{}\",\"hash\":\"{:016X}\",\"modification\":\"{:016X}\",\"endian\":\"little\",\"words\":{},\"frame\":{}}}\n",
              stage,shader->ucode_data_hash(),modification,shader->ucode_dword_count(),frame);
          REXLOG_INFO("[deko-missing-v10] captured {} words={}",stem,shader->ucode_dword_count());
        } else Error("could not write missing shader "+stem);
      }
      Error(fmt::format("missing shader {} {:016X} mod {:016X}",stage,shader->ucode_data_hash(),modification));return nullptr;
    }
    auto& module=it->second;
    if(!module.shader) {auto bytes=ReadFile(module.filename);module.shader=library->Load(bytes,module.filename.c_str());
      if(module.shader)++stats.shaders_translated;else Error("invalid shader "+module.filename);}
    return module.shader?&module:nullptr;
  }
  Image* Get(Target* target){if(!target)return nullptr;images->MarkUsed(target->id);return images->Find(target->id);}
  bool ColorCache(const Target& t) {return !t.depth && t.pitch%80==0 && (t.format==0 || t.format==1 || t.format==4);}
  Target* TargetFor(uint32_t base,uint32_t pitch,uint32_t format,bool depth,uint32_t required_height) {
    if(!pitch || pitch>8192 || base>=2048)return nullptr;
    auto key=std::make_tuple(base,pitch,format,depth);auto it=targets.find(key);
    Target* old=it!=targets.end()?it->second.get():nullptr;
    Image* previous=old?Get(old):nullptr;
    const uint32_t height=TargetHeight(previous?previous->spec.height:0,required_height);
    if(previous && previous->spec.height>=height)return old;
    ImageSpec spec;spec.use=ImageUse::Target;spec.width=pitch;spec.depth=depth;spec.render_target=true;
    spec.format=depth?DkImageFormat_ZF32_X24S8:ColorFormat(format);
    spec.guest_float20e4=depth && format==uint32_t(xenos::DepthRenderTargetFormat::kD24FS8);
    spec.height=height;
    Begin();auto* image=images->Create(spec);if(!image){Error("target allocation failed");return nullptr;}
    const DkImageRect rect{0,0,0,spec.width,spec.height,1};
    if(depth)images->ClearDepth(image->id,rect,true,1,255,0);
    else {const float zero[4]={};images->ClearColor(image->id,rect,zero);}
    if(previous) {
      const auto cmd=recorder.comandos();const DkImageRect area{0,0,0,pitch,previous->spec.height,1};
      // Same-format raw copy, including packed host depth/stencil. No guest
      // depth conversion belongs in a host allocation resize.
      dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
      dkCmdBufCopyImage(cmd,&previous->view,&area,&image->view,&area,0);
      dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
      const uint64_t old_id=old->id;old->id=image->id;images->Release(old_id);
      REXLOG_INFO("[deko-target-v7] grew base={} pitch={} height={}",base,pitch,height);
      ++stats.render_targets_created;return old;
    }
    auto target=std::make_unique<Target>();target->base=base;target->pitch=pitch;target->format=format;
    target->depth=depth;target->id=image->id;auto* result=target.get();targets.emplace(key,std::move(target));
    ++stats.render_targets_created;return result;
  }
  void Import(Target& target) {
    if(target.depth)return;
    if(!ColorCache(target)) {
      const uint32_t block=WideColor(target.format)?40:80;
      if(target.pitch%block)return;
      auto* image=Get(&target);const uint32_t pitch=target.pitch/block;
      for(uint32_t y=0;y<image->spec.height/16;++y)for(uint32_t x=0;x<pitch;++x) {
        const uint32_t tile=target.base+y*pitch+x;if(tile>=2048)continue;
        if(tiles[tile].zero && tiles[tile].version!=target.seen[tile]) {
          const DkImageRect rectangle{x*block,y*16,0,block,16,1};const float zero[4]={};
          images->ClearColor(target.id,rectangle,zero);target.seen[tile]=tiles[tile].version;
        }
      }
      return;
    }
    auto* dest=Get(&target);const uint32_t pitch=target.pitch/80;
    auto cmd=recorder.comandos();bool changed=false;
    for(uint32_t y=0;y<dest->spec.height/16;++y)for(uint32_t x=0;x<pitch;) {
      const uint32_t tile=target.base+y*pitch+x;if(tile>=2048)break;
      auto& state=tiles[tile];if(!state.version || target.seen[tile]==state.version){++x;continue;}
      const auto compatible=[&](uint32_t t){return tiles[t].version && target.seen[t]!=tiles[t].version &&
          tiles[t].zero==state.zero && (state.zero || tiles[t].producer==state.producer);};
      uint32_t run=1;
      if(state.zero) {
        run=TileSpan(tile,pitch-x,0,0,compatible);
        const DkImageRect to{x*80,y*16,0,run*80,16,1};const float zero[4]={};images->ClearColor(target.id,to,zero);
      }
      else if(state.producer && state.producer!=&target) {
        auto* source=Get(state.producer);const uint32_t source_pitch=state.producer->pitch/80;
        const uint32_t index=tile-state.producer->base;
        if(!source || !source_pitch || tile<state.producer->base || index/source_pitch*16+16>source->spec.height){++x;continue;}
        run=TileSpan(tile,pitch-x,state.producer->base,source_pitch,compatible);
        const DkImageRect from{index%source_pitch*80,index/source_pitch*16,0,run*80,16,1};
        const DkImageRect to{x*80,y*16,0,run*80,16,1};
        // This buffer is scratch, not a persistent tile-major EDRAM backing.
        // A row run can use a wider linear pitch and copy back identically.
        const DkCopyBuf buffer{edram_gpu+uint64_t(tile)*5120,run*320,run*5120};
        dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
        dkCmdBufCopyImageToBuffer(cmd,&source->view,&from,&buffer,0);
        dkCmdBufBarrier(cmd,DkBarrier_Full,0);
        dkCmdBufCopyBufferToImage(cmd,&buffer,&dest->view,&to,0);changed=true;
      }
      for(uint32_t n=0;n<run;++n)target.seen[tile+n]=tiles[tile+n].version;
      x+=run;
    }
    if(changed){dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);++dest->version;}
  }
  void Publish(Target& target,const DkImageRect& rect,bool zero=false) {
    const uint32_t block=!target.depth && WideColor(target.format)?40:80;
    if(target.pitch%block)return;
    const uint32_t pitch=target.pitch/block;const bool supported=ColorCache(target);
    for(uint32_t y=rect.y/16;y<(rect.y+rect.height+15)/16;++y)
      for(uint32_t x=rect.x/block;x<std::min(pitch,(rect.x+rect.width+block-1)/block);++x) {
        const uint32_t tile=target.base+y*pitch+x;if(tile>=2048)continue;
        const bool full=rect.x<=x*block && rect.x+rect.width>=(x+1)*block &&
            rect.y<=y*16 && rect.y+rect.height>=(y+1)*16;
        tiles[tile]={supported?&target:nullptr,++mutation,zero&&full};target.seen[tile]=mutation;
      }
  }
  DkResHandle TextureHandle(const TextureRef& reference) {
    ++texture_requests;
    auto fetch=regs->GetTextureFetch(reference.fetch);uint32_t address=(fetch.base_address&0x1FFFF)<<12;
    Image* image=nullptr;
    if(auto it=resolves.find(address);it!=resolves.end()) {
      ++resolve_texture_lookups;
      image=images->Find(it->second);
      if(image && image->spec.format==DkImageFormat_R32_Float &&
          fetch.format!=xenos::TextureFormat::k_24_8 && fetch.format!=xenos::TextureFormat::k_24_8_FLOAT) {
        if(auto* words=images->PackedDepthWords(image->id))image=words;
      }
    }
    if(image && reference.dimension==DkImageType_2DArray) {
      const uint32_t width=fetch.size_2d.width+1,height=fetch.size_2d.height+1;
      if(width!=image->spec.width || height!=image->spec.height) {
        auto& sized=sized_resolves[{image->id,width,height}];
        auto* dest=images->Find(sized.id);
        if(!dest) {
          ImageSpec spec;spec.use=ImageUse::SizedResolve;spec.width=width;spec.height=height;spec.format=image->spec.format;
          spec.render_target=true;spec.host_swizzle=image->spec.host_swizzle;
          dest=images->Create(spec);if(!dest){Error("sized resolve allocation failed");return 0;}sized.id=dest->id;
        }
        if(sized.version!=resolve_version) {
          const float zero[4]={};images->ClearColor(dest->id,{0,0,0,width,height,1},zero);
          const uint32_t texel=fetch.format==xenos::TextureFormat::k_16 && image->spec.format==DkImageFormat_R16_Unorm?2:
              fetch.format==xenos::TextureFormat::k_8_8_8_8 && image->spec.format==DkImageFormat_RGBA8_Unorm?4:0;
          const uint32_t pitch=uint32_t(fetch.pitch)<<5;bool copied=false;
          if(fetch.tiled && texel && pitch>=width && height>image->spec.height) {
            struct Part {Image* source;uint32_t y;Written written;};std::vector<Part> parts;
            for(const auto& [base,id]:resolves) {
              auto* candidate=images->Find(id);auto wr=resolve_written.find(base);
              if(candidate && candidate->spec.format==DkImageFormat_R32_Float && image->spec.format==DkImageFormat_RGBA8_Unorm)
                if(auto* words=images->PackedDepthWords(candidate->id))candidate=words;
              if(!candidate || wr==resolve_written.end() || base<address || candidate->spec.format!=image->spec.format ||
                  (wr->second.pitch && wr->second.pitch!=pitch))continue;
              const uint64_t offset=uint64_t(base)-address,row_bytes=uint64_t(pitch)*texel;
              if(!row_bytes || offset%row_bytes)continue;const uint64_t y=offset/row_bytes;
              if(y>=height || y%32 || uint64_t(uint32_t(rex::graphics::texture_util::GetTiledOffset2D(
                  0,int32_t(y),pitch,texel==2?1:2)))!=offset)continue;
              parts.push_back({candidate,uint32_t(y),wr->second});
            }
            std::sort(parts.begin(),parts.end(),[](const Part& a,const Part& b){return a.written.frame<b.written.frame ||
                (a.written.frame==b.written.frame && a.source->spec.guest_address<b.source->spec.guest_address);});
            for(const auto& part:parts) {
              auto from=part.written.rect;if(from.x>=width || uint64_t(from.y)+part.y>=height)continue;
              from.width=std::min(from.width,width-from.x);from.height=std::min(from.height,height-from.y-part.y);
              auto to=from;to.y+=part.y;
              copied|=images->Copy(part.source->id,from,dest->id,to)==ImageTransfer::Recorded;
            }
          }
          if(!copied) {
            const DkImageRect rect{0,0,0,std::min(width,image->spec.width),std::min(height,image->spec.height),1};
            copied=images->Copy(image->id,rect,dest->id,rect)==ImageTransfer::Recorded;
          }
          if(!copied){Error("sized resolve copy failed");return 0;}sized.version=resolve_version;
        }
        image=dest;
      }
    }
    if(!image) {
      native::texture_cpu::DecodePlan plan;
      if(!native::texture_cpu::Prepare(fetch,reference.sign!=0,plan)) {
        Error(fmt::format("unsupported texture layout fetch={} dimension={} format={} base={:08X}",
            reference.fetch,uint32_t(fetch.dimension),uint32_t(fetch.format),address));return 0;
      }
      const uint64_t key=XXH3_64bits(&fetch,sizeof(fetch))^(uint64_t(reference.sign)<<63);
      auto& cached=textures[key];cached.last_frame=frame;
      bool reused=cached.id!=0;
      if(cached.checked!=frame || !cached.id) {
        const auto* base=plan.base_bytes?memory->TranslatePhysical<const uint8_t*>(plan.base_page<<12):nullptr;
        const auto* mip=plan.mip_bytes?memory->TranslatePhysical<const uint8_t*>(plan.mip_page<<12):nullptr;
        ++cached.uses;
        uint64_t hash=base?cached.base_detector.Check(base,plan.base_bytes,cached.uses,plan.base_page,bytes_hashed):0;
        if(mip)hash^=cached.mip_detector.Check(mip,plan.mip_bytes,cached.uses,plan.mip_page,bytes_hashed)*0x9E3779B97F4A7C15ull;
        if(!cached.id || cached.hash!=hash) {
          reused=false;
          // Full texture staging must fit one work slot. A separate submission
          // preserves earlier samples before this texture's replacement.
          Begin();if(plan.staging_bytes+4096>recorder.SubidaLibre()){Submit();Begin();}
          auto* fresh=UploadGuestTexture(*images,plan,{base,plan.base_bytes},{mip,plan.mip_bytes},fetch.swizzle,
              fetch.dimension==xenos::DataDimension::kCube);
          if(!fresh){Error("texture upload/budget failed");return 0;}
          if(cached.id)images->Release(cached.id);
          cached.id=fresh->id;cached.hash=hash;++stats.textures_created;++stats.textures_uploaded;
          stats.texture_bytes_uploaded+=plan.staging_bytes;
        }
        cached.checked=frame;
      }
      if(reused)++texture_reuses;
      image=images->Find(cached.id);
    }
    if(!image)return 0;
    images->MarkUsed(image->id);
    if(reference.dimension==DkImageType_Cubemap && !image->spec.cube){Error("resolved cubemap needs six faces");return 0;}
    const uint64_t key_words[]={image->id,image->version,uint64_t(XXH3_64bits(&fetch,sizeof(fetch))),
        uint64_t(reference.sign)|(uint64_t(reference.dimension)<<8)|XXH3_64bits(reference.filters.data(),reference.filters.size())};
    const uint64_t key=XXH3_64bits(key_words,sizeof(key_words));
    if(auto found=descriptors.find(key);found!=descriptors.end())return found->second;
    DkSampler sampler;dkSamplerDefaults(&sampler);
    auto filter=[&](uint32_t at,xenos::TextureFilter fallback) {
      const char c=at<reference.filters.size()?reference.filters[at]:'f';
      return c=='l'?xenos::TextureFilter::kLinear:c=='p'?xenos::TextureFilter::kPoint:c=='b'?xenos::TextureFilter::kBaseMap:fallback;
    };
    sampler.magFilter=filter(0,fetch.mag_filter)==xenos::TextureFilter::kLinear?DkFilter_Linear:DkFilter_Nearest;
    sampler.minFilter=filter(1,fetch.min_filter)==xenos::TextureFilter::kLinear?DkFilter_Linear:DkFilter_Nearest;
    auto mip_filter=filter(2,fetch.mip_filter);
    sampler.mipFilter=mip_filter==xenos::TextureFilter::kLinear?DkMipFilter_Linear:
        mip_filter==xenos::TextureFilter::kPoint?DkMipFilter_Nearest:DkMipFilter_None;
    auto wrap=[](xenos::ClampMode clamp) {
      switch(clamp){case xenos::ClampMode::kRepeat:return DkWrapMode_Repeat;
        case xenos::ClampMode::kMirroredRepeat:return DkWrapMode_MirroredRepeat;
        case xenos::ClampMode::kClampToBorder:return DkWrapMode_ClampToBorder;default:return DkWrapMode_ClampToEdge;}
    };
    sampler.wrapMode[0]=wrap(fetch.clamp_x);sampler.wrapMode[1]=wrap(fetch.clamp_y);sampler.wrapMode[2]=wrap(fetch.clamp_z);
    sampler.lodClampMin=float(fetch.mip_min_level);sampler.lodClampMax=float(fetch.mip_max_level);
    sampler.lodBias=float(fetch.lod_bias)/32.0f;sampler.maxAnisotropy=1;
    DkSamplerDescriptor descriptor;dkSamplerDescriptorInitialize(&descriptor,&sampler);
    const uint64_t sampler_hash=XXH3_64bits(&descriptor,sizeof(descriptor));
    auto [si,added]=samplers.emplace(sampler_hash,uint32_t(samplers.size()));
    if(si->second>=recorder.config().descriptores_sampler-1 || !images->Sampler(si->second,sampler))return 0;
    DkImageView view=image->view;view.type=DkImageType(reference.dimension);
    const auto host=native::texture_cpu::GetHostFormat(rex::graphics::GetBaseFormat(fetch.format));
    uint32_t swizzle=host.swizzle;
    if(resolves.contains(address))swizzle=image->spec.host_swizzle;
    if(reference.sign && image->spec.format==DkImageFormat_R16_Unorm)view.format=DkImageFormat_R16_Snorm;
    for(uint32_t c=0;c<4;++c) {
      const uint32_t g=(fetch.swizzle>>(3*c))&7,channel=g<4?(swizzle>>(3*g))&7:g&5;
      view.swizzle[c]=channel<4?DkImageSwizzle(DkImageSwizzle_Red+channel):channel==5?DkImageSwizzle_One:DkImageSwizzle_Zero;
    }
    const DescriptorRequest request{image->id,DkDsSource_Depth,false,&view};DkResHandle handle;
    if(!images->TransientViews({&request,1},{&handle,1})){Error("descriptor pool full");return 0;}
    handle|=dkMakeSamplerHandle(si->second);descriptors[key]=handle;return handle;
  }
  void UpdateSystemConstants(bool,const draw_util::ViewportInfo&,uint32_t,uint32_t,const Shader*,const Shader*);
  bool CopyColor(Image&,const DkImageRect&,Image&,const DkImageRect&,float);
  bool RestoreStencil(Image&,Image&,const DkImageRect&);
  void Draw(const uint32_t*,uint32_t);
  void Resolve();
};
bool GameRenderer::Impl::CopyColor(Image& source,const DkImageRect& from,Image& destination,const DkImageRect& to,float scale) {
  images->MarkUsed(source.id);images->MarkUsed(destination.id);
  if(source.spec.depth || destination.spec.depth || from.width!=to.width || from.height!=to.height)return false;
  auto* vs=Helper("output.vert.glsl.dksh");auto* ps=Helper("copy_color.frag.glsl.dksh");if(!vs || !ps)return false;
  Begin();if(recorder.SubidaLibre()<4096){Submit();Begin();}
  struct alignas(16) Params {int32_t sx,sy,dx,dy;uint32_t w,h;float scale;uint32_t pad;};
  const Params params{int32_t(from.x),int32_t(from.y),int32_t(to.x),int32_t(to.y),to.width,to.height,scale,0};
  auto uniform=Upload({reinterpret_cast<const uint8_t*>(&params),sizeof(params)});if(!uniform)return false;
  auto view=source.view;view.type=DkImageType_2D;view.layerOffset=0;view.layerCount=1;
  const DescriptorRequest request{source.id,DkDsSource_Depth,false,&view};DkResHandle handle;
  DkSampler sampler;dkSamplerDefaults(&sampler);sampler.wrapMode[0]=sampler.wrapMode[1]=DkWrapMode_ClampToEdge;
  const uint32_t slot=recorder.config().descriptores_sampler-1;
  if(!images->Sampler(slot,sampler) || !images->TransientViews({&request,1},{&handle,1}))return false;
  handle|=dkMakeSamplerHandle(slot);auto cmd=recorder.comandos();
  recorder.InvalidarCodigoSiHaceFalta();recorder.BindDescriptores();
  const DkShader* programs[]={vs,ps};dkCmdBufBindShaders(cmd,DkStageFlag_GraphicsMask,programs,2);
  dkCmdBufBindUniformBuffer(cmd,DkStage_Fragment,0,uniform.gpu,sizeof(params));dkCmdBufBindTextures(cmd,DkStage_Fragment,0,&handle,1);
  DkRasterizerState raster;dkRasterizerStateDefaults(&raster);raster.cullMode=DkFace_None;
  DkMultisampleState samples;dkMultisampleStateDefaults(&samples);
  DkColorState color;dkColorStateDefaults(&color);DkColorWriteState writes;dkColorWriteStateDefaults(&writes);
  DkDepthStencilState depth;dkDepthStencilStateDefaults(&depth);depth.depthTestEnable=false;depth.depthWriteEnable=false;depth.stencilTestEnable=false;
  const DkViewport viewport{float(to.x),float(to.y),float(to.width),float(to.height),0,1};
  const DkScissor scissor{to.x,to.y,to.width,to.height};
  const DkViewportSwizzle swizzle{DkSwizzle_PositiveX,DkSwizzle_NegativeY,DkSwizzle_PositiveZ,DkSwizzle_PositiveW};
  const DkImageView* target[]={&destination.view};
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
  dkCmdBufBindRenderTargets(cmd,target,1,nullptr);dkCmdBufBindRasterizerState(cmd,&raster);dkCmdBufBindMultisampleState(cmd,&samples);
  dkCmdBufBindColorState(cmd,&color);dkCmdBufBindColorWriteState(cmd,&writes);dkCmdBufBindDepthStencilState(cmd,&depth);
  dkCmdBufSetViewports(cmd,0,&viewport,1);dkCmdBufSetViewportSwizzles(cmd,0,&swizzle,1);dkCmdBufSetScissors(cmd,0,&scissor,1);
  dkCmdBufSetPrimitiveRestart(cmd,false,UINT32_MAX);dkCmdBufDraw(cmd,DkPrimitive_TriangleStrip,4,1,0,0);
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);++destination.version;return true;
}
bool GameRenderer::Impl::RestoreStencil(Image& source,Image& destination,const DkImageRect& rectangle) {
  auto* vs=Helper("output.vert.glsl.dksh");auto* ps=Helper("restore_stencil.frag.glsl.dksh");if(!vs || !ps)return false;
  Begin();if(recorder.SubidaLibre()<4096){Submit();Begin();}
  DkSampler sampler;dkSamplerDefaults(&sampler);sampler.wrapMode[0]=sampler.wrapMode[1]=DkWrapMode_ClampToEdge;
  const uint32_t slot=recorder.config().descriptores_sampler-1;
  auto view=source.view;view.type=DkImageType_2D;
  const DescriptorRequest request{source.id,DkDsSource_Depth,false,&view};DkResHandle handle;
  if(!images->Sampler(slot,sampler) || !images->TransientViews({&request,1},{&handle,1}))return false;
  handle|=dkMakeSamplerHandle(slot);images->ClearDepth(destination.id,rectangle,false,0,255,0);
  auto cmd=recorder.comandos();recorder.InvalidarCodigoSiHaceFalta();recorder.BindDescriptores();
  const DkShader* programs[]={vs,ps};dkCmdBufBindShaders(cmd,DkStageFlag_GraphicsMask,programs,2);
  dkCmdBufBindTextures(cmd,DkStage_Fragment,0,&handle,1);
  DkRasterizerState raster;dkRasterizerStateDefaults(&raster);raster.cullMode=DkFace_None;
  DkMultisampleState samples;dkMultisampleStateDefaults(&samples);DkColorState color;dkColorStateDefaults(&color);
  DkColorWriteState writes;dkColorWriteStateDefaults(&writes);
  DkDepthStencilState depth;dkDepthStencilStateDefaults(&depth);depth.depthTestEnable=false;depth.depthWriteEnable=false;depth.stencilTestEnable=true;
  depth.stencilFrontFailOp=depth.stencilBackFailOp=DkStencilOp_Keep;
  depth.stencilFrontDepthFailOp=depth.stencilBackDepthFailOp=DkStencilOp_Keep;
  depth.stencilFrontPassOp=depth.stencilBackPassOp=DkStencilOp_Replace;
  depth.stencilFrontCompareOp=depth.stencilBackCompareOp=DkCompareOp_Always;
  const DkViewport viewport{float(rectangle.x),float(rectangle.y),float(rectangle.width),float(rectangle.height),0,1};
  const DkScissor scissor{rectangle.x,rectangle.y,rectangle.width,rectangle.height};
  const DkViewportSwizzle swizzle{DkSwizzle_PositiveX,DkSwizzle_NegativeY,DkSwizzle_PositiveZ,DkSwizzle_PositiveW};
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
  dkCmdBufBindRenderTargets(cmd,nullptr,0,&destination.view);dkCmdBufBindRasterizerState(cmd,&raster);dkCmdBufBindMultisampleState(cmd,&samples);
  dkCmdBufBindColorState(cmd,&color);dkCmdBufBindColorWriteState(cmd,&writes);dkCmdBufBindDepthStencilState(cmd,&depth);
  dkCmdBufSetViewports(cmd,0,&viewport,1);dkCmdBufSetViewportSwizzles(cmd,0,&swizzle,1);dkCmdBufSetScissors(cmd,0,&scissor,1);
  dkCmdBufSetPrimitiveRestart(cmd,false,UINT32_MAX);
  for(uint32_t bit=1;bit<=128;bit<<=1) {
    struct alignas(16) Params {int32_t x,y;uint32_t w,h,bit,swap,pad[2];};
    const Params params{int32_t(rectangle.x),int32_t(rectangle.y),rectangle.width,rectangle.height,bit,
        source.spec.width%80==0 && rectangle.x%80==0 && rectangle.width%80==0?1u:0u,{0,0}};
    auto uniform=recorder.Reservar(sizeof(params),256);if(!uniform)return false;std::memcpy(uniform.cpu,&params,sizeof(params));
    dkCmdBufBindUniformBuffer(cmd,DkStage_Fragment,0,uniform.gpu,sizeof(params));
    dkCmdBufSetStencil(cmd,DkFace_FrontAndBack,bit,bit,255);
    dkCmdBufDraw(cmd,DkPrimitive_TriangleStrip,4,1,0,0);
  }
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);++destination.version;return true;
}
void GameRenderer::Impl::Draw(const uint32_t* immediate,uint32_t immediate_words) {
  Begin();const auto begin=rex::deko3d::AhoraNs();
  struct Timer {uint64_t& target;uint64_t start;~Timer(){target+=rex::deko3d::AhoraNs()-start;}} timer{stats.cpu_ns,begin};
  ++stats.draw_calls;
  if(diagnostic_each_draw && stats.draw_calls<=32) {
    REXLOG_INFO("[deko-startup-v4] draw={} vs={:016X} ps={:016X}",stats.draw_calls,
        active_vs?active_vs->ucode_data_hash():0,active_ps?active_ps->ucode_data_hash():0);
    rex::FlushLogging();
  }
  auto skip=[&](uint32_t reason){++stats.draws_skipped;++stats.skipped_by_reason[reason&15];};
  const RegisterFile& r=*regs;const auto init=r.Get<reg::VGT_DRAW_INITIATOR>();
  if(!init.num_indices)return;
  Shader* vs=active_vs;if(!vs){skip(1);return;}
  if(vs->memexport_eM_written()){Error("VS memory export unsupported");skip(2);return;}
  const bool polygon=draw_util::IsPrimitivePolygonal(r);
  if(!draw_util::IsRasterizationPotentiallyDone(r,polygon)){skip(3);return;}
  Shader* ps=r.Get<reg::RB_MODECONTROL>().edram_mode==xenos::EdramMode::kColorDepth?active_ps:nullptr;
  if(ps && !draw_util::IsPixelShaderNeededWithRasterization(*ps,r))ps=nullptr;
  if(ps && ps->memexport_eM_written()){Error("PS memory export unsupported");skip(2);return;}
  const auto surface=r.Get<reg::RB_SURFACE_INFO>();if(!surface.surface_pitch){skip(0);return;}
  auto depth_control=draw_util::GetNormalizedDepthControl(r);
  uint32_t param_pos=UINT32_MAX;
  const uint32_t interpolators=ps?vs->writes_interpolators()&ps->GetInterpolatorInputMask(
      r.Get<reg::SQ_PROGRAM_CNTL>(),r.Get<reg::SQ_CONTEXT_MISC>(),param_pos):0;
  SpirvShaderTranslator::Modification vm(0),pm(0);
  vm.vertex.dynamic_addressable_register_count=vs->GetDynamicAddressableRegisterCount(r.Get<reg::SQ_PROGRAM_CNTL>().vs_num_reg);
  vm.vertex.interpolator_mask=interpolators;vm.vertex.host_vertex_shader_type=Shader::HostVertexShaderType::kVertex;
  const auto clip=r.Get<reg::PA_CL_CLIP_CNTL>();
  vm.vertex.user_clip_plane_cull=clip.ucp_cull_only_ena;
  vm.vertex.user_clip_plane_count=clip.clip_disable?0:rex::bit_count(clip.ucp_ena);
  vm.vertex.point_ps_ucp_mode=clip.ps_ucp_mode;
  vm.vertex.vertex_kill_and=uint32_t((vs->writes_point_size_edge_flag_kill_vertex()&4) && !clip.vtx_kill_or);
  vm.vertex.output_point_parameters=uint32_t((vs->writes_point_size_edge_flag_kill_vertex()&1) && init.prim_type==xenos::PrimitiveType::kPointList);
  if(ps) {
    pm.pixel.dynamic_addressable_register_count=ps->GetDynamicAddressableRegisterCount(r.Get<reg::SQ_PROGRAM_CNTL>().ps_num_reg);
    pm.pixel.interpolator_mask=interpolators;
    pm.pixel.interpolators_centroid=interpolators&~xenos::GetInterpolatorSamplingPattern(surface.msaa_samples,
        r.Get<reg::SQ_CONTEXT_MISC>().sc_sample_cntl,r.Get<reg::SQ_INTERPOLATOR_CNTL>().sampling_pattern);
    if(param_pos<xenos::kMaxInterpolators){pm.pixel.param_gen_enable=1;pm.pixel.param_gen_interpolator=param_pos;
      pm.pixel.param_gen_point=init.prim_type==xenos::PrimitiveType::kPointList;}
    pm.pixel.depth_stencil_mode=ps->implicit_early_z_write_allowed() &&
        (!ps->writes_color_target(0) || !draw_util::DoesCoverageDependOnAlpha(r.Get<reg::RB_COLORCONTROL>()))?
        SpirvShaderTranslator::Modification::DepthStencilMode::kEarlyHint:
        SpirvShaderTranslator::Modification::DepthStencilMode::kNoModifiers;
  }
  Module* vertex=FindModule('v',vs,vm.value);Module* fragment=ps?FindModule('p',ps,pm.value):nullptr;
  if(!vertex || (ps&&!fragment)){skip(6);return;}
  DrawPacket packet;packet.vertex=vertex->shader;packet.fragment=fragment?fragment->shader:nullptr;
  std::string primitive_helper;
  switch(init.prim_type) {
    case xenos::PrimitiveType::kPointList:packet.primitive=DkPrimitive_Points;primitive_helper="point";break;
    case xenos::PrimitiveType::kRectangleList:packet.primitive=DkPrimitive_Triangles;primitive_helper="rect";break;
    case xenos::PrimitiveType::kQuadList:packet.primitive=DkPrimitive_LinesAdjacency;primitive_helper="quad";break;
    case xenos::PrimitiveType::kTriangleList:packet.primitive=DkPrimitive_Triangles;break;
    case xenos::PrimitiveType::kTriangleStrip:packet.primitive=DkPrimitive_TriangleStrip;break;
    case xenos::PrimitiveType::kTriangleFan:case xenos::PrimitiveType::kPolygon:packet.primitive=DkPrimitive_TriangleFan;break;
    case xenos::PrimitiveType::kLineList:packet.primitive=DkPrimitive_Lines;break;
    case xenos::PrimitiveType::kLineStrip:packet.primitive=DkPrimitive_LineStrip;break;
    case xenos::PrimitiveType::kLineLoop:packet.primitive=DkPrimitive_LineLoop;break;
    default:Error("unsupported primitive "+std::to_string(uint32_t(init.prim_type)));skip(4);return;
  }
  if(!primitive_helper.empty()) {
    if(vm.vertex.user_clip_plane_count || vm.vertex.vertex_kill_and){Error("primitive helper needs clip/cull variant");skip(5);return;}
    packet.geometry=Helper(fmt::format("gs_{}_{}_{}_{}.geom.dksh",primitive_helper,rex::bit_count(interpolators),
        uint32_t(vm.vertex.output_point_parameters),uint32_t(pm.pixel.param_gen_point)));
    if(!packet.geometry){skip(12);return;}
  }
  const uint32_t guest_msaa=uint32_t(surface.msaa_samples),sx=guest_msaa>=2?2:1,sy=guest_msaa>=1?2:1;
  draw_util::Scissor scissor;draw_util::GetScissor(r,scissor);
  scissor.offset[0]*=sx;scissor.offset[1]*=sy;scissor.extent[0]*=sx;scissor.extent[1]*=sy;
  draw_util::ViewportInfo viewport;
  draw_util::GetHostViewportInfo(r,sx,sy,false,16384,16384,true,depth_control,false,false,ps&&ps->writes_depth(),viewport);
  if(!viewport.xy_extent[0] || !viewport.xy_extent[1]){skip(3);return;}
  packet.viewport={float(viewport.xy_offset[0]),float(viewport.xy_offset[1]),float(viewport.xy_extent[0]),
      float(viewport.xy_extent[1]),viewport.z_min,viewport.z_max};
  packet.swizzle.y=DkSwizzle_NegativeY;
  packet.scissor={scissor.offset[0],scissor.offset[1],scissor.extent[0],scissor.extent[1]};
  const uint32_t color_mask=draw_util::GetNormalizedColorMask(r,ps?ps->writes_color_targets():0);
  uint32_t needed_height=scissor.offset[1]+scissor.extent[1];
  if(viewport.xy_extent[1])needed_height=std::min(needed_height,viewport.xy_offset[1]+viewport.xy_extent[1]);
  const auto draw_height=[&](uint32_t base,bool wide) {
    const uint32_t tile_pitch=(surface.surface_pitch*sx+79)/80*(wide?2:1);
    const uint32_t rows=(2048-std::min(base,2047u))/std::max(tile_pitch,1u)*16;
    return std::min(needed_height,rows);
  };
  Target* colors[4]{};Target* depth=nullptr;
  for(uint32_t i=0;i<4;++i)if((color_mask>>(4*i))&15) {
    const auto info=r.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    const uint32_t format=uint32_t(xenos::GetStorageColorFormat(info.color_format));
    colors[i]=TargetFor(info.color_base,surface.surface_pitch*sx,format,false,draw_height(info.color_base,WideColor(format)));
    if(!colors[i]){skip(7);return;}Import(*colors[i]);packet.colors[i]=&Get(colors[i])->view;packet.color_count=i+1;
  }
  if(depth_control.z_enable || depth_control.stencil_enable) {
    const auto info=r.Get<reg::RB_DEPTH_INFO>();depth=TargetFor(info.depth_base,surface.surface_pitch*sx,uint32_t(info.depth_format),true,draw_height(info.depth_base,false));
    if(!depth){skip(7);return;}packet.depth=&Get(depth)->view;
  }
  if(!packet.depth && !packet.color_count){skip(7);return;}
  uint32_t bound_width=8192,bound_height=8192;
  for(auto* target:colors)if(target){auto* im=Get(target);bound_width=std::min(bound_width,im->spec.width);bound_height=std::min(bound_height,im->spec.height);}
  if(depth){bound_width=std::min(bound_width,Get(depth)->spec.width);bound_height=std::min(bound_height,Get(depth)->spec.height);}
  packet.scissor.width=std::min(packet.scissor.width,bound_width-std::min(packet.scissor.x,bound_width));
  packet.scissor.height=std::min(packet.scissor.height,bound_height-std::min(packet.scissor.y,bound_height));
  if(!packet.scissor.width || !packet.scissor.height){skip(3);return;}
  const uint32_t shape_data[]={uint32_t(vs->ucode_data_hash()),uint32_t(ps?ps->ucode_data_hash():0),
      viewport.xy_offset[0],viewport.xy_offset[1],viewport.xy_extent[0],viewport.xy_extent[1],
      packet.scissor.x,packet.scissor.y,packet.scissor.width,packet.scissor.height};
  if(logged_shapes.size()<32 && logged_shapes.insert(XXH3_64bits(shape_data,sizeof(shape_data))).second)
    REXLOG_INFO("[deko-shape-v6] vs={:016X} ps={:016X} viewport={},{} {}x{} scissor={},{} {}x{} target={}x{} msaa={}",
        vs->ucode_data_hash(),ps?ps->ucode_data_hash():0,viewport.xy_offset[0],viewport.xy_offset[1],
        viewport.xy_extent[0],viewport.xy_extent[1],packet.scissor.x,packet.scissor.y,
        packet.scissor.width,packet.scissor.height,bound_width,bound_height,uint32_t(surface.msaa_samples));
  dkRasterizerStateDefaults(&packet.raster);packet.raster.cullMode=DkFace_None;
  const auto mode=r.Get<reg::PA_SU_SC_MODE_CNTL>();
  if(polygon)packet.raster.cullMode=DkFace((mode.cull_front?DkFace_Front:0)|(mode.cull_back?DkFace_Back:0));
  packet.raster.frontFace=mode.face?DkFrontFace_CW:DkFrontFace_CCW;
  packet.raster.depthClampEnable=clip.clip_disable;
  dkMultisampleStateDefaults(&packet.samples);dkColorStateDefaults(&packet.color);dkColorWriteStateDefaults(&packet.writes);
  for(uint32_t i=0;i<4;++i) {
    dkColorWriteStateSetMask(&packet.writes,i,colors[i]?(color_mask>>(i*4))&15:0);
    dkBlendStateDefaults(&packet.blend[i]);const auto blend=r.Get<reg::RB_BLENDCONTROL>(reg::RB_BLENDCONTROL::rt_register_indices[i]);
    if(colors[i] && (blend.value&0x1FFF1FFF)!=0x00010001) {
      packet.color.blendEnableMask|=1u<<i;
      packet.blend[i].colorBlendOp=BlendOp(blend.color_comb_fcn);packet.blend[i].alphaBlendOp=BlendOp(blend.alpha_comb_fcn);
      packet.blend[i].srcColorBlendFactor=Factor(blend.color_srcblend);packet.blend[i].dstColorBlendFactor=Factor(blend.color_destblend);
      packet.blend[i].srcAlphaBlendFactor=Factor(blend.alpha_srcblend);packet.blend[i].dstAlphaBlendFactor=Factor(blend.alpha_destblend);
    }
  }
  dkDepthStencilStateDefaults(&packet.depth_stencil);
  packet.depth_stencil.depthTestEnable=depth&&depth_control.z_enable;
  packet.depth_stencil.depthWriteEnable=depth&&depth_control.z_enable&&depth_control.z_write_enable;
  packet.depth_stencil.depthCompareOp=DkCompareOp(1+uint32_t(depth_control.zfunc));
  packet.depth_stencil.stencilTestEnable=depth&&depth_control.stencil_enable;
  packet.depth_stencil.stencilFrontFailOp=DkStencilOp(1+uint32_t(depth_control.stencilfail));
  packet.depth_stencil.stencilFrontPassOp=DkStencilOp(1+uint32_t(depth_control.stencilzpass));
  packet.depth_stencil.stencilFrontDepthFailOp=DkStencilOp(1+uint32_t(depth_control.stencilzfail));
  packet.depth_stencil.stencilFrontCompareOp=DkCompareOp(1+uint32_t(depth_control.stencilfunc));
  packet.depth_stencil.stencilBackFailOp=polygon&&depth_control.backface_enable?DkStencilOp(1+uint32_t(depth_control.stencilfail_bf)):packet.depth_stencil.stencilFrontFailOp;
  packet.depth_stencil.stencilBackPassOp=polygon&&depth_control.backface_enable?DkStencilOp(1+uint32_t(depth_control.stencilzpass_bf)):packet.depth_stencil.stencilFrontPassOp;
  packet.depth_stencil.stencilBackDepthFailOp=polygon&&depth_control.backface_enable?DkStencilOp(1+uint32_t(depth_control.stencilzfail_bf)):packet.depth_stencil.stencilFrontDepthFailOp;
  packet.depth_stencil.stencilBackCompareOp=polygon&&depth_control.backface_enable?DkCompareOp(1+uint32_t(depth_control.stencilfunc_bf)):packet.depth_stencil.stencilFrontCompareOp;
  auto sf=r.Get<reg::RB_STENCILREFMASK>();auto sb=polygon&&depth_control.backface_enable?r.Get<reg::RB_STENCILREFMASK>(rex::graphics::XE_GPU_REG_RB_STENCILREFMASK_BF):sf;
  packet.front_stencil={sf.stencilwritemask,sf.stencilref,sf.stencilmask};packet.back_stencil={sb.stencilwritemask,sb.stencilref,sb.stencilmask};
  packet.blend_constant={r.Get<float>(rex::graphics::XE_GPU_REG_RB_BLEND_RED),r.Get<float>(rex::graphics::XE_GPU_REG_RB_BLEND_GREEN),
      r.Get<float>(rex::graphics::XE_GPU_REG_RB_BLEND_BLUE),r.Get<float>(rex::graphics::XE_GPU_REG_RB_BLEND_ALPHA)};
  draw_util::GetPreferredFacePolygonOffset(r,polygon,packet.depth_bias_slope,packet.depth_bias);
  packet.depth_bias*=r.Get<reg::RB_DEPTH_INFO>().depth_format==xenos::DepthRenderTargetFormat::kD24S8?
      draw_util::kD3D10PolygonOffsetFactorUnorm24:draw_util::kD3D10PolygonOffsetFactorFloat24;
  packet.depth_bias_slope*=xenos::kPolygonOffsetScaleSubpixelUnit;
  if(packet.depth_bias || packet.depth_bias_slope)packet.raster.depthBiasEnableMask=7;
  std::array<uint32_t,192> fetch_words;
  std::memcpy(fetch_words.data(),&r[0x4800],sizeof(fetch_words));
  std::set<uint64_t> needed;
  uint64_t new_bytes=0,all_bytes=0;
  for(const auto& binding:vs->vertex_bindings()) {
    const auto vf=r.GetVertexFetch(binding.fetch_constant);
    if(vf.type!=xenos::FetchConstantType::kVertex)continue;
    const uint32_t address=uint32_t(vf.address)<<2,bytes=uint32_t(vf.size)<<2;
    if(!bytes || uint64_t(address)+bytes>0x20000000){skip(8);return;}
    const uint64_t key=(uint64_t(address)<<32)|bytes;
    if(needed.insert(key).second){const uint64_t allocation=(uint64_t(bytes)+31)&~uint64_t(15);
      all_bytes+=allocation;if(!vertices.contains(key))new_bytes+=allocation;}
  }
  if(all_bytes>kVertexBytes){Error("one draw exceeds the vertex arena");skip(9);return;}
  if(uint64_t(vertex_head)+new_bytes>kVertexBytes) {
    Submit();device.EsperarInactiva();vertices.clear();vertex_head=0;Begin();
  }
  // Relocate only vertex-fetch words, as the proven Vulkan frontend does.
  // Immutable CPU staging -> ordered GPU copies prevents in-flight overwrite.
  for(const auto& binding:vs->vertex_bindings()) {
    const auto vf=r.GetVertexFetch(binding.fetch_constant);if(vf.type!=xenos::FetchConstantType::kVertex)continue;
    const uint32_t address=uint32_t(vf.address)<<2,bytes=uint32_t(vf.size)<<2;
    if(!bytes || uint64_t(address)+bytes>0x20000000){skip(8);return;}
    const auto* source=memory->TranslatePhysical<const uint8_t*>(address);
    const uint64_t key=(uint64_t(address)<<32)|bytes;
    auto [entry,inserted]=vertices.try_emplace(key);
    if(inserted){entry->second.offset=vertex_head;vertex_head=uint32_t((uint64_t(vertex_head)+bytes+31)&~uint64_t(15));}
    auto& range=entry->second;
    if(range.checked!=frame) {
      range.checked=frame;
      if(bytes<=256u<<10){range.content=XXH3_64bits(source,bytes);bytes_hashed+=bytes;}
      else range.content=range.detector.Check(source,bytes,++range.uses,address>>4,bytes_hashed);
    }
    const uint64_t hash=range.content;
    const uint32_t fetch_index=binding.fetch_constant*2;
    fetch_words[fetch_index]=(fetch_words[fetch_index]&3u)|entry->second.offset;
    if(!inserted && entry->second.hash==hash)continue;
    for(uint32_t offset=0;offset<bytes;) {
      const uint32_t chunk=std::min(8u<<20,bytes-offset);auto upload=Upload({source+offset,chunk});
      if(!upload){skip(9);return;}
      dkCmdBufBarrier(recorder.comandos(),DkBarrier_Full,0);
      dkCmdBufCopyBuffer(recorder.comandos(),upload.gpu,shared_gpu+entry->second.offset+offset,chunk);
      dkCmdBufBarrier(recorder.comandos(),DkBarrier_Full,DkInvalidateFlags_Shader);
      stats.vertex_bytes_copied+=chunk;offset+=chunk;
    }
    entry->second.hash=hash;
  }
  packet.count=init.num_indices;packet.indexed=init.source_select!=xenos::SourceSelect::kAutoIndex;
  packet.index_format=init.index_size==xenos::IndexFormat::kInt32?DkIdxFormat_Uint32:DkIdxFormat_Uint16;
  const uint32_t index_bytes=packet.index_format==DkIdxFormat_Uint32?4:2;
  std::vector<uint8_t> indices;
  if(packet.indexed) {
    if(packet.count>(1u<<20)){skip(8);return;}
    const uint8_t* source=nullptr;auto endian=r.Get<reg::VGT_DMA_SIZE>().swap_mode;
    if(init.source_select==xenos::SourceSelect::kDMA) {
      const uint32_t address=r[rex::graphics::XE_GPU_REG_VGT_DMA_BASE]&0x1FFFFFFF;
      if(uint64_t(address)+uint64_t(packet.count)*index_bytes>0x20000000){skip(8);return;}
      source=memory->TranslatePhysical<const uint8_t*>(address);
    } else if(immediate) {
      source=reinterpret_cast<const uint8_t*>(immediate);endian=xenos::Endian::kNone;
      packet.count=std::min(packet.count,immediate_words*(4/index_bytes));
    } else {skip(15);return;}
    indices.resize(size_t(packet.count)*index_bytes);
    for(uint32_t i=0;i<packet.count;++i) {
      if(index_bytes==4){uint32_t value;std::memcpy(&value,source+i*4,4);value=xenos::GpuSwap(value,endian);std::memcpy(indices.data()+i*4,&value,4);}
      else {uint16_t value;std::memcpy(&value,source+i*2,2);value=xenos::GpuSwap(value,endian);std::memcpy(indices.data()+i*2,&value,2);}
    }
    packet.restart=mode.multi_prim_ib_ena && (packet.primitive==DkPrimitive_TriangleStrip || packet.primitive==DkPrimitive_LineStrip);
    packet.restart_index=r.Get<reg::VGT_MULTI_PRIM_IB_RESET_INDX>().reset_indx;
  }
  auto fill_textures=[&](Module* module,StageBindings& stage) {
    if(!module)return true;
    for(const auto& reference:module->textures) {
      const auto handle=TextureHandle(reference);if(!handle)return false;
      stage.textures[reference.slot]=handle;stage.texture_count=std::max(stage.texture_count,reference.slot+1);
    }return true;
  };
  for(uint32_t attempt=0;attempt<3;++attempt) {
    const uint64_t current_work=work;
    if(!fill_textures(vertex,packet.vs) || !fill_textures(fragment,packet.ps)){skip(11);return;}
    if(indices.size()+32768>recorder.SubidaLibre()){Submit();Begin();continue;}
    if(current_work==work)break;
    if(attempt==2){skip(9);return;}
  }
  uint32_t used=0;for(const auto& ref:vertex->textures)used|=1u<<ref.fetch;
  if(fragment)for(const auto& ref:fragment->textures)used|=1u<<ref.fetch;
  UpdateSystemConstants(polygon,viewport,used,0,vs,ps);
  DkBufExtents sys;if(!UploadSystem(recorder,&system_constants,sizeof(system_constants),sys)){skip(9);return;}
  DkBufExtents constants[5]{};constants[0]=sys;
  auto float_constants=[&](Shader* shader,uint32_t base,uint32_t binding) {
    std::vector<uint8_t> data(shader?std::max(shader->constant_register_map().float_count,1u)*16:16,0);
    uint32_t offset=0;
    if(shader)for(uint32_t block=0;block<4;++block) {
      uint64_t bits=shader->constant_register_map().float_bitmap[block];uint32_t bit;
      while(rex::bit_scan_forward(bits,&bit)){bits&=~(uint64_t(1)<<bit);
        std::memcpy(data.data()+offset,&r[base+block*256+bit*4],16);offset+=16;}
    }
    auto upload=Upload(data);constants[binding]={upload.gpu,uint32_t(data.size())};return bool(upload);
  };
  if(!float_constants(vs,0x4000,1) || !float_constants(ps,0x4400,2)){skip(9);return;}
  auto boolean=Upload({reinterpret_cast<const uint8_t*>(&r[rex::graphics::XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031]),160});
  auto fetch=Upload({reinterpret_cast<const uint8_t*>(fetch_words.data()),sizeof(fetch_words)});
  if(!boolean || !fetch){skip(9);return;}constants[3]={boolean.gpu,160};constants[4]={fetch.gpu,768};
  auto bind=[&](Module* module,StageBindings& stage) {
    if(!module)return;stage.constant_mask=module->mask;
    for(uint32_t b=0;b<5;++b)if(module->mask&(1u<<b))stage.constants[b]=constants[b];
    stage.storage_count=1;stage.storage[0]={shared_gpu,kVertexBytes};
  };
  bind(vertex,packet.vs);bind(fragment,packet.ps);
  if(packet.geometry && primitive_helper=="point"){packet.gs.constant_mask=1;packet.gs.constants[0]=sys;}
  if(packet.indexed){auto upload=Upload(indices);if(!upload){skip(9);return;}packet.indices=upload.gpu;}
  if(!RecordDraw(recorder,packet)){Error("draw packet validation failed");skip(14);return;}
  ++stats.draws;
  DkImageRect area{std::max(packet.scissor.x,viewport.xy_offset[0]),std::max(packet.scissor.y,viewport.xy_offset[1]),0,0,0,1};
  const uint32_t right=std::min(packet.scissor.x+packet.scissor.width,viewport.xy_offset[0]+viewport.xy_extent[0]);
  const uint32_t bottom=std::min(packet.scissor.y+packet.scissor.height,viewport.xy_offset[1]+viewport.xy_extent[1]);
  area.width=right>area.x?right-area.x:0;area.height=bottom>area.y?bottom-area.y:0;
  bool rectangle_known=false,zero_color=false,zero_depth=false;
  if(packet.count==3 && init.prim_type==xenos::PrimitiveType::kRectangleList &&
      vs->ucode_data_hash()==0x1E6883FCCDE1F688ull && rex::graphics::ShaderInterpreter::CanInterpretShader(*vs)) {
    struct Sink : rex::graphics::ShaderInterpreter::ExportSink {
      float position[4]{},color[4]{};uint32_t pm=0,cm=0;
      void Export(rex::graphics::ucode::ExportRegister target,const float* value,uint32_t mask)override {
        const bool col=target==rex::graphics::ucode::ExportRegister::kVSInterpolator0;
        if(!col && target!=rex::graphics::ucode::ExportRegister::kVSPosition)return;
        for(uint32_t c=0;c<4;++c)if(mask&(1u<<c))(col?color:position)[c]=value[c];
        (col?cm:pm)|=mask;
      }
    } sink;
    rex::graphics::ShaderInterpreter interpreter(r,*memory);interpreter.SetExportSink(&sink);interpreter.SetShader(*vs);
    const auto vte=r.Get<reg::PA_CL_VTE_CNTL>();float points[3][2]{};bool valid=true;zero_color=true;zero_depth=true;
    const float all_zero[4]={};
    for(uint32_t n=0;n<3;++n) {
      uint32_t index=n;if(packet.indexed){if(index_bytes==4)std::memcpy(&index,indices.data()+n*4,4);else {uint16_t i;std::memcpy(&i,indices.data()+n*2,2);index=i;}}
      index=(index+r[rex::graphics::XE_GPU_REG_VGT_INDX_OFFSET])&xenos::kVertexIndexMask;
      index=std::clamp(index,r[rex::graphics::XE_GPU_REG_VGT_MIN_VTX_INDX]&xenos::kVertexIndexMask,
          r[rex::graphics::XE_GPU_REG_VGT_MAX_VTX_INDX]&xenos::kVertexIndexMask);
      std::memset(interpreter.temp_registers(),0,4*sizeof(float)*xenos::kMaxShaderTempRegisters);
      interpreter.temp_registers()[0]=float(index);sink.pm=sink.cm=0;interpreter.Execute();
      valid&=(sink.pm&11)==11 && std::isfinite(sink.position[3]) && (vte.vtx_xy_fmt || sink.position[3]!=0);
      zero_color&=sink.cm==15 && !std::memcmp(sink.color,all_zero,sizeof(all_zero));
      float z=sink.position[2]/(vte.vtx_z_fmt?1.f:sink.position[3]);
      if(vte.vport_z_scale_ena)z*=r.Get<float>(rex::graphics::XE_GPU_REG_PA_CL_VPORT_ZSCALE);
      if(vte.vport_z_offset_ena)z+=r.Get<float>(rex::graphics::XE_GPU_REG_PA_CL_VPORT_ZOFFSET);
      zero_depth&=(sink.pm&4) && std::isfinite(z) && (z==0 || (z<0 && clip.clip_disable));
      for(uint32_t axis=0;axis<2;++axis) {
        const float p=sink.position[axis]/(vte.vtx_xy_fmt?1.f:sink.position[3]);
        points[n][axis]=(p*viewport.ndc_scale[axis]+viewport.ndc_offset[axis]+1)*.5f*viewport.xy_extent[axis]+viewport.xy_offset[axis];
        valid&=std::isfinite(points[n][axis]);
      }
    }
    interpreter.SetExportSink(nullptr);
    float lo[2]{},hi[2]{};
    for(uint32_t axis=0;axis<2;++axis) {
      lo[axis]=std::min({points[0][axis],points[1][axis],points[2][axis]});
      hi[axis]=std::max({points[0][axis],points[1][axis],points[2][axis]});
      valid&=lo[axis]<hi[axis] && std::abs(lo[axis]-std::round(lo[axis]))<.001f && std::abs(hi[axis]-std::round(hi[axis]))<.001f;
      for(uint32_t n=0;n<3;++n)valid&=points[n][axis]==lo[axis] || points[n][axis]==hi[axis];
    }
    rectangle_known=valid;
    if(valid) {
      const uint32_t left=uint32_t(std::clamp(std::round(lo[0]),float(area.x),float(area.x+area.width)));
      const uint32_t top=uint32_t(std::clamp(std::round(lo[1]),float(area.y),float(area.y+area.height)));
      const uint32_t x=uint32_t(std::clamp(std::round(hi[0]),float(left),float(area.x+area.width)));
      const uint32_t y=uint32_t(std::clamp(std::round(hi[1]),float(top),float(area.y+area.height)));
      area={left,top,0,x-left,y-top,1};
    }
  }
  const bool initialization=rectangle_known && ps && ps->ucode_data_hash()==0xA4A965C189287B99ull &&
      (!depth_control.z_enable || depth_control.zfunc==xenos::CompareFunction::kAlways) &&
      !depth_control.stencil_enable && (!r.Get<reg::RB_COLORCONTROL>().alpha_test_enable ||
        r.Get<reg::RB_COLORCONTROL>().alpha_func==xenos::CompareFunction::kAlways);
  for(uint32_t i=0;i<4;++i)if(colors[i]) {
    const bool zero=initialization && zero_color && ((color_mask>>(i*4))&15)==15 &&
        r[reg::RB_BLENDCONTROL::rt_register_indices[i]]==0x00010001;
    Publish(*colors[i],area,zero);++Get(colors[i])->version;
  }
  if(depth && ((depth_control.z_enable&&depth_control.z_write_enable)||depth_control.stencil_enable)) {
    const bool zero=!ps && rectangle_known && zero_depth && clip.clip_disable && !clip.ucp_ena &&
        !mode.cull_front && !mode.cull_back && !packet.depth_bias && !packet.depth_bias_slope &&
        depth_control.z_enable && depth_control.z_write_enable && depth_control.zfunc==xenos::CompareFunction::kAlways &&
        depth_control.stencil_enable && depth_control.stencilfunc==xenos::CompareFunction::kAlways &&
        depth_control.stencilzpass==xenos::StencilOp::kReplace && sf.stencilwritemask==255 && !sf.stencilref &&
        (!depth_control.backface_enable || (depth_control.stencilfunc_bf==xenos::CompareFunction::kAlways &&
          depth_control.stencilzpass_bf==xenos::StencilOp::kReplace && sb.stencilwritemask==255 && !sb.stencilref));
    Publish(*depth,area,zero);
  }
  if(ps && vs && packet.count==4 && ps->ucode_data_hash()==0xF57B6BC95EB4EC9Bull &&
      vs->ucode_data_hash()==0x101E86A7F525A236ull && colors[0] && depth && (color_mask&0xFFFF)==1 &&
      colors[0]->base==depth->base && colors[0]->pitch==depth->pitch && depth_control.z_enable &&
      depth_control.z_write_enable && depth_control.zfunc==xenos::CompareFunction::kAlways && !depth_control.stencil_enable)
    if(!RestoreStencil(*Get(colors[0]),*Get(depth),area))Error("stencil restoration failed");
}
void GameRenderer::Impl::Resolve() {
  Begin();const RegisterFile& r=*regs;const auto control=r.Get<reg::RB_COPY_CONTROL>();
  const auto surface=r.Get<reg::RB_SURFACE_INFO>();const auto fetch=r.GetVertexFetch(0);
  if(fetch.type!=xenos::FetchConstantType::kVertex || fetch.size!=6 || !surface.surface_pitch)return;
  const uint32_t address=uint32_t(fetch.address)<<2;if(uint64_t(address)+24>0x20000000)return;
  const auto* vertices=memory->TranslatePhysical<const float*>(address);
  const float half=r.Get<reg::PA_SU_VTX_CNTL>().pix_center==xenos::PixelCenter::kD3DZero?0.5f:0;
  int32_t v[6];for(uint32_t i=0;i<6;++i){const float f=xenos::GpuSwap(vertices[i],fetch.endian)+half;if(!std::isfinite(f) || std::abs(f)>16384)return;v[i]=int32_t(f*256);}
  int32_t x0=(std::min({v[0],v[2],v[4]})+127)>>8,y0=(std::min({v[1],v[3],v[5]})+127)>>8;
  int32_t x1=(std::max({v[0],v[2],v[4]})+127)>>8,y1=(std::max({v[1],v[3],v[5]})+127)>>8;
  if(r.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable){auto offset=r.Get<reg::PA_SC_WINDOW_OFFSET>();x0+=offset.window_x_offset;x1+=offset.window_x_offset;y0+=offset.window_y_offset;y1+=offset.window_y_offset;}
  draw_util::Scissor scissor;draw_util::GetScissor(r,scissor,false);
  x0=std::clamp(x0,int32_t(scissor.offset[0]),int32_t(scissor.offset[0]+scissor.extent[0]));
  x1=std::clamp(x1,int32_t(scissor.offset[0]),int32_t(scissor.offset[0]+scissor.extent[0]));
  y0=std::clamp(y0,int32_t(scissor.offset[1]),int32_t(scissor.offset[1]+scissor.extent[1]));
  y1=std::clamp(y1,int32_t(scissor.offset[1]),int32_t(scissor.offset[1]+scissor.extent[1]));
  x0=std::max(0,x0&~7);y0=std::max(0,y0&~7);x1=std::min(int32_t(surface.surface_pitch),(x1+7)&~7);y1=(y1+7)&~7;
  if(x1<=x0 || y1<=y0)return;
  const bool depth=control.copy_src_select>=4;Target* source=nullptr;
  if(depth){auto info=r.Get<reg::RB_DEPTH_INFO>();source=TargetFor(info.depth_base,surface.surface_pitch,uint32_t(info.depth_format),true,uint32_t(y1));}
  else {auto info=r.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[control.copy_src_select]);source=TargetFor(info.color_base,surface.surface_pitch,uint32_t(xenos::GetStorageColorFormat(info.color_format)),false,uint32_t(y1));}
  if(!source)return;Import(*source);auto* image=Get(source);y1=std::min(y1,int32_t(image->spec.height));
  if(y1<=y0)return;
  const DkImageRect rectangle{uint32_t(x0),uint32_t(y0),0,uint32_t(x1-x0),uint32_t(y1-y0),1};
  const uint32_t dest_address=r[rex::graphics::XE_GPU_REG_RB_COPY_DEST_BASE]&0x1FFFFFFF;
  const auto dest_info=r.Get<reg::RB_COPY_DEST_INFO>();
  uint32_t host_swizzle=xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
  DkImageFormat requested=depth?DkImageFormat_R32_Float:image->spec.format;
  float color_scale=1;
  if(!depth) {
    const auto host=native::texture_cpu::GetHostFormat(static_cast<xenos::TextureFormat>(dest_info.copy_dest_format));
    const auto converted=TextureFormat(host.unorm);
    if(converted!=DkImageFormat_None && !host.compressed){requested=converted;host_swizzle=host.swizzle;}
    int32_t bias=dest_info.copy_dest_exp_bias;
    if(source->format==4 || source->format==5)bias=std::min(31,bias+5);
    color_scale=std::ldexp(1.f,bias);
    if(dest_info.copy_dest_swap)host_swizzle=(host_swizzle&~0x1C7u)|((host_swizzle&7)<<6)|((host_swizzle>>6)&7);
  } else host_swizzle=xenos::XE_GPU_TEXTURE_SWIZZLE_RRRR;
  if(dest_address && (control.copy_command==xenos::CopyCommand::kRaw || control.copy_command==xenos::CopyCommand::kConvert)) {
    Image* dest=nullptr;if(auto found=resolves.find(dest_address);found!=resolves.end())dest=images->Find(found->second);
    if(!dest || dest->spec.width<uint32_t(x1) || dest->spec.height<uint32_t(y1) ||
        dest->spec.format!=requested) {
      if(dest)images->Release(dest->id);
      ImageSpec spec;spec.use=ImageUse::Resolve;spec.width=uint32_t(x1);spec.height=uint32_t(y1);spec.guest_address=dest_address;
      spec.guest_pitch=r[rex::graphics::XE_GPU_REG_RB_COPY_DEST_PITCH]&0x3FFF;
      spec.format=requested;spec.storage=depth;spec.render_target=!depth;spec.host_swizzle=host_swizzle;
      dest=images->Create(spec);if(!dest){Error("resolve destination allocation failed");return;}resolves[dest_address]=dest->id;
    }
    dest->spec.host_swizzle=host_swizzle;
    const bool copied=depth?images->Copy(source->id,rectangle,dest->id,rectangle)==ImageTransfer::Recorded:
        CopyColor(*image,rectangle,*dest,rectangle,color_scale);
    if(!copied)Error("resolve needs unsupported conversion");
    else {resolve_written[dest_address]={rectangle,dest->spec.guest_pitch,frame};++resolve_version;}
    ++stats.resolves;
  }
  if(control.color_clear_enable && !depth) {
    const uint64_t value=r[rex::graphics::XE_GPU_REG_RB_COLOR_CLEAR]|
        (uint64_t(r[rex::graphics::XE_GPU_REG_RB_COLOR_CLEAR_LO])<<32);
    float rgba[4]{};
    if(source->format==0 || source->format==1)for(uint32_t c=0;c<4;++c)rgba[c]=float((value>>(c*8))&255)/255;
    else if(source->format==2 || source->format==10){for(uint32_t c=0;c<3;++c)rgba[c]=float((value>>(c*10))&1023)/1023;rgba[3]=float(value>>30&3)/3;}
    else if(source->format==3 || source->format==12){for(uint32_t c=0;c<3;++c)rgba[c]=xenos::Float7e3To32(value>>(c*10)&1023);rgba[3]=float(value>>30&3)/3;}
    else if(source->format==6 || source->format==7)for(uint32_t c=0;c<4;++c)rgba[c]=rex::xenos_half_to_float(uint16_t(value>>(c*16)));
    else if(source->format==4 || source->format==5)for(uint32_t c=0;c<4;++c)rgba[c]=std::max(-1.f,float(int16_t(value>>(c*16)))/32767);
    else std::memcpy(rgba,&value,std::min(sizeof(value),sizeof(rgba)));
    images->ClearColor(source->id,rectangle,rgba);Publish(*source,rectangle,value==0);
  }
  if(control.depth_clear_enable) {
    auto info=r.Get<reg::RB_DEPTH_INFO>();auto* target=TargetFor(info.depth_base,surface.surface_pitch,uint32_t(info.depth_format),true,uint32_t(y1));
    if(target && uint32_t(x1)<=Get(target)->spec.width && uint32_t(y1)<=Get(target)->spec.height) {
      const uint32_t clear=r[rex::graphics::XE_GPU_REG_RB_DEPTH_CLEAR];
      const float z=info.depth_format==xenos::DepthRenderTargetFormat::kD24FS8?xenos::Float20e4To32(clear>>8):xenos::UNorm24To32(clear>>8);
      images->ClearDepth(target->id,rectangle,true,z,255,uint8_t(clear));
    }
  }
}
void GameRenderer::Impl::UpdateSystemConstants(bool primitive_polygonal,
                                           const draw_util::ViewportInfo& viewport,
                                           uint32_t used_texture_mask, uint32_t,
                                           const Shader* vs, const Shader* ps) {
  // VulkanCommandProcessor::UpdateSystemConstantValues for host render
  // targets, without EDRAM, tessellation or memory export.
  const RegisterFile& r = *regs;
  auto pa_cl_clip_cntl = r.Get<reg::PA_CL_CLIP_CNTL>();
  auto pa_cl_vte_cntl = r.Get<reg::PA_CL_VTE_CNTL>();
  auto rb_colorcontrol = r.Get<reg::RB_COLORCONTROL>();
  auto rb_depth_info = r.Get<reg::RB_DEPTH_INFO>();
  auto rb_surface_info = r.Get<reg::RB_SURFACE_INFO>();
  auto vgt_draw_initiator = r.Get<reg::VGT_DRAW_INITIATOR>();
  SpirvShaderTranslator::SystemConstants& sc = system_constants;

  uint32_t flags = 0;
  if (pa_cl_vte_cntl.vtx_xy_fmt) flags |= SpirvShaderTranslator::kSysFlag_XYDividedByW;
  if (pa_cl_vte_cntl.vtx_z_fmt) flags |= SpirvShaderTranslator::kSysFlag_ZDividedByW;
  if (pa_cl_vte_cntl.vtx_w0_fmt) flags |= SpirvShaderTranslator::kSysFlag_WNotReciprocal;
  if (primitive_polygonal) flags |= SpirvShaderTranslator::kSysFlag_PrimitivePolygonal;
  if (draw_util::IsPrimitiveLine(r)) flags |= SpirvShaderTranslator::kSysFlag_PrimitiveLine;
  flags |= 0u
           << SpirvShaderTranslator::kSysFlag_MsaaSamples_Shift;
  if (rb_depth_info.depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
    flags |= SpirvShaderTranslator::kSysFlag_DepthFloat24;
  }
  xenos::CompareFunction alpha_test = rb_colorcontrol.alpha_test_enable
                                          ? rb_colorcontrol.alpha_func
                                          : xenos::CompareFunction::kAlways;
  flags |= uint32_t(alpha_test) << SpirvShaderTranslator::kSysFlag_AlphaPassIfLess_Shift;
  for (uint32_t i = 0; i < 4; ++i) {
    auto color_info = r.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    if (color_info.color_format == xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {
      flags |= SpirvShaderTranslator::kSysFlag_ConvertColor0ToGamma << i;
    }
    // Exponent bias, with 16_16 and 16_16_16_16 stored as -1...1 SNORM
    // instead of -32...32.
    int32_t exp_bias = color_info.color_exp_bias;
    if (color_info.color_format == xenos::ColorRenderTargetFormat::k_16_16 ||
        color_info.color_format == xenos::ColorRenderTargetFormat::k_16_16_16_16) {
      exp_bias -= 5;
    }
    float scale;
    uint32_t scale_bits = UINT32_C(0x3F800000) + (uint32_t(exp_bias) << 23);
    std::memcpy(&scale, &scale_bits, sizeof(scale));
    sc.color_exp_bias[i] = scale;
  }
  sc.flags = flags;
  sc.vertex_index_endian = xenos::Endian::kNone;
  sc.line_loop_closing_index = 0;
  sc.vertex_base_index = r.Get<int32_t>(rex::graphics::XE_GPU_REG_VGT_INDX_OFFSET);
  sc.vertex_index_min = r[rex::graphics::XE_GPU_REG_VGT_MIN_VTX_INDX];
  sc.vertex_index_max = r[rex::graphics::XE_GPU_REG_VGT_MAX_VTX_INDX];
  sc.vertex_index_reset = 0;
  sc.compute_memexport_vertex_count = 0;
  if (!pa_cl_clip_cntl.clip_disable) {
    float* planes = sc.user_clip_planes[0];
    uint32_t remaining = pa_cl_clip_cntl.ucp_ena;
    uint32_t index;
    while (rex::bit_scan_forward(remaining, &index)) {
      remaining &= ~(1u << index);
      std::memcpy(planes, &r[rex::graphics::XE_GPU_REG_PA_CL_UCP_0_X + index * 4],
                  4 * sizeof(float));
      planes += 4;
    }
  }
  for (uint32_t i = 0; i < 3; ++i) {
    sc.ndc_scale[i] = viewport.ndc_scale[i];
    sc.ndc_offset[i] = viewport.ndc_offset[i];
  }
  if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
    auto minmax = r.Get<reg::PA_SU_POINT_MINMAX>();
    auto size = r.Get<reg::PA_SU_POINT_SIZE>();
    sc.point_vertex_diameter_min = float(minmax.min_size) * (2.0f / 16.0f);
    sc.point_vertex_diameter_max = float(minmax.max_size) * (2.0f / 16.0f);
    sc.point_constant_diameter[0] = float(size.width) * (2.0f / 16.0f);
    sc.point_constant_diameter[1] = float(size.height) * (2.0f / 16.0f);
    sc.point_screen_diameter_to_ndc_radius[0] =
        1.0f / float(std::max(viewport.xy_extent[0], 1u));
    sc.point_screen_diameter_to_ndc_radius[1] =
        1.0f / float(std::max(viewport.xy_extent[1], 1u));
  }
  uint32_t remaining = used_texture_mask;
  uint32_t index;
  while (rex::bit_scan_forward(remaining, &index)) {
    remaining &= ~(1u << index);
    uint32_t& word = sc.texture_swizzled_signs[index >> 2];
    uint32_t shift = 8 * (index & 3);
    uint8_t signs = rex::graphics::texture_util::SwizzleSigns(r.GetTextureFetch(index));
    word = (word & ~(0xFFu << shift)) | (uint32_t(signs) << shift);
  }
  sc.textures_resolution_scaled = 0;
  sc.alpha_test_reference = r.Get<float>(rex::graphics::XE_GPU_REG_RB_ALPHA_REF);
  sc.alpha_to_mask = rb_colorcontrol.alpha_to_mask_enable
                         ? (rb_colorcontrol.value >> 24) | (UINT32_C(1) << 8)
                         : 0;
  (void)vs;
  (void)ps;
}


GameRenderer::GameRenderer():dk_(std::make_unique<Impl>()){
  for(uint32_t i=0;i<256;++i){const uint32_t v=i*1023/255;dk_->gamma[i]=v|(v<<10)|(v<<20);}
}
GameRenderer::~GameRenderer(){Shutdown();}
bool GameRenderer::InitializePresentation() {
  if(dk_->device_ready)return true;
  rex::deko3d::FijarRegistro([](const char* line){REXLOG_INFO("[deko] {}",line);},[](){rex::FlushLogging();std::fflush(nullptr);});
  dk_->device.Crear(false);dk_->device_ready=true;
  REXLOG_INFO("[deko-game-v10] independent submit/wait; missing shader capture; image accounting");rex::FlushLogging();return true;
}
bool GameRenderer::Initialize(rex::ui::vulkan::VulkanProvider*,rex::memory::Memory* memory,RegisterFile* regs) {
  auto& d=*dk_;if(!d.device_ready || !memory || !regs)return false;d.memory=memory;d.regs=regs;
  // Optional rollback to the intrusive 007 diagnostics, without another NRO.
  if(std::ifstream tuning{"sdmc:/switch/green_lantern/tuning.ini"};tuning) {
    std::string line;while(std::getline(tuning,line)) {
      unsigned value=0;char extra=0;
      if(std::sscanf(line.c_str()," deko_diagnostic_each_draw = %u %c",&value,&extra)==1 && value<=1)
        d.diagnostic_each_draw=value!=0;
      if(std::sscanf(line.c_str()," deko_batch_ops = %u %c",&value,&extra)==1 && value>=1 && value<=64)
        d.batch_ops=value;
      if(std::sscanf(line.c_str()," deko_wait_submissions = %u %c",&value,&extra)==1 && value>=1 && value<=64)
        d.wait_submissions=value;
    }
  }
  std::ifstream manifest(std::string(kRoot)+"modules.txt");if(!manifest){d.Error("missing deko_shaders/modules.txt");return false;}
  std::string line;
  while(std::getline(manifest,line)) {
    std::istringstream in(line);std::string stage;Impl::Module module;uint32_t count=0;
    if(!(in>>stage>>std::hex>>module.hash>>module.mod>>std::dec>>module.filename>>module.mask))return false;
    for(auto& size:module.sizes)if(!(in>>size))return false;
    if(!(in>>count) || count>32)return false;
    for(uint32_t n=0;n<count;++n){Impl::TextureRef ref;if(!(in>>ref.slot>>ref.fetch>>ref.sign>>ref.dimension>>ref.filters) ||
        ref.slot>=32 || ref.fetch>=32)return false;module.textures.push_back(ref);}
    d.modules.emplace(std::make_tuple(stage=="vs"?'v':'p',module.hash,module.mod),std::move(module));
  }
  if(d.modules.empty())return false;
  rex::deko3d::ConfigNucleo config;config.ranuras=3;config.mb_subida=32;config.mb_codigo=16;
  config.kb_comandos=4096;config.descriptores_imagen=65536;config.descriptores_sampler=1024;
  d.recorder.Crear(d.device,config);d.images=std::make_unique<Images>(d.recorder,384ull<<20);
  d.library=std::make_unique<ShaderLibrary>(d.recorder);
  d.shared=d.device.CrearBloque(kVertexBytes,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached,"relocated vertex arena");
  d.edram=d.device.CrearBloque(10u<<20,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached,"EDRAM color words");
  d.shared_gpu=dkMemBlockGetGpuAddr(d.shared);d.edram_gpu=dkMemBlockGetGpuAddr(d.edram);
  auto depth=ReadFile("depth_resolve.dksh");if(!d.images->InitializeDepthResolve(depth))return false;
  d.ready=true;d.report_ns=rex::deko3d::AhoraNs();d.Begin();
  REXLOG_INFO("[deko-game-v10] ready: {} shader variants; diagnostic={} submit_ops={} wait_submissions={}; vertex=160 MiB upload=3x32 MiB image budget=384 MiB",d.modules.size(),d.diagnostic_each_draw,d.batch_ops,d.wait_submissions);return true;
}
void GameRenderer::Shutdown() {
  if(!dk_ || !dk_->device_ready)return;auto& d=*dk_;
  if(d.ready)d.Submit();d.device.EsperarInactiva();d.output.Shutdown();
  d.library.reset();d.images.reset();d.recorder.Destruir();
  d.targets.clear();d.tiles={};d.resolves.clear();d.resolve_written.clear();d.sized_resolves.clear();
  d.textures.clear();d.descriptors.clear();d.samplers.clear();d.vertices.clear();d.vertex_head=0;d.helpers.clear();
  for(auto& [key,module]:d.modules)module.shader=nullptr;
  if(d.shared)d.device.DestruirBloque(d.shared);if(d.edram)d.device.DestruirBloque(d.edram);
  d.shared=d.edram=nullptr;d.device.Destruir();d.device_ready=d.ready=d.output_ready=false;
}
void GameRenderer::LoadShader(xenos::ShaderType type,const uint32_t* code,uint32_t words,uint32_t) {
  auto& d=*dk_;if(!d.ready || !code || !words)return;const uint64_t hash=XXH3_64bits(code,size_t(words)*4);
  auto& shader=d.shaders[hash];if(!shader){shader=std::make_unique<Shader>(type,hash,code,words);shader->AnalyzeUcode(d.disassembly);}
  if(type==xenos::ShaderType::kVertex)d.active_vs=shader.get();else d.active_ps=shader.get();
}
void GameRenderer::Draw(const uint32_t* immediate,uint32_t words){
  if(dk_->ready){const auto before=dk_->stats.draws;dk_->Draw(immediate,words);
    if(dk_->stats.draws!=before)dk_->Checkpoint("draw");}
}
void GameRenderer::Resolve(){if(dk_->ready){dk_->Resolve();dk_->Checkpoint("resolve");}}
void GameRenderer::Flush(){if(dk_->ready){dk_->Submit();dk_->Begin();}}
void GameRenderer::SetGammaRamp256(const uint32_t* values){std::memcpy(dk_->gamma,values,sizeof(dk_->gamma));}
const native::RendererStats& GameRenderer::stats()const{return dk_->stats;}
void GameRenderer::Swap(rex::ui::Presenter*,uint32_t address,uint32_t width,uint32_t height) {
  auto& d=*dk_;if(!d.ready)return;
  if(!d.output_ready) {
    auto vs=ReadFile("output.vert.glsl.dksh"),ps=ReadFile("output.frag.glsl.dksh");
    d.output_ready=d.output.Initialize(d.device,1280,720,nullptr,vs,ps);
    if(!d.output_ready){d.Error("swapchain setup failed");return;}
  }
  d.Submit();
  uint32_t frontbuffer=address&0x1FFFFFFF,swizzle=xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
  const auto fetch=d.regs->GetTextureFetch(0);
  if(fetch.type==xenos::FetchConstantType::kTexture && fetch.base_address) {
    frontbuffer=(fetch.base_address&0x1FFFF)<<12;swizzle=fetch.swizzle;
  }
  auto found=d.resolves.find(frontbuffer);
  if(found!=d.resolves.end()){if(auto* source=d.images->Find(found->second)) {
    d.images->MarkUsed(source->id);
    if(d.frame<4 || d.frame%120==0)REXLOG_INFO("[deko-present-v5] frame={} base={:08X} image={}x{} visible={}x{} swizzle={:03X}",
        d.frame,frontbuffer,source->spec.width,source->spec.height,width,height,swizzle);
    if(d.frame<4 || d.frame%120==0)if(auto written=d.resolve_written.find(frontbuffer);written!=d.resolve_written.end())
      REXLOG_INFO("[deko-rect-v6] last frontbuffer copy={},{} {}x{}",written->second.rect.x,written->second.rect.y,
          written->second.rect.width,written->second.rect.height);
    d.output.Present(*source,d.gamma,width,height,swizzle);
  }}
  else d.Error(fmt::format("no resolved frontbuffer {:08X} {}x{}",address,width,height));
  ++d.frame;
  d.images->SetFrame(d.frame);
  const uint64_t now=rex::deko3d::AhoraNs();
  if(now-d.report_ns>=5'000'000'000ull) {
    const uint64_t frames=d.frame-d.report_frame;const double seconds=double(now-d.report_ns)*1e-9;
    REXLOG_INFO("[deko-perf-v8] frame={} wall_s={:.2f} fps={:.2f} draws_per_frame={:.1f} resolves_per_frame={:.1f} submits_per_frame={:.1f} queue_idle_wait_ms_per_frame={:.2f} image_mib={:.1f} skipped_total={}",
        d.frame,seconds,double(frames)/seconds,double(d.stats.draws-d.report_draws)/frames,
        double(d.stats.resolves-d.report_resolves)/frames,double(d.stats.submissions-d.report_submits)/frames,
        double(d.queue_idle_wait_ns-d.report_wait)*1e-6/frames,double(d.images->bytes())/(1u<<20),d.stats.draws_skipped);
    d.report_ns=now;d.report_frame=d.frame;d.report_draws=d.stats.draws;d.report_resolves=d.stats.resolves;
    d.report_submits=d.stats.submissions;d.report_wait=d.queue_idle_wait_ns;
    const auto memory=d.images->Report();
    const char* names[]={"texture","target","resolve","sized_resolve","depth_words"};
    for(uint32_t i=0;i<5;++i)REXLOG_INFO("[deko-memory-v10] category={} count={} mib={:.2f} max_idle_frames={} created_total={}",
        names[i],memory.active[i].count,double(memory.active[i].bytes)/(1u<<20),
        memory.active[i].max_age_frames,memory.active[i].created_total);
    REXLOG_INFO("[deko-memory-v10] retired_count={} retired_mib={:.2f} peak_mib={:.2f} texture_requests={} texture_reuses={} resolve_texture_lookups={}",
        memory.retired_count,double(memory.retired_bytes)/(1u<<20),double(memory.peak_bytes)/(1u<<20),
        d.texture_requests,d.texture_reuses,d.resolve_texture_lookups);
  }
  if(d.frame%120==0)REXLOG_INFO("[deko-game-v4] frame={} draws={} skipped={} resolves={} images={} bytes={} submissions={}",
      d.frame,d.stats.draws,d.stats.draws_skipped,d.stats.resolves,d.stats.textures_created,d.images->bytes(),d.stats.submissions);
  d.Begin();
  for(auto it=d.sized_resolves.begin();it!=d.sized_resolves.end();) {
    if(!d.images->Find(std::get<0>(it->first))){d.images->Release(it->second.id);it=d.sized_resolves.erase(it);}else ++it;
  }
  for(auto it=d.textures.begin();it!=d.textures.end();) {
    if(it->second.last_frame+60<d.frame){d.images->Release(it->second.id);it=d.textures.erase(it);}else ++it;
  }
}
} // namespace green_lantern::deko
