/**
 ******************************************************************************
 * ReXGlue : Xenos GPU plugin - bounded frame-sequence draw census           *
 ******************************************************************************
 * Released under the BSD license - see LICENSE in the root for more details. *
 */

#include "draw_census.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/version.h>

#if REX_PLATFORM_WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

REXCVAR_DEFINE_STRING(gpu_census_dir, "", "GPU/Vulkan",
                      "Directory for draw census captures");
REXCVAR_DEFINE_STRING(gpu_census_tag, "", "GPU/Vulkan",
                      "Build identifier stored in draw census headers");
REXCVAR_DEFINE_INT32(gpu_census_capacity_draws, 4096, "GPU/Vulkan",
                     "Maximum draw records in one draw census capture");
REXCVAR_DEFINE_INT32(gpu_census_sequence_frames, 1, "GPU/Vulkan",
                     "Consecutive frames written by one draw census capture request (1-8)");

// Defined by rexcore. The GPU plugin reaches it through the import library.
REXCVAR_DECLARE(std::string, perf_log_csv);

namespace rex::graphics::vulkan::draw_census {
namespace {

std::atomic<uint32_t> g_requests_pending{0};
std::atomic<uint32_t> g_requested{0};
std::atomic<uint32_t> g_started{0};
std::atomic<uint32_t> g_completed{0};
std::atomic<uint32_t> g_dropped{0};
std::atomic<uint32_t> g_truncated{0};
std::mutex g_last_path_mutex;
std::string g_last_path = "-";

uint32_t ProcessId() {
#if REX_PLATFORM_WIN32
  return uint32_t(_getpid());
#else
  return uint32_t(getpid());
#endif
}

void SetLastPath(const std::filesystem::path& path) {
  std::lock_guard<std::mutex> lock(g_last_path_mutex);
  g_last_path = path.string();
}

void LogStatus() {
  std::string last_path;
  {
    std::lock_guard<std::mutex> lock(g_last_path_mutex);
    last_path = g_last_path;
  }
  REXGPU_INFO(
      "draw-census: requested={} started={} completed={} dropped={} truncated={} last_path={}",
      g_requested.load(std::memory_order_relaxed), g_started.load(std::memory_order_relaxed),
      g_completed.load(std::memory_order_relaxed), g_dropped.load(std::memory_order_relaxed),
      g_truncated.load(std::memory_order_relaxed), last_path);
}

void RequestCapture() {
  g_requested.fetch_add(1, std::memory_order_relaxed);
  g_requests_pending.fetch_add(1, std::memory_order_release);
}

}  // namespace

REXCVAR_DEFINE_COMMAND(gpu_census_capture, RequestCapture, "GPU/Vulkan",
                       "Capture the configured complete guest GPU frame sequence");
REXCVAR_DEFINE_COMMAND(gpu_census_status, LogStatus, "GPU/Vulkan",
                       "Log draw census request and completion counters");

bool HasPendingRequest() { return g_requests_pending.load(std::memory_order_acquire) != 0; }

bool Recorder::Append(const void* data, size_t size, bool reserve_trailer) {
  const size_t trailer_reserve = reserve_trailer ? sizeof(TrailerRecord) : 0;
  if (size > max_buffer_bytes_ || buffer_.size() > max_buffer_bytes_ - size ||
      buffer_.size() + size > max_buffer_bytes_ - trailer_reserve) {
    MarkTruncated();
    return false;
  }
  const size_t offset = buffer_.size();
  buffer_.resize(offset + size);
  std::memcpy(buffer_.data() + offset, data, size);
  return true;
}

void Recorder::MarkTruncated() {
  if (!capture_truncated_) {
    capture_truncated_ = true;
    g_truncated.fetch_add(1, std::memory_order_relaxed);
  }
}

bool Recorder::Start(uint32_t title_id, uint64_t guest_swap_ordinal,
                     uint64_t guest_vblank_counter, uint64_t vk_frame_current,
                     uint64_t vk_submission, uint32_t mode_flags) {
  const int32_t configured_capacity = REXCVAR_GET(gpu_census_capacity_draws);
  capacity_draws_ = uint32_t(std::max(configured_capacity, 1));
  max_buffer_bytes_ = sizeof(HeaderRecord) +
                      size_t(capacity_draws_) * sizeof(DrawRecord) +
                      size_t(1) * 1024 * 1024;

  buffer_.clear();
  shaders_seen_.clear();
  try {
    buffer_.reserve(max_buffer_bytes_);
    shaders_seen_.reserve(256);
  } catch (const std::bad_alloc&) {
    buffer_.clear();
    shaders_seen_.clear();
    g_dropped.fetch_add(1, std::memory_order_relaxed);
    REXGPU_ERROR("draw-census: unable to allocate {} bytes for capture",
                 max_buffer_bytes_);
    return false;
  }

  capture_truncated_ = false;
  event_ordinal_ = 0;
  draws_recorded_ = 0;
  draws_skipped_ = 0;
  copies_ = 0;
  shaders_ = 0;
  overflow_vertex_ = 0;
  overflow_texture_ = 0;
  overflow_sampler_ = 0;
  guest_swap_ordinal_open_ = guest_swap_ordinal;

  std::filesystem::path directory = REXCVAR_GET(gpu_census_dir);
  if (directory.empty()) {
    const std::filesystem::path perf_path = REXCVAR_GET(perf_log_csv);
    directory = perf_path.parent_path();
  }
  char file_name[64];
  std::snprintf(file_name, sizeof(file_name), "census_%08x_%08llu.dcen", title_id,
                static_cast<unsigned long long>(guest_swap_ordinal));
  output_path_ = directory / file_name;

  HeaderRecord header{};
  header.prefix = {kRecordHeader, uint16_t(sizeof(HeaderRecord)), 0};
  std::memcpy(header.magic, "THP8DCEN", sizeof(header.magic));
  header.schema_version = kSchemaVersion;
  header.header_size = sizeof(HeaderRecord);
  std::snprintf(header.sdk_version, sizeof(header.sdk_version), "%s", REXGLUE_VERSION_STRING);
  std::snprintf(header.census_tag, sizeof(header.census_tag), "%s",
                REXCVAR_GET(gpu_census_tag).c_str());
  header.title_id = title_id;
  header.pid = ProcessId();
  header.unix_time_ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
  header.guest_swap_ordinal = guest_swap_ordinal;
  header.vk_frame_current = vk_frame_current;
  header.vk_submission = vk_submission;
  header.mode_flags = mode_flags;
  header.capacity_draws = capacity_draws_;
  header.draw_record_size = sizeof(DrawRecord);
  header.shader_record_size = sizeof(ShaderRecord);
  header.copy_record_size = sizeof(CopyRecord);
  header.trailer_size = sizeof(TrailerRecord);
  header.reserved[0] = uint32_t(guest_vblank_counter);
  header.reserved[1] = uint32_t(guest_vblank_counter >> 32);

  recording_ = true;
  if (!Append(&header, sizeof(header))) {
    recording_ = false;
    g_dropped.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  g_started.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void Recorder::Complete(uint64_t guest_swap_ordinal_close, int64_t draw_calls_snapshot,
                        bool release_storage) {
  const uint32_t completed = g_completed.fetch_add(1, std::memory_order_relaxed) + 1;

  TrailerRecord trailer{};
  trailer.prefix = {kRecordTrailer, uint16_t(sizeof(TrailerRecord)), event_ordinal_};
  trailer.draws_recorded = draws_recorded_;
  trailer.draws_skipped = draws_skipped_;
  trailer.copies = copies_;
  trailer.shaders = shaders_;
  trailer.requested = g_requested.load(std::memory_order_relaxed);
  trailer.started = g_started.load(std::memory_order_relaxed);
  trailer.completed = completed;
  trailer.dropped = g_dropped.load(std::memory_order_relaxed);
  trailer.truncated = g_truncated.load(std::memory_order_relaxed);
  trailer.overflow_vertex = overflow_vertex_;
  trailer.overflow_texture = overflow_texture_;
  trailer.overflow_sampler = overflow_sampler_;
  trailer.draw_calls_snapshot = draw_calls_snapshot;
  trailer.bytes_written = buffer_.size() + sizeof(TrailerRecord);
  trailer.guest_swap_ordinal_close = guest_swap_ordinal_close;
  Append(&trailer, sizeof(trailer), false);

  std::error_code directory_error;
  if (!output_path_.parent_path().empty()) {
    std::filesystem::create_directories(output_path_.parent_path(), directory_error);
  }
  bool wrote_file = false;
  if (!directory_error) {
    if (std::FILE* file = std::fopen(output_path_.string().c_str(), "wb")) {
      wrote_file = std::fwrite(buffer_.data(), 1, buffer_.size(), file) == buffer_.size();
      wrote_file = std::fclose(file) == 0 && wrote_file;
    }
  }
  SetLastPath(output_path_);
  if (!wrote_file) {
    REXGPU_ERROR("draw-census: failed to write {}", output_path_.string());
  }

  REXGPU_INFO(
      "draw-census: completed swap={} draws={} skipped={} copies={} shaders={} bytes={} "
      "truncated={} path={}",
      guest_swap_ordinal_open_, draws_recorded_, draws_skipped_, copies_, shaders_,
      buffer_.size(), capture_truncated_ ? 1 : 0, output_path_.string());

  recording_ = false;
  capture_truncated_ = false;
  buffer_.clear();
  shaders_seen_.clear();
  if (release_storage) {
    buffer_.shrink_to_fit();
    shaders_seen_.shrink_to_fit();
  }
}

bool Recorder::OnSwap(uint32_t title_id, uint64_t guest_swap_ordinal,
                      uint64_t guest_vblank_counter, uint64_t vk_frame_current,
                      uint64_t vk_submission, uint32_t mode_flags,
                      int64_t draw_calls_snapshot) {
  if (recording_) {
    // Requests made while the capture frame was open are deliberately not
    // queued behind its already-latched sequence.
    const uint32_t requests_during_capture =
        g_requests_pending.exchange(0, std::memory_order_acq_rel);
    if (requests_during_capture) {
      g_dropped.fetch_add(requests_during_capture, std::memory_order_relaxed);
    }
    const bool continue_sequence = sequence_frames_remaining_ != 0;
    Complete(guest_swap_ordinal, draw_calls_snapshot, !continue_sequence);
    if (continue_sequence) {
      if (Start(title_id, guest_swap_ordinal, guest_vblank_counter, vk_frame_current,
                vk_submission, mode_flags)) {
        --sequence_frames_remaining_;
        return true;
      } else {
        sequence_frames_remaining_ = 0;
      }
    }
    return false;
  }

  const uint32_t requests = g_requests_pending.exchange(0, std::memory_order_acq_rel);
  if (!requests) {
    return false;
  }
  if (requests > 1) {
    g_dropped.fetch_add(requests - 1, std::memory_order_relaxed);
  }
  sequence_frames_remaining_ =
      uint32_t(std::clamp(REXCVAR_GET(gpu_census_sequence_frames), 1, 8));
  if (Start(title_id, guest_swap_ordinal, guest_vblank_counter, vk_frame_current,
            vk_submission, mode_flags)) {
    --sequence_frames_remaining_;
    return true;
  } else {
    sequence_frames_remaining_ = 0;
    return false;
  }
}

bool Recorder::RecordShader(ShaderRecord record) {
  if (!IsRecording()) {
    return false;
  }
  const std::pair<uint64_t, uint8_t> identity{record.ucode_hash, record.shader_type};
  if (std::find(shaders_seen_.begin(), shaders_seen_.end(), identity) != shaders_seen_.end()) {
    return true;
  }
  record.prefix = {kRecordShader, uint16_t(sizeof(ShaderRecord)), event_ordinal_};
  if (!Append(&record, sizeof(record))) {
    return false;
  }
  shaders_seen_.push_back(identity);
  ++shaders_;
  return true;
}

bool Recorder::RecordDraw(DrawRecord record) {
  if (!IsRecording()) {
    return false;
  }
  if (draws_recorded_ >= capacity_draws_) {
    MarkTruncated();
    return false;
  }
  record.prefix = {kRecordDraw, uint16_t(sizeof(DrawRecord)), event_ordinal_};
  record.draw_ordinal = draws_recorded_;
  if (!Append(&record, sizeof(record))) {
    return false;
  }
  if (record.flags & kDrawVertexBindingOverflow) {
    ++overflow_vertex_;
  }
  if (record.flags & kDrawTextureOverflow) {
    ++overflow_texture_;
  }
  if (record.flags & kDrawSamplerOverflow) {
    ++overflow_sampler_;
  }
  ++draws_recorded_;
  ++event_ordinal_;
  return true;
}

bool Recorder::RecordCopy(CopyRecord record) {
  if (!IsRecording()) {
    return false;
  }
  record.prefix = {kRecordCopy, uint16_t(sizeof(CopyRecord)), event_ordinal_};
  record.draws_before = draws_recorded_;
  if (!Append(&record, sizeof(record))) {
    return false;
  }
  ++copies_;
  ++event_ordinal_;
  return true;
}

void Recorder::NoteDrawSkipped() {
  if (IsRecording()) {
    ++draws_skipped_;
  }
}

}  // namespace rex::graphics::vulkan::draw_census
