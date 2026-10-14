#pragma once
/**
 ******************************************************************************
 * ReXGlue : Xenos GPU plugin - bounded frame-sequence draw census           *
 ******************************************************************************
 * Released under the BSD license - see LICENSE in the root for more details. *
 *
 * Authored instrumentation. The capture contains hashes and layout facts only:
 * no guest bytes, shader microcode, SPIR-V, textures, or render-target data.
 */

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace rex::graphics::vulkan::draw_census {

constexpr uint32_t kSchemaVersion = 1;
constexpr uint16_t kRecordHeader = 1;
constexpr uint16_t kRecordShader = 2;
constexpr uint16_t kRecordDraw = 3;
constexpr uint16_t kRecordCopy = 4;
constexpr uint16_t kRecordTrailer = 5;

constexpr uint32_t kModeNativeFrame = UINT32_C(1) << 0;
constexpr uint32_t kModeNativeIndex = UINT32_C(1) << 1;
constexpr uint32_t kModeNativeVertex = UINT32_C(1) << 2;
constexpr uint32_t kModeCpFastpath = UINT32_C(1) << 3;
constexpr uint32_t kModeSamplerSetReuse = UINT32_C(1) << 4;
constexpr uint32_t kModeTextureRequestReuse = UINT32_C(1) << 5;
constexpr uint32_t kModeTraceGpuStream = UINT32_C(1) << 6;

constexpr uint32_t kDrawIndexed = UINT32_C(1) << 0;
constexpr uint32_t kDrawMemexportVertex = UINT32_C(1) << 1;
constexpr uint32_t kDrawMemexportPixel = UINT32_C(1) << 2;
constexpr uint32_t kDrawRasterizationDone = UINT32_C(1) << 3;
constexpr uint32_t kDrawPixelShaderNeeded = UINT32_C(1) << 4;
constexpr uint32_t kDrawPrimitiveReset = UINT32_C(1) << 5;
constexpr uint32_t kDrawIndexNative = UINT32_C(1) << 6;
constexpr uint32_t kDrawVertexBindingOverflow = UINT32_C(1) << 7;
constexpr uint32_t kDrawTextureOverflow = UINT32_C(1) << 8;
constexpr uint32_t kDrawSamplerOverflow = UINT32_C(1) << 9;
constexpr uint32_t kDrawVizQueryKilled = UINT32_C(1) << 10;
// The native index upload path copies without fingerprinting. Keep the frozen
// field zero and make that absence explicit rather than treating zero as a
// stable generation.
constexpr uint32_t kDrawIndexHashUnavailable = UINT32_C(1) << 11;
// ExecutePacketType3_DRAW_INDX supplies the visibility-query condition above
// the backend IssueDraw interface, so H2 cannot recover whether it killed this
// invocation. Bit 10 stays zero and this bit documents the missing fact.
constexpr uint32_t kDrawVizQueryStateUnavailable = UINT32_C(1) << 12;

#pragma pack(push, 1)

struct RecordPrefix {
  uint16_t type;
  uint16_t size_bytes;
  uint32_t ordinal;
};

struct HeaderRecord {
  RecordPrefix prefix;
  char magic[8];
  uint32_t schema_version;
  uint32_t header_size;
  char sdk_version[64];
  char census_tag[64];
  uint32_t title_id;
  uint32_t pid;
  uint64_t unix_time_ns;
  uint64_t guest_swap_ordinal;
  uint64_t vk_frame_current;
  uint64_t vk_submission;
  uint32_t mode_flags;
  uint32_t capacity_draws;
  uint16_t draw_record_size;
  uint16_t shader_record_size;
  uint16_t copy_record_size;
  uint16_t trailer_size;
  // reserved[0..1] is the opening CommandProcessor::counter() value as a
  // little-endian u64. It counts vblanks as well as swap packets and is
  // informational only; guest_swap_ordinal is the IssueSwap-only ordinal.
  uint32_t reserved[8];
};

struct ShaderRecord {
  RecordPrefix prefix;
  uint64_t ucode_hash;
  uint8_t shader_type;
  uint8_t memexport;
  uint16_t vertex_binding_count;
  uint32_t ucode_dword_count;
  uint32_t float_count;
  uint64_t float_bitmap[4];
  uint32_t bool_bitmap[8];
  uint32_t loop_bitmap;
  uint32_t vertex_fetch_bitmap[3];
  uint32_t texture_mask;
  uint64_t vertex_layout_hash;
};

struct VertexBindingRecord {
  uint8_t fetch_constant;
  uint8_t endian;
  uint8_t residency_result;
  uint8_t hash_valid;
  uint32_t address_bytes;
  uint32_t size_bytes;
  uint32_t stride_words;
  uint64_t content_hash;
};

struct TextureRecord {
  uint8_t fetch_constant;
  uint8_t swizzled_signs;
  uint16_t pad;
  uint32_t host_swizzle;
  uint8_t key[16];
};

struct SamplerRecord {
  uint8_t fetch_constant;
  uint8_t stage;
  uint16_t pad;
  uint32_t parameters;
};

struct DrawRecord {
  RecordPrefix prefix;
  uint32_t draw_ordinal;
  uint64_t vk_submission;
  uint32_t flags;
  uint8_t guest_prim_type;
  uint8_t host_prim_type;
  uint8_t host_vs_type;
  uint8_t index_buffer_type;
  uint8_t index_format;
  uint8_t index_endian;
  uint8_t edram_mode;
  uint8_t msaa_samples;
  uint32_t guest_vertex_count;
  uint32_t host_vertex_count;
  uint32_t guest_index_base;
  uint32_t guest_index_bytes;
  uint64_t index_content_hash;
  uint64_t vs_hash;
  uint64_t vs_mod;
  uint64_t ps_hash;
  uint64_t ps_mod;
  uint64_t pipeline_desc_hash;
  uint32_t render_pass_key;
  uint32_t rb_surface_info;
  uint32_t rb_modecontrol;
  uint32_t rb_colorcontrol;
  uint32_t rb_depthcontrol_normalized;
  uint32_t rb_color_mask_normalized;
  uint32_t rb_color_info[4];
  uint32_t rb_depth_info;
  uint32_t rb_blendcontrol[4];
  uint32_t rb_stencilrefmask;
  uint32_t rb_stencilrefmask_bf;
  uint32_t rb_alpha_ref_bits;
  uint32_t pa_su_sc_mode_cntl;
  uint32_t pa_cl_vte_cntl;
  uint32_t pa_sc_window_scissor_tl;
  uint32_t pa_sc_window_scissor_br;
  uint32_t sq_program_cntl;
  uint32_t sq_context_misc;
  float vp_xy_offset[2];
  float vp_xy_extent[2];
  float vp_z_min;
  float vp_z_max;
  uint32_t sc_offset[2];
  uint32_t sc_extent[2];
  uint64_t float_dirty[8];
  uint32_t fetch_dirty[6];
  uint32_t boolloop_dirty_writes;
  uint64_t vs_const_block_hash;
  uint64_t ps_const_block_hash;
  uint32_t vs_reg_hash[256];
  uint32_t ps_reg_hash[256];
  uint32_t used_texture_mask;
  uint8_t vertex_binding_count;
  uint8_t texture_count;
  uint8_t sampler_count_vs;
  uint8_t sampler_count_ps;
  VertexBindingRecord vertex_bindings[16];
  TextureRecord textures[16];
  SamplerRecord samplers[16];
};

struct CopyRecord {
  RecordPrefix prefix;
  uint32_t draws_before;
  uint32_t rb_copy_control;
  uint32_t rb_copy_dest_info;
  uint32_t rb_copy_dest_base;
  uint32_t rb_copy_dest_pitch;
  uint32_t written_address;
  uint32_t written_length;
  uint32_t rb_surface_info;
  uint32_t rb_color_info0;
  uint32_t rb_depth_info;
  uint32_t rb_modecontrol;
  uint8_t readback_path;
  uint8_t pad[3];
};

struct TrailerRecord {
  RecordPrefix prefix;
  uint32_t draws_recorded;
  uint32_t draws_skipped;
  uint32_t copies;
  uint32_t shaders;
  uint32_t requested;
  uint32_t started;
  uint32_t completed;
  uint32_t dropped;
  uint32_t truncated;
  uint32_t overflow_vertex;
  uint32_t overflow_texture;
  uint32_t overflow_sampler;
  int64_t draw_calls_snapshot;
  uint64_t bytes_written;
  uint64_t guest_swap_ordinal_close;
};

#pragma pack(pop)

static_assert(sizeof(RecordPrefix) == 8);
static_assert(sizeof(HeaderRecord) == 240);
static_assert(sizeof(ShaderRecord) == 120);
static_assert(sizeof(VertexBindingRecord) == 24);
static_assert(sizeof(TextureRecord) == 24);
static_assert(sizeof(SamplerRecord) == 8);
static_assert(sizeof(DrawRecord) == 3292);
static_assert(sizeof(CopyRecord) == 56);
static_assert(sizeof(TrailerRecord) == 80);

// The command callbacks use process-lifetime atomics. This query is the only
// normal-path boundary hook; it is one relaxed load at IssueSwap and false
// until a capture command is issued.
bool HasPendingRequest();

class Recorder {
 public:
  bool IsRecording() const { return recording_ && !capture_truncated_; }
  bool HasOpenCapture() const { return recording_; }

  // Returns true when this boundary opened a new capture frame. The caller
  // uses that signal to reset its per-frame dirty-state tracking, including
  // between members of one consecutive-frame sequence.
  bool OnSwap(uint32_t title_id, uint64_t guest_swap_ordinal,
              uint64_t guest_vblank_counter, uint64_t vk_frame_current,
              uint64_t vk_submission, uint32_t mode_flags, int64_t draw_calls_snapshot);

  bool RecordShader(ShaderRecord record);
  bool RecordDraw(DrawRecord record);
  bool RecordCopy(CopyRecord record);
  void NoteDrawSkipped();

 private:
  bool Start(uint32_t title_id, uint64_t guest_swap_ordinal,
             uint64_t guest_vblank_counter, uint64_t vk_frame_current,
             uint64_t vk_submission, uint32_t mode_flags);
  void Complete(uint64_t guest_swap_ordinal_close, int64_t draw_calls_snapshot,
                bool release_storage);
  bool Append(const void* data, size_t size, bool reserve_trailer = true);
  void MarkTruncated();

  bool recording_ = false;
  bool capture_truncated_ = false;
  uint32_t capacity_draws_ = 0;
  uint32_t event_ordinal_ = 0;
  uint32_t draws_recorded_ = 0;
  uint32_t draws_skipped_ = 0;
  uint32_t copies_ = 0;
  uint32_t shaders_ = 0;
  uint32_t overflow_vertex_ = 0;
  uint32_t overflow_texture_ = 0;
  uint32_t overflow_sampler_ = 0;
  uint32_t sequence_frames_remaining_ = 0;
  uint64_t guest_swap_ordinal_open_ = 0;
  size_t max_buffer_bytes_ = 0;
  std::filesystem::path output_path_;
  std::vector<uint8_t> buffer_;
  std::vector<std::pair<uint64_t, uint8_t>> shaders_seen_;
};

}  // namespace rex::graphics::vulkan::draw_census
