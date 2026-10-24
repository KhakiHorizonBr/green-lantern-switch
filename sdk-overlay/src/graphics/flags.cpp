/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/graphics/flags.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <rex/ui/renderdoc_api.h>

#include <rex/chrono/clock.h>
#include <rex/math.h>

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

REXCVAR_DEFINE_BOOL(gpu_allow_invalid_fetch_constants, false, "GPU",
                    "Allow invalid fetch constants");
REXCVAR_DEFINE_BOOL(native_2x_msaa, true, "GPU", "Enable native 2x MSAA");
REXCVAR_DEFINE_BOOL(depth_float24_round, false, "GPU", "Round float24 depth values");
REXCVAR_DEFINE_BOOL(depth_float24_convert_in_pixel_shader, false, "GPU",
                    "Convert float24 depth in pixel shader");
REXCVAR_DEFINE_BOOL(depth_transfer_not_equal_test, true, "GPU",
                    "Use not-equal test for depth transfer");
REXCVAR_DEFINE_BOOL(gamma_render_target_as_unorm16, true, "GPU",
                    "Use R16G16B16A16_UNORM for gamma render targets (more accurate than sRGB)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(dump_shaders, "", "GPU", "Path to dump shaders to");
REXCVAR_DEFINE_STRING(gpu_compare_ps_hash, "", "GPU",
                     "Opt-in bounded CPU draw-state snapshots for a hexadecimal guest pixel shader hash");
REXCVAR_DEFINE_STRING(gpu_compare_resolve_address, "", "GPU",
                     "Opt-in four synchronized GPU resolve snapshots at a hexadecimal guest address");
REXCVAR_DEFINE_UINT32(gpu_capture_frame, 0, "Development",
    "Automatically capture one complete GPU frame (desktop comparison; 0 disables)");
REXCVAR_DEFINE_BOOL(use_fuzzy_alpha_epsilon, false, "GPU",
                    "Use approximate compare for alpha test values to prevent "
                    "flickering on NVIDIA graphics cards");
REXCVAR_DEFINE_BOOL(gpu_debug_markers, false, "GPU",
                    "Insert debug markers into GPU command streams for tools "
                    "like PIX and RenderDoc. Automatically enabled when "
                    "RenderDoc is detected.");

// Native residency. The master switch and every subsystem switch default to
// the emulated path, so an unconfigured run is bit-identical to a build
// without this patch.
REXCVAR_DEFINE_BOOL(gpu_watch_rearm_oracle, false, "GPU",
                    "MEASUREMENT ONLY - skip re-arming the GPU write watch in MakeRangeValid. "
                    "Renders stale geometry by design; exists to bound what a write-watch "
                    "redesign could recover before one is built");

REXCVAR_DEFINE_BOOL(gpu_native_residency, false, "GPU",
                    "Make draw vertex/index data resident by copying it from guest memory "
                    "instead of page-watching it. Latched once per frame at swap.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(gpu_native_index, true, "GPU",
                    "Use the native residency path for index buffers (requires "
                    "gpu_native_residency)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(gpu_native_vertex, true, "GPU",
                    "Use the native residency path for vertex buffers (requires "
                    "gpu_native_residency)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(gpu_native_dedup, true, "GPU",
                    "Skip native uploads whose range fingerprint is unchanged since the last "
                    "frame that referenced it (off = hash and copy on every request)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(gpu_native_verify, false, "GPU",
                    "Re-hash native ranges on a dedup hit and warn plus re-upload on a "
                    "mismatch (intra-frame rewrite oracle, expensive)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(gpu_native_prefetch, false, "GPU",
                    "EXPERIMENTAL - upload last frame's vertex working set at the next swap "
                    "so transfer barriers land before the render pass. Every range is "
                    "fingerprinted again before its first draw.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(gpu_native_hash_max_bytes, 16777216, "GPU",
                      "Ranges larger than this take the emulated path instead of being "
                      "hashed and copied")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// The other end of the same axis, and the one that decides whether a range is
// fingerprinted at all rather than whether this path owns it. A range shorter
// than this is made resident by copying it every frame with no fingerprint:
// still deduplicated within a frame by the epoch stamp, still invalidated by
// the buffer's page write sequence, still refused over a resolve destination -
// only the cross-frame "have these bytes changed" compare is dropped, and the
// copy that compare would have skipped is done unconditionally instead. That is
// correct by construction, because the bytes copied are the same bytes the
// emulated path would have uploaded. Worth doing wherever the fingerprint
// rarely saves a copy: measured on this title's idle frames, hashing 3.4 MB
// prevented under 1 MB of uploads.
//
// Default 0 means no range qualifies, so an unconfigured run hashes exactly
// what it hashed before this cvar existed. Setting it above
// gpu_native_hash_max_bytes makes every range this path owns copy-always.
// gpu_native_verify overrides it to 0 for the frames it is set, so the oracle
// always has fingerprints to compare.
REXCVAR_DEFINE_UINT32(gpu_native_hash_min_bytes, 0, "GPU",
                      "Ranges smaller than this are copied every frame instead of being "
                      "fingerprinted (0 = fingerprint everything; above "
                      "gpu_native_hash_max_bytes = never fingerprint)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(gpu_native_stats, false, "GPU",
                    "Collect and log native-residency telemetry: per-frame CP drain, "
                    "unique vertex bytes, shared page overlaps, resolve destinations")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(gpu_native_suppress_draws, false, "GPU",
                    "Diagnostic only: drop every draw at the top of IssueDraw to measure the "
                    "frame rate ceiling of the command processor. Output is garbage.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

bool IsGpuDebugMarkersEnabled() {
  static bool cached = false;
  static bool result = false;
  if (!cached) {
    cached = true;
    if (REXCVAR_GET(gpu_debug_markers)) {
      result = true;
      REXLOG_INFO("GPU debug markers enabled via CVar");
    } else {
      auto renderdoc_api = rex::ui::RenderDocAPI::CreateIfConnected();
      if (renderdoc_api) {
        result = true;
        REXLOG_INFO("GPU debug markers auto-enabled (RenderDoc detected)");
      }
    }
  }
  return result;
}

namespace rex::graphics::native_telemetry {

namespace {

// The shared memory buffer the ranges below index into is 512 MiB of guest
// physical memory (SharedMemory::kBufferSize); page granularity for the
// overlap test is the 4 KiB host page.
constexpr uint32_t kBufferSize = 512u * 1024u * 1024u;
constexpr uint32_t kPageSizeLog2 = 12;
constexpr uint32_t kPageCount = kBufferSize >> kPageSizeLog2;
constexpr size_t kPageWords = kPageCount / 64;

struct State {
  // Per-frame: which distinct (start, length) ranges a vertex fetch asked for,
  // and the pages those ranges and the frame's texture requests cover.
  std::unordered_set<uint64_t> vertex_ranges;
  std::vector<uint64_t> vertex_pages = std::vector<uint64_t>(kPageWords, 0);
  std::vector<uint64_t> texture_pages = std::vector<uint64_t>(kPageWords, 0);
  int64_t unique_vertex_bytes = 0;

  // Session lifetime: every distinct resolve destination seen so far, and how
  // many of them landed on a page some vertex fetch also read. The design
  // routes any such range to the emulated path unconditionally; this is the
  // measurement that says whether that guard is ever hot.
  std::unordered_set<uint64_t> resolve_ranges;
  std::vector<uint64_t> vertex_pages_session = std::vector<uint64_t>(kPageWords, 0);
  uint64_t resolve_vertex_overlaps = 0;

  // ~1 Hz summary accumulation.
  uint64_t log_tick = 0;
  uint64_t frames = 0;
  int64_t native_draws = 0;
  int64_t fallback_draws = 0;
  int64_t native_seq_evictions = 0;
  int64_t make_range_valid_calls = 0;
  int64_t watch_rearms = 0;
  int64_t request_range_vertex_bytes = 0;
  int64_t request_range_texture_bytes = 0;
  int64_t unique_vertex_bytes_sum = 0;
  int64_t index_bytes = 0;
  int64_t shared_page_overlaps = 0;
  int64_t drain_us_sum = 0;
  int64_t drain_us_max = 0;
  uint64_t drain_samples = 0;
};

State& state() {
  static State s;
  return s;
}

// Returns false when the range is empty or falls outside the shared memory
// buffer, in which case there is nothing meaningful to record.
bool ClampRange(uint32_t& start, uint32_t& length) {
  if (!length || start >= kBufferSize) {
    return false;
  }
  length = std::min(length, kBufferSize - start);
  return true;
}

void MarkPages(std::vector<uint64_t>& pages, uint32_t start, uint32_t length) {
  uint32_t page_first = start >> kPageSizeLog2;
  uint32_t page_last = (start + length - 1) >> kPageSizeLog2;
  for (uint32_t page = page_first; page <= page_last; ++page) {
    pages[page >> 6] |= uint64_t(1) << (page & 63);
  }
}

bool AnyPageSet(const std::vector<uint64_t>& pages, uint32_t start, uint32_t length) {
  uint32_t page_first = start >> kPageSizeLog2;
  uint32_t page_last = (start + length - 1) >> kPageSizeLog2;
  for (uint32_t page = page_first; page <= page_last; ++page) {
    if (pages[page >> 6] & (uint64_t(1) << (page & 63))) {
      return true;
    }
  }
  return false;
}

constexpr uint64_t RangeKey(uint32_t start, uint32_t length) {
  return uint64_t(start) | (uint64_t(length) << 32);
}

}  // namespace

void NoteVertexRange(uint32_t start, uint32_t length) {
  if (!ClampRange(start, length)) {
    return;
  }
  State& s = state();
  if (!s.vertex_ranges.insert(RangeKey(start, length)).second) {
    return;
  }
  s.unique_vertex_bytes += length;
  MarkPages(s.vertex_pages, start, length);
  MarkPages(s.vertex_pages_session, start, length);
}

void NoteIndexRange(uint32_t start, uint32_t length) {
  if (!ClampRange(start, length)) {
    return;
  }
  // Index data shares the vertex page set: an index buffer sitting on a
  // watched texture page is the same blind spot as a vertex buffer doing so.
  State& s = state();
  MarkPages(s.vertex_pages, start, length);
  MarkPages(s.vertex_pages_session, start, length);
}

void NoteTextureRange(uint32_t start, uint32_t length) {
  if (!ClampRange(start, length)) {
    return;
  }
  MarkPages(state().texture_pages, start, length);
}

void NoteResolveDestination(uint32_t start, uint32_t length) {
  if (!ClampRange(start, length)) {
    return;
  }
  State& s = state();
  if (!s.resolve_ranges.insert(RangeKey(start, length)).second) {
    return;
  }
  bool hits_vertex_data = AnyPageSet(s.vertex_pages_session, start, length);
  if (hits_vertex_data) {
    ++s.resolve_vertex_overlaps;
  }
  REXLOG_INFO(
      "gpu_native_stats: resolve destination 0x{:08X} + 0x{:X} bytes ({} unique so far, "
      "vertex-range overlap: {})",
      start, length, s.resolve_ranges.size(), hits_vertex_data ? "YES" : "no");
}

void EndFrame(int64_t cp_drain_us) {
  State& s = state();

  uint32_t overlap_pages = 0;
  for (size_t i = 0; i < kPageWords; ++i) {
    overlap_pages += static_cast<uint32_t>(
        rex::bit_count(s.vertex_pages[i] & s.texture_pages[i]));
  }

  PERF_counter_set(kNativeUniqueVertexBytes, s.unique_vertex_bytes);
  PERF_counter_set(kSharedPageOverlaps, static_cast<int64_t>(overlap_pages));

  s.vertex_ranges.clear();
  std::fill(s.vertex_pages.begin(), s.vertex_pages.end(), uint64_t(0));
  std::fill(s.texture_pages.begin(), s.texture_pages.end(), uint64_t(0));
  int64_t unique_vertex_bytes = s.unique_vertex_bytes;
  s.unique_vertex_bytes = 0;

  // The frame's accumulators were snapshotted by Profiler::Flip() at the top
  // of the swap packet, so the snapshot buffer is exactly the frame that just
  // ended. The two values computed above were set after that snapshot and are
  // therefore carried in locally rather than read back.
  using rex::perf::CounterId;
  using rex::perf::GetSnapshotCounter;
  ++s.frames;
  // kNativeDraws, not kDrawCalls: this line pairs draws with fallback draws,
  // both of which the native residency path totals once per draw in EndDraw.
  // kDrawCalls is the backend's own draw counter and is only meaningful on the
  // backend that increments it, so reading it here printed "draws 0" for every
  // Vulkan run while frames.csv recorded ~600 native draws a frame. Every name
  // in this line now matches its frames.csv column and its CounterId.
  s.native_draws += GetSnapshotCounter(CounterId::kNativeDraws);
  s.fallback_draws += GetSnapshotCounter(CounterId::kNativeFallbackDraws);
  s.native_seq_evictions += GetSnapshotCounter(CounterId::kNativeSeqEvictions);
  s.make_range_valid_calls += GetSnapshotCounter(CounterId::kMakeRangeValidCalls);
  s.watch_rearms += GetSnapshotCounter(CounterId::kWatchRearms);
  s.request_range_vertex_bytes += GetSnapshotCounter(CounterId::kRequestRangeVertexBytes);
  s.request_range_texture_bytes += GetSnapshotCounter(CounterId::kRequestRangeTextureBytes);
  s.index_bytes += GetSnapshotCounter(CounterId::kNativeIndexBytes);
  s.unique_vertex_bytes_sum += unique_vertex_bytes;
  s.shared_page_overlaps += static_cast<int64_t>(overlap_pages);
  if (cp_drain_us >= 0) {
    s.drain_us_sum += cp_drain_us;
    s.drain_us_max = std::max(s.drain_us_max, cp_drain_us);
    ++s.drain_samples;
  }

  uint64_t now = rex::chrono::Clock::QueryHostTickCount();
  uint64_t frequency = rex::chrono::Clock::QueryHostTickFrequency();
  if (!s.log_tick) {
    s.log_tick = now;
    return;
  }
  if (now - s.log_tick < frequency) {
    return;
  }
  s.log_tick = now;

  // Two halves: per-frame averages for a human reading the log, then the raw
  // window totals as key=value so post-processing does not have to unpick
  // rounded prose.
  double frames = static_cast<double>(s.frames);
  REXLOG_INFO(
      "gpu_native_stats: {} frames | native draws {:.0f} (fallback {:.0f}) | seq evictions "
      "{:.1f} | drain avg {:.2f} ms "
      "max {:.2f} ms | MakeRangeValid {:.0f} | watch rearms {:.0f} | RequestRange KiB "
      "vtx {:.0f} tex {:.0f} | unique vtx KiB {:.0f} | index KiB {:.0f} | shared pages "
      "{:.1f} | resolve dests {} (vtx overlaps {}) | "
      "frames={} native_draws={} native_fallback_draws={} native_seq_evictions={} "
      "make_range_valid_calls={} watch_rearms={} "
      "request_range_vertex_bytes={} request_range_texture_bytes={} "
      "native_unique_vertex_bytes={} native_index_bytes={} shared_page_overlaps={} "
      "cp_drain_us_sum={} cp_drain_us_max={} cp_drain_samples={} resolve_dests={} "
      "resolve_vertex_overlaps={}",
      s.frames, s.native_draws / frames, s.fallback_draws / frames,
      s.native_seq_evictions / frames,
      s.drain_samples ? double(s.drain_us_sum) / double(s.drain_samples) / 1000.0 : 0.0,
      double(s.drain_us_max) / 1000.0, s.make_range_valid_calls / frames,
      s.watch_rearms / frames, s.request_range_vertex_bytes / frames / 1024.0,
      s.request_range_texture_bytes / frames / 1024.0,
      s.unique_vertex_bytes_sum / frames / 1024.0, s.index_bytes / frames / 1024.0,
      s.shared_page_overlaps / frames, s.resolve_ranges.size(), s.resolve_vertex_overlaps,
      s.frames, s.native_draws, s.fallback_draws, s.native_seq_evictions,
      s.make_range_valid_calls, s.watch_rearms,
      s.request_range_vertex_bytes, s.request_range_texture_bytes, s.unique_vertex_bytes_sum,
      s.index_bytes, s.shared_page_overlaps, s.drain_us_sum, s.drain_us_max, s.drain_samples,
      s.resolve_ranges.size(), s.resolve_vertex_overlaps);

  s.frames = 0;
  s.native_draws = 0;
  s.fallback_draws = 0;
  s.native_seq_evictions = 0;
  s.make_range_valid_calls = 0;
  s.watch_rearms = 0;
  s.request_range_vertex_bytes = 0;
  s.request_range_texture_bytes = 0;
  s.unique_vertex_bytes_sum = 0;
  s.index_bytes = 0;
  s.shared_page_overlaps = 0;
  s.drain_us_sum = 0;
  s.drain_us_max = 0;
  s.drain_samples = 0;
}

}  // namespace rex::graphics::native_telemetry
