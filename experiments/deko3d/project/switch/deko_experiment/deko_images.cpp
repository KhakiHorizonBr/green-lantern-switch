#include "deko_images.h"
#include "deko_depth.h"
#include <cstring>
#include <cmath>
#include <limits>
#include <algorithm>

namespace green_lantern::deko {
namespace {
bool RectInside(const Image& i,const DkImageRect& r) {
  return r.width && r.height && r.z==0 && r.depth==1 &&
      uint64_t(r.x)+r.width<=i.spec.width && uint64_t(r.y)+r.height<=i.spec.height;
}
uint32_t TexelBytes(DkImageFormat f) {
  switch(f) {
    case DkImageFormat_R8_Unorm:case DkImageFormat_R8_Snorm:return 1;
    case DkImageFormat_RG8_Unorm:case DkImageFormat_RG8_Snorm:
    case DkImageFormat_R16_Float:case DkImageFormat_R16_Unorm:case DkImageFormat_R16_Snorm:
    case DkImageFormat_RGB565_Unorm:case DkImageFormat_BGR5A1_Unorm:return 2;
    case DkImageFormat_RGBA8_Unorm:case DkImageFormat_RGBA8_Unorm_sRGB:
    case DkImageFormat_BGRA8_Unorm:case DkImageFormat_RGB10A2_Unorm:
    case DkImageFormat_RGBA8_Snorm:case DkImageFormat_R32_Float:case DkImageFormat_RG16_Float:
    case DkImageFormat_RG16_Unorm:case DkImageFormat_RG16_Snorm:return 4;
    case DkImageFormat_RGBA16_Unorm:case DkImageFormat_RGBA16_Snorm:
    case DkImageFormat_RGBA16_Float:case DkImageFormat_RG32_Float:return 8;
    case DkImageFormat_RGBA32_Float:return 16;
    default:return 0;
  }
}
bool Compressed(DkImageFormat f) {
  return f>=DkImageFormat_RGB_BC1 && f<=DkImageFormat_RG_BC5_Snorm;
}
uint32_t BlockBytes(DkImageFormat f) {
  if(!Compressed(f))return TexelBytes(f);
  switch(f) {
    case DkImageFormat_RGB_BC1:case DkImageFormat_RGBA_BC1:
    case DkImageFormat_RGB_BC1_sRGB:case DkImageFormat_RGBA_BC1_sRGB:
    case DkImageFormat_R_BC4_Unorm:case DkImageFormat_R_BC4_Snorm:return 8;
    default:return 16;
  }
}
bool DepthFormat(DkImageFormat f) {
  return f==DkImageFormat_Z16 || f==DkImageFormat_Z24X8 || f==DkImageFormat_ZF32 ||
      f==DkImageFormat_Z24S8 || f==DkImageFormat_ZF32_X24S8;
}
}
Images::Images(rex::deko3d::Nucleo& recorder,uint64_t budget):recorder_(recorder),budget_(budget){}
Images::~Images()=default;
bool Images::InitializeDepthResolve(std::span<const uint8_t> dksh) {
  if(depth_resolve_)return false;
  auto converter=std::make_unique<DepthResolve>(recorder_,*this);
  if(!converter->Initialize(dksh))return false;
  depth_resolve_=std::move(converter);return true;
}
Image* Images::PackedDepthWords(uint64_t depth) {
  auto it=packed_depth_.find(depth);return it==packed_depth_.end()?nullptr:Find(it->second);
}
bool Images::BeginWork(uint64_t work) {
  if(!recorder_.TrabajoAbierto() || !work || work!=work_+1)return false;
  work_=work;
  for(auto it=transient_views_.begin();it!=transient_views_.end();) {
    if(work-it->work>=recorder_.config().ranuras) {
      for(uint32_t slot:it->slots){image_slots_.erase(slot);transient_slots_.erase(slot);}
      it=transient_views_.erase(it);
    } else ++it;
  }
  for(auto i=retired_.begin();i!=retired_.end();) {
    if(work>=i->work && work-i->work>=recorder_.config().ranuras) {
      for(auto slot=image_slots_.begin();slot!=image_slots_.end();) {
        if(slot->second==i->id)slot=image_slots_.erase(slot);else ++slot;
      }
      bytes_-=i->bytes;i=retired_.erase(i);
    } else ++i;
  }
  return true;
}
Image* Images::Create(const ImageSpec& spec) {
  if(!recorder_.TrabajoAbierto() || !work_ || uint32_t(spec.use)>=5 || !spec.width || !spec.height ||
      spec.width>16384 || spec.height>16384 || spec.format==DkImageFormat_None ||
      !spec.levels || spec.levels>15 || !spec.layers || spec.layers>2048 ||
      spec.levels>uint32_t(1+std::floor(std::log2(std::max(spec.width,spec.height)))) ||
      (spec.cube && (spec.layers!=6 || spec.width!=spec.height)) ||
      (spec.storage && (spec.depth || spec.samples!=DkMsMode_1x || !TexelBytes(spec.format))) ||
      ((spec.depth || spec.render_target || spec.samples!=DkMsMode_1x) &&
        (spec.layers!=1 || spec.levels!=1)) ||
      spec.depth!=DepthFormat(spec.format) || (spec.render_target && !spec.depth && !TexelBytes(spec.format)) ||
      (spec.samples!=DkMsMode_1x && spec.samples!=DkMsMode_2x &&
       spec.samples!=DkMsMode_4x && spec.samples!=DkMsMode_8x))return nullptr;
  DkImageLayoutMaker maker;
  dkImageLayoutMakerDefaults(&maker,recorder_.dispositivo());
  maker.format=spec.format;maker.dimensions[0]=spec.width;maker.dimensions[1]=spec.height;
  maker.type=spec.cube?DkImageType_Cubemap:(spec.layers>1?DkImageType_2DArray:DkImageType_2D);
  maker.dimensions[2]=spec.layers;maker.mipLevels=spec.levels;
  maker.msMode=spec.samples;
  maker.flags=spec.depth?DkImageFlags_UsageRender:
      (spec.render_target?DkImageFlags_Usage2DEngine|DkImageFlags_UsageRender:0u);
  if(spec.storage)maker.flags|=DkImageFlags_UsageLoadStore;
  rex::deko3d::ArreglarTamanoDeTesela(maker,Compressed(spec.format)?(spec.height+3)/4:spec.height);
  DkImageLayout layout;dkImageLayoutInitialize(&layout,&maker);
  const uint64_t bytes=dkImageLayoutGetSize(&layout);
  if(!bytes || bytes>UINT32_MAX || bytes>budget_ || bytes_>budget_-bytes)return nullptr;
  auto image=std::make_unique<Image>();
  image->memory=recorder_.ReservarImagen(uint32_t(bytes),dkImageLayoutGetAlignment(&layout));
  if(!image->memory)return nullptr;
  if(image->memory.bytes>budget_-bytes_) {
    recorder_.LiberarImagenLuego(image->memory);
    // Layouts must already satisfy the allocator's granularity: count a failed
    // allocation too until the slot reclaims it, rather than hiding peak memory.
    bytes_+=image->memory.bytes;retired_.push_back({work_,image->memory.bytes,0});
    return nullptr;
  }
  image->id=++next_;image->spec=spec;image->created_frame=image->last_used_frame=frame_;
  dkImageInitialize(&image->image,&layout,image->memory.bloque,image->memory.offset);
  dkImageViewDefaults(&image->view,&image->image);
  Image* result=image.get();bytes_+=image->memory.bytes;peak_bytes_=std::max(peak_bytes_,bytes_);
  ++created_by_use_[uint32_t(spec.use)];images_.emplace(image->id,std::move(image));
  return result;
}
Image* Images::Find(uint64_t id) {
  auto it=images_.find(id);if(it==images_.end())return nullptr;
  return it->second.get();
}
Images::MemoryReport Images::Report() const {
  MemoryReport r;r.peak_bytes=peak_bytes_;r.retired_count=retired_.size();
  for(const auto& retired:retired_)r.retired_bytes+=retired.bytes;
  for(uint32_t i=0;i<5;++i)r.active[i].created_total=created_by_use_[i];
  for(const auto& [id,image]:images_) {
    auto& category=r.active[uint32_t(image->spec.use)];++category.count;category.bytes+=image->memory.bytes;
    category.max_age_frames=std::max(category.max_age_frames,frame_-std::min(frame_,image->last_used_frame));
  }
  return r;
}
bool Images::Release(uint64_t id) {
  auto i=images_.find(id);if(i==images_.end() || !recorder_.TrabajoAbierto())return false;
  if(!work_)return false;
  if(auto pair=packed_depth_.find(id);pair!=packed_depth_.end()) {
    const uint64_t words=pair->second;packed_depth_.erase(pair);
    if(Find(words))Release(words);
  }
  for(auto pair=packed_depth_.begin();pair!=packed_depth_.end();) {
    if(pair->second==id)pair=packed_depth_.erase(pair);else ++pair;
  }
  recorder_.LiberarImagenLuego(i->second->memory);
  retired_.push_back({work_,i->second->memory.bytes,id});images_.erase(i);return true;
}
ImageTransfer Images::UploadLinear(uint64_t id,std::span<const uint8_t> data,uint32_t row_bytes) {
  return UploadLevel(id,0,0,data,row_bytes);
}
ImageTransfer Images::UploadLevel(uint64_t id,uint32_t level,uint32_t layer,
    std::span<const uint8_t> data,uint32_t row_bytes) {
  auto* i=Find(id);if(!i || !recorder_.TrabajoAbierto())return ImageTransfer::Invalid;
  MarkUsed(id);
  if(level>=i->spec.levels || layer>=i->spec.layers)return ImageTransfer::Invalid;
  const uint32_t texel=BlockBytes(i->spec.format);
  if(i->spec.depth || i->spec.samples!=DkMsMode_1x || !texel)return ImageTransfer::NeedsConversion;
  const uint32_t w=std::max(1u,i->spec.width>>level),h=std::max(1u,i->spec.height>>level);
  const uint32_t block=Compressed(i->spec.format)?4:1;
  const uint64_t bytes=uint64_t(row_bytes)*((h+block-1)/block);
  if(row_bytes<uint64_t((w+block-1)/block)*texel || row_bytes%texel ||
      bytes>UINT32_MAX || bytes>data.size())return ImageTransfer::Invalid;
  auto upload=recorder_.Reservar(uint32_t(bytes),256);if(!upload)return ImageTransfer::Invalid;
  std::memcpy(upload.cpu,data.data(),size_t(bytes));
  const DkCopyBuf source{upload.gpu,row_bytes,uint32_t(bytes)};
  const DkImageRect rect{0,0,0,w,h,1};
  DkImageView view=i->view;view.type=DkImageType_2D;view.layerOffset=layer;view.layerCount=1;
  view.mipLevelOffset=level;view.mipLevelCount=1;
  // Record in-order: hoisting a second upload to the pre-list could change an
  // earlier draw which already sampled this image. A replacement image is preferable.
  auto cmd=recorder_.comandos();
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
  dkCmdBufCopyBufferToImage(cmd,&source,&view,&rect,0);
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
  ++i->version;return ImageTransfer::Recorded;
}
ImageTransfer Images::Copy(uint64_t source,const DkImageRect& a,uint64_t destination,const DkImageRect& b) {
  auto* s=Find(source);auto* d=Find(destination);
  MarkUsed(source);MarkUsed(destination);
  if(!s || !d || !recorder_.TrabajoAbierto() || !RectInside(*s,a) || !RectInside(*d,b))return ImageTransfer::Invalid;
  if(s->spec.depth && d->spec.format==DkImageFormat_R32_Float && d->spec.storage && depth_resolve_) {
    if(s->spec.samples!=DkMsMode_1x || d->spec.samples!=DkMsMode_1x ||
        s->spec.layers!=1 || d->spec.layers!=1 || d->spec.levels!=1 ||
        a.width!=b.width || a.height!=b.height)return ImageTransfer::NeedsConversion;
    auto* words=PackedDepthWords(destination);
    if(!words) {
      ImageSpec spec;spec.use=ImageUse::DepthWords;spec.width=d->spec.width;spec.height=d->spec.height;
      spec.guest_address=d->spec.guest_address;spec.guest_pitch=d->spec.guest_pitch;
      spec.format=DkImageFormat_RGBA8_Unorm;spec.storage=true;
      words=Create(spec);if(!words)return ImageTransfer::Invalid;
      packed_depth_[destination]=words->id;
    }
    return depth_resolve_->Record(source,a,destination,words->id,b,s->spec.guest_float20e4);
  }
  // Depth/stencil and format reinterpretation require the same conversion
  // semantics as our Vulkan EDRAM path. Never approximate them as a color copy.
  if(s->spec.depth || d->spec.depth || s->spec.format!=d->spec.format ||
      s->spec.samples!=DkMsMode_1x || d->spec.samples!=DkMsMode_1x ||
      a.width!=b.width || a.height!=b.height || source==destination)return ImageTransfer::NeedsConversion;
  auto cmd=recorder_.comandos();dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
  dkCmdBufCopyImage(cmd,&s->view,&a,&d->view,&b,0);
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
  ++d->version;return ImageTransfer::Recorded;
}
bool Images::ClearColor(uint64_t id,const DkImageRect& rect,const float rgba[4],uint32_t mask) {
  MarkUsed(id);
  auto* i=Find(id);
  if(!i || !recorder_.TrabajoAbierto() || i->spec.depth || !i->spec.render_target ||
      !RectInside(*i,rect) || !rgba || (mask&~15u))return false;
  auto cmd=recorder_.comandos();const DkImageView* views[]={&i->view};
  dkCmdBufBindRenderTargets(cmd,views,1,nullptr);
  const DkScissor scissor{rect.x,rect.y,rect.width,rect.height};dkCmdBufSetScissors(cmd,0,&scissor,1);
  dkCmdBufClearColor(cmd,0,mask,rgba);
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
  ++i->version;return true;
}
bool Images::ClearDepth(uint64_t id,const DkImageRect& rect,bool depth,float value,uint8_t stencil_mask,uint8_t stencil) {
  MarkUsed(id);
  auto* i=Find(id);
  if(!i || !recorder_.TrabajoAbierto() || !i->spec.depth || !RectInside(*i,rect) ||
      !std::isfinite(value) || value<0 || value>1 ||
      (stencil_mask && i->spec.format!=DkImageFormat_Z24S8 && i->spec.format!=DkImageFormat_ZF32_X24S8))return false;
  auto cmd=recorder_.comandos();dkCmdBufBindRenderTargets(cmd,nullptr,0,&i->view);
  const DkScissor scissor{rect.x,rect.y,rect.width,rect.height};dkCmdBufSetScissors(cmd,0,&scissor,1);
  dkCmdBufClearDepthStencil(cmd,depth,value,stencil_mask,stencil);
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
  ++i->version;return true;
}
bool Images::Texture(uint64_t id,uint32_t slot,uint32_t sampler_slot,const DkSampler& sampler,DkResHandle& handle) {
  handle=0;auto* image=Find(id);
  if(!image || !recorder_.TrabajoAbierto() || slot>=recorder_.config().descriptores_imagen ||
      sampler_slot>=recorder_.config().descriptores_sampler || transient_slots_.contains(slot))return false;
  auto existing=image_slots_.find(slot);
  if(existing!=image_slots_.end() && existing->second!=id)return false;
  DkSamplerDescriptor sd;dkSamplerDescriptorInitialize(&sd,&sampler);
  auto previous=sampler_slots_.find(sampler_slot);
  if(previous!=sampler_slots_.end() && std::memcmp(&previous->second,&sd,sizeof(sd)))return false;
  auto cmd=recorder_.comandos();
  if(existing==image_slots_.end()) {
    DkImageDescriptor descriptor;dkImageDescriptorInitialize(&descriptor,&image->view,false,false);
    dkCmdBufPushData(cmd,recorder_.DescriptoresImagen()+uint64_t(slot)*sizeof(descriptor),&descriptor,sizeof(descriptor));
    image_slots_.emplace(slot,id);
  }
  if(previous==sampler_slots_.end()) {
    dkCmdBufPushData(cmd,recorder_.DescriptoresSampler()+uint64_t(sampler_slot)*sizeof(sd),&sd,sizeof(sd));
    sampler_slots_.emplace(sampler_slot,sd);
  }
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Descriptors);
  handle=dkMakeTextureHandle(slot,sampler_slot);return true;
}
bool Images::TransientViews(std::span<const DescriptorRequest> requests,std::span<DkResHandle> handles) {
  if(!recorder_.TrabajoAbierto() || !work_ || requests.empty() || requests.size()!=handles.size())return false;
  for(const auto& request:requests) {
    auto* image=Find(request.image);
    MarkUsed(request.image);
    if(!image || (request.view && request.view->pImage!=&image->image) ||
        (request.storage && !image->spec.storage) ||
        (request.aspect==DkDsSource_Stencil && image->spec.format!=DkImageFormat_Z24S8 &&
         image->spec.format!=DkImageFormat_ZF32_X24S8))return false;
  }
  RetiredViews retired;retired.work=work_;
  for(uint32_t slot=recorder_.config().descriptores_imagen;slot && retired.slots.size()<requests.size();) {
    --slot;if(!image_slots_.contains(slot))retired.slots.push_back(slot);
  }
  if(retired.slots.size()!=requests.size())return false;
  auto cmd=recorder_.comandos();
  for(size_t n=0;n<requests.size();++n) {
    const auto& request=requests[n];auto* image=Find(request.image);
    auto view=request.view?*request.view:image->view;view.dsSource=request.aspect;
    DkImageDescriptor descriptor;dkImageDescriptorInitialize(&descriptor,&view,request.storage,false);
    const uint32_t slot=retired.slots[n];
    dkCmdBufPushData(cmd,recorder_.DescriptoresImagen()+uint64_t(slot)*sizeof(descriptor),&descriptor,sizeof(descriptor));
    handles[n]=dkMakeImageHandle(slot);image_slots_[slot]=image->id;transient_slots_.insert(slot);
  }
  transient_views_.push_back(std::move(retired));
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Descriptors);
  return true;
}
bool Images::Sampler(uint32_t slot,const DkSampler& sampler) {
  if(!recorder_.TrabajoAbierto() || slot>=recorder_.config().descriptores_sampler)return false;
  DkSamplerDescriptor descriptor;dkSamplerDescriptorInitialize(&descriptor,&sampler);
  const auto previous=sampler_slots_.find(slot);
  if(previous!=sampler_slots_.end())return !std::memcmp(&previous->second,&descriptor,sizeof(descriptor));
  auto cmd=recorder_.comandos();
  dkCmdBufPushData(cmd,recorder_.DescriptoresSampler()+uint64_t(slot)*sizeof(descriptor),&descriptor,sizeof(descriptor));
  sampler_slots_.emplace(slot,descriptor);
  dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Descriptors);return true;
}
} // namespace green_lantern::deko
