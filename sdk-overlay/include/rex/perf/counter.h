/**
 * @file        perf/counter.h
 * @brief       Performance counter registry and profiler
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstdint>
#include <string>

#ifdef REXGLUE_ENABLE_PROFILING
#include <tracy/Tracy.hpp>
#endif

namespace rex::perf {

enum class CounterId : uint16_t {
  // Frame
  kFrameTimeUs,
  kFps,

  // GPU
  kDrawCalls,
  kCommandBufferStalls,
  kVerticesProcessed,

  // Audio
  kXmaFramesDecoded,
  kAudioFrameLatencyUs,
  kBufferQueueDepth,

  // Dispatch
  kFunctionsDispatched,
  kInterruptDispatches,

  // Threading
  kActiveThreads,
  kApcQueueDepth,
  kCriticalRegionContentions,

  // Caches
  kTextureCacheHits,
  kTextureCacheMisses,
  kPipelineCacheHits,
  kPipelineCacheMisses,

  // Native residency telemetry.
  // Appended (never reordered) so the frames.csv column set only ever grows.
  // Everything here is written from the command-processor thread; the ones
  // that cost more than a relaxed atomic add are gated on `gpu_native_stats`.
  kNativeDraws,
  kNativeFallbackDraws,
  kNativeBytesHashed,
  kNativeBytesUploaded,
  kNativeUniqueVertexBytes,
  kNativeIndexBytes,
  kMakeRangeValidCalls,
  kWatchRearms,
  kRequestRangeVertexBytes,
  kRequestRangeTextureBytes,
  kSharedPageOverlaps,
  kCpDrainUs,

  // Host memory. Written from whichever guest thread took the fault.
  // Counts the benign racing-unwatch recoveries in the physical-memory access
  // violation handler, which used to be one warning per event -- this is the
  // canary for asymmetric watch state, tracked per frame instead of by grep.
  kStaleProtectRecoveries,

  // Native residency, second wave. APPEND-ONLY RULE: every new CounterId goes
  // here, directly before kCount, never inserted mid-enum - the frames.csv
  // column set is positional at write time, so an insert silently renames
  // every column after it in every previously written CSV. On any change to
  // this enum, re-check all six consumers: this header (installed - refresh
  // the prefix's include/ copy), kCounterNames and kIsGauge in
  // src/core/perf/counter.cpp (both static_assert-guarded against kCount),
  // the frames.csv emitter (trailing append only), the gpu_native_stats
  // printer in src/graphics/flags.cpp, the debug overlay in
  // src/ui/overlay/debug_overlay.cpp, and the residency report parser
  // (resolves by header name, so it only needs a new CounterSpec to report
  // the column).
  //
  // Counts fingerprint hits demoted to re-uploads because something else was
  // written into the shared memory buffer over the range since it was last
  // copied in - an overlapping native window or a page-granular emulated
  // upload. This is the attribution instrument for the overlapping-range
  // clobber: a steady nonzero rate says the hazard is live in real content, a
  // hard zero across a long soak says it never fires in this title.
  kNativeSeqEvictions,

  // GPU time for one frame, in microseconds, measured on the GPU itself with
  // a pair of Vulkan timestamp queries rather than inferred from wall clock.
  //
  // Exists because every other instrument here is CPU-side, and on an APU that
  // is only half the picture: a change that uploads fewer bytes gives the GPU
  // less to do as well as the CPU, and nothing in this table could tell those
  // apart. `gpu_busy_percent` is not a substitute - it reads 0 on Van Gogh even
  // mid-render.
  //
  // READ THIS BEFORE QUOTING IT. This is the SPAN from the frame's first GPU
  // command starting to its last completing, not GPU-busy time: if the GPU goes
  // idle mid-frame waiting for the CPU to submit more, that idle time is inside
  // the number. Measured on a Deck it reads 82-86% of frame time in arms whose
  // CPU cost differs by 3x, which is the giveaway.
  //
  // It also perturbs: enabling it cost ~8 ms/frame on one arm and ~14 ms on
  // another, enough to erase the difference between them. Use it to see whether
  // GPU work exists at all, never to compare two configurations.
  //
  // Zero when gpu_timestamp_queries is off (the default) or when the device
  // reports no timestampPeriod.
  kGpuFrameUs,

  // Draws whose shader writes a memory-export register
  // (`memexport_eM_written() != 0`). Two things follow from a draw being one,
  // and both are expensive: the native residency path is disarmed for it
  // (BeginDraw(!memexport_writes_possible)), and shared memory is Use()d for
  // read-WRITE rather than read, which is a barrier the GPU must honour.
  //
  // On THPS P8 this is ~11% of draws, and it is exactly the gap between
  // kDrawCalls and kNativeDraws - which is how the count was inferred before
  // this counter existed. It cannot be inferred once a draw can be excluded
  // from the native path for any other reason, hence the counter.
  kMemexportDraws,

  // The subset of those whose destination ranges did not resolve, so the
  // conservative path is taken: RequestRange over the WHOLE shared memory
  // buffer, and a read-write barrier over the whole buffer, per draw. If this
  // is nonzero in steady state it is the first thing to fix; if it is zero the
  // barriers are at least scoped to real extents.
  kMemexportUnresolved,

  // Vulkan render pass entries per frame. On this backend a barrier ENDS the
  // render pass (SubmitBarriers calls EndRenderPass whenever anything is
  // pending) and the next draw re-enters it. If this approaches the draw count
  // the GPU is tearing down and restoring the framebuffer per draw, which costs
  // thousands of clocks and produces no pixels - the signature being GPU time
  // that scales with clock but not with resolution.
  kRenderPassBegins,

  // vkCmdPipelineBarrier commands actually emitted per frame. Pairs with the
  // above: this is what forces those render pass ends.
  kVkPipelineBarriers,

  // Frame-ahead native vertex prefetch. Uploaded ranges prove the arm
  // engaged; hits and misses report the mandatory first-draw validation.
  kNativePrefetchRanges,
  kNativePrefetchHits,
  kNativePrefetchMisses,

  // Exact reuse of the immediately preceding non-empty texture/sampler
  // descriptor set in the same Vulkan submission. Collected only while
  // gpu_native_stats is enabled so the timing arm doesn't pay for telemetry.
  kTextureDescriptorSetAdjacentReuseLookups,
  kTextureDescriptorSetAdjacentReuseHits,
  kTextureDescriptorSetAdjacentReuseMisses,

  kCount  // sentinel -- must be last
};

// Returns human-readable name for a counter (e.g. "frame_time_us")
const char* CounterName(CounterId id);

// Set a counter to an absolute value
void SetCounter(CounterId id, int64_t value);

// Atomically add to a counter
void IncrementCounter(CounterId id, int64_t delta = 1);

// Read a counter's current live value
int64_t GetCounter(CounterId id);

// Snapshot current values into the read buffer and zero the live counters.
// Called once per frame by Profiler::Flip().
void ResetFrameCounters();

// Read a counter from the last-frame snapshot (stable between frames).
int64_t GetSnapshotCounter(CounterId id);

// Initialize the counter system (zeroes everything). Safe to call multiple times.
void Init();

// CSV logging
void SetCsvLogPath(const std::string& path);
void WriteCsvFrame();
void FlushCsv();

// Profiler -- coordinates Tracy frame marks and counter snapshots.
// Moved here from rex::debug to consolidate all perf code under rex::perf.
class Profiler {
 public:
  static void Startup() {
#ifdef REXGLUE_ENABLE_PROFILING
    if (!tracy::IsProfilerStarted())
      tracy::StartupProfiler();
#endif
  }
  static void OnThreadEnter(const char* name = nullptr) {
#ifdef REXGLUE_ENABLE_PROFILING
    if (name && tracy::IsProfilerStarted())
      tracy::SetThreadName(name);
#else
    (void)name;
#endif
  }
  static void OnThreadExit() {}
  static void ThreadEnter(const char* name = nullptr) { OnThreadEnter(name); }
  static void ThreadExit() {}
  static void Flip() {
#ifdef REXGLUE_ENABLE_PROFILING
    if (tracy::IsProfilerStarted()) {
      FrameMark;
    }
#endif
#ifdef REXGLUE_ENABLE_PERF_COUNTERS
    ResetFrameCounters();
    WriteCsvFrame();
#endif
  }
  static void Flush() {}
  static void Shutdown() {
#ifdef REXGLUE_ENABLE_PROFILING
    if (tracy::IsProfilerStarted())
      tracy::ShutdownProfiler();
#endif
#ifdef REXGLUE_ENABLE_PERF_COUNTERS
    FlushCsv();
#endif
  }
  static bool is_enabled() {
#ifdef REXGLUE_ENABLE_PROFILING
    return tracy::IsProfilerStarted();
#else
    return false;
#endif
  }
};

}  // namespace rex::perf

// Perf counter macros -- compile to no-ops when counters are disabled.
#ifdef REXGLUE_ENABLE_PERF_COUNTERS

// Generic helpers for easily adding new counters
#define PERF_counter_set(id, value) rex::perf::SetCounter(rex::perf::CounterId::id, value)
#define PERF_counter_inc(id) rex::perf::IncrementCounter(rex::perf::CounterId::id)
#define PERF_counter_add(id, delta) rex::perf::IncrementCounter(rex::perf::CounterId::id, delta)

// Purpose-specific macros so callsites stay clean
#define PROFILE_FRAME_TIME_US(value) PERF_counter_set(kFrameTimeUs, value)
#define PROFILE_FPS(value) PERF_counter_set(kFps, value)
#define PROFILE_FUNCTION_DISPATCHED() PERF_counter_inc(kFunctionsDispatched)
#define PROFILE_INTERRUPT_DISPATCHED() PERF_counter_inc(kInterruptDispatches)
#define PROFILE_XMA_FRAME_DECODED() PERF_counter_inc(kXmaFramesDecoded)
#define PROFILE_DRAW_CALL() PERF_counter_inc(kDrawCalls)
#define PROFILE_VERTICES(n) PERF_counter_add(kVerticesProcessed, n)
#define PROFILE_CMD_BUFFER_STALL() PERF_counter_inc(kCommandBufferStalls)
#define PROFILE_AUDIO_LATENCY_US(value) PERF_counter_set(kAudioFrameLatencyUs, value)
#define PROFILE_BUFFER_QUEUE_DEPTH(value) PERF_counter_set(kBufferQueueDepth, value)
#define PROFILE_THREAD_CREATED() PERF_counter_inc(kActiveThreads)
#define PROFILE_THREAD_EXITED() PERF_counter_add(kActiveThreads, -1)
#define PROFILE_APC_QUEUE_DEPTH(value) PERF_counter_set(kApcQueueDepth, value)
#define PROFILE_CRITICAL_REGION_CONTENTION() PERF_counter_inc(kCriticalRegionContentions)
#define PROFILE_TEXTURE_CACHE_HIT() PERF_counter_inc(kTextureCacheHits)
#define PROFILE_TEXTURE_CACHE_MISS() PERF_counter_inc(kTextureCacheMisses)
#define PROFILE_PIPELINE_CACHE_HIT() PERF_counter_inc(kPipelineCacheHits)
#define PROFILE_PIPELINE_CACHE_MISS() PERF_counter_inc(kPipelineCacheMisses)

#else

#define PERF_counter_set(id, value)
#define PERF_counter_inc(id)
#define PERF_counter_add(id, delta)

#define PROFILE_FRAME_TIME_US(value)
#define PROFILE_FPS(value)
#define PROFILE_FUNCTION_DISPATCHED()
#define PROFILE_INTERRUPT_DISPATCHED()
#define PROFILE_XMA_FRAME_DECODED()
#define PROFILE_DRAW_CALL()
#define PROFILE_VERTICES(n)
#define PROFILE_CMD_BUFFER_STALL()
#define PROFILE_AUDIO_LATENCY_US(value)
#define PROFILE_BUFFER_QUEUE_DEPTH(value)
#define PROFILE_THREAD_CREATED()
#define PROFILE_THREAD_EXITED()
#define PROFILE_APC_QUEUE_DEPTH(value)
#define PROFILE_CRITICAL_REGION_CONTENTION()
#define PROFILE_TEXTURE_CACHE_HIT()
#define PROFILE_TEXTURE_CACHE_MISS()
#define PROFILE_PIPELINE_CACHE_HIT()
#define PROFILE_PIPELINE_CACHE_MISS()

#endif
