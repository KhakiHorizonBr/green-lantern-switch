#pragma once
#include <rex/ui/deko3d/nucleo.h>
#include <deko3d.h>
#include <memory>
#include <array>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace green_lantern::deko {
enum class ImageUse:uint32_t {Texture,Target,Resolve,SizedResolve,DepthWords,Count};
class DepthResolve;
struct ImageSpec {
  ImageUse use=ImageUse::Texture;
  uint32_t guest_address=0,width=0,height=0,guest_pitch=0;
  DkImageFormat format=DkImageFormat_None;
  DkMsMode samples=DkMsMode_1x;
  bool depth=false,render_target=false;
  uint32_t levels=1,layers=1;
  bool cube=false;
  bool storage=false;
  bool guest_float20e4=false;
  uint32_t host_swizzle=0x688; // Xenos RGBA encoding, composed with fetch swizzle.
};
struct Image {
  uint64_t created_frame=0,last_used_frame=0;
  uint64_t id=0,version=0;
  ImageSpec spec;
  DkImage image{};
  DkImageView view{};
  rex::deko3d::BloqueImagen memory;
};
enum class ImageTransfer { Recorded, Invalid, NeedsConversion };
struct DescriptorRequest {
  uint64_t image=0;
  DkDsSource aspect=DkDsSource_Depth;
  bool storage=false;
  const DkImageView* view=nullptr;
};
// Owns host images independently from guest address identity: differently sized
// resolves and depth/color aliases never silently overwrite each other's view.
class Images {
 public:
  Images(rex::deko3d::Nucleo& recorder,uint64_t budget);
  ~Images();
  Images(const Images&)=delete;
  Images& operator=(const Images&)=delete;
  Image* Create(const ImageSpec& spec);
  // Call immediately AFTER Nucleo::EmpezarTrabajo with consecutive ids from 1.
  // Budget credit is delayed until that allocator slot has actually been reused.
  bool BeginWork(uint64_t work);
  Image* Find(uint64_t id);
  void MarkUsed(uint64_t id){if(auto* image=Find(id))image->last_used_frame=frame_;}
  bool Release(uint64_t id); // only during open work; memory freed after its fence
  ImageTransfer UploadLinear(uint64_t id,std::span<const uint8_t> data,uint32_t row_bytes);
  // Linear host blocks, one mip/layer at a time (compressed pitches in bytes).
  ImageTransfer UploadLevel(uint64_t id,uint32_t level,uint32_t layer,
      std::span<const uint8_t> data,uint32_t row_bytes);
  ImageTransfer Copy(uint64_t source,const DkImageRect& from,uint64_t destination,const DkImageRect& to);
  bool InitializeDepthResolve(std::span<const uint8_t> dksh);
  Image* PackedDepthWords(uint64_t resolved_depth);
  bool ClearColor(uint64_t id,const DkImageRect& rect,const float rgba[4],uint32_t mask=15);
  bool ClearDepth(uint64_t id,const DkImageRect& rect,bool depth,float value,uint8_t stencil_mask,uint8_t stencil);
  bool Texture(uint64_t id,uint32_t image_slot,uint32_t sampler_slot,
               const DkSampler& sampler,DkResHandle& handle);
  // Immutable views for this submission; slots return after its fence slot is
  // reused. Storage and sampling views have different descriptor flags.
  bool TransientViews(std::span<const DescriptorRequest> requests,std::span<DkResHandle> handles);
  bool Sampler(uint32_t slot,const DkSampler& sampler);
  uint64_t bytes()const{return bytes_;}
  void SetFrame(uint64_t frame){frame_=frame;}
  struct CategoryStats {uint64_t bytes=0,count=0,max_age_frames=0,created_total=0;};
  struct MemoryReport {std::array<CategoryStats,5> active{};uint64_t retired_bytes=0,retired_count=0,peak_bytes=0;};
  MemoryReport Report() const;
 private:
  rex::deko3d::Nucleo& recorder_;
  uint64_t budget_,bytes_=0,next_=0,work_=0,frame_=0,peak_bytes_=0;
  std::array<uint64_t,5> created_by_use_{};
  struct Retired {uint64_t work,bytes,id;};
  std::vector<Retired> retired_;
  struct RetiredViews {uint64_t work;std::vector<uint32_t> slots;};
  std::vector<RetiredViews> transient_views_;
  std::unordered_set<uint32_t> transient_slots_;
  std::unordered_map<uint64_t,std::unique_ptr<Image>> images_;
  // Image slots are immutable until their retired allocator slot is reclaimed.
  std::unordered_map<uint32_t,uint64_t> image_slots_;
  std::unordered_map<uint32_t,DkSamplerDescriptor> sampler_slots_;
  std::unique_ptr<DepthResolve> depth_resolve_;
  std::unordered_map<uint64_t,uint64_t> packed_depth_;
};
} // namespace green_lantern::deko
