#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include <rex/memory.h>
#include <rex/thread/mutex.h>

namespace rex::graphics {

// Manages memory for unconverted textures, resolve targets, vertex and index
// buffers that can be accessed from shaders with Xenon physical addresses, with
// system page size granularity.
class SharedMemory {
 public:
  static constexpr uint32_t kBufferSizeLog2 = 29;
  static constexpr uint32_t kBufferSize = 1 << kBufferSizeLog2;

  virtual ~SharedMemory();
  // Call in the implementation-specific ClearCache.
  virtual void ClearCache();
  void SetSystemPageBlocksValidWithGpuDataWritten();
  void InvalidateAllPages();

  typedef void (*GlobalWatchCallback)(const std::unique_lock<std::recursive_mutex>& global_lock,
                                      void* context, uint32_t address_first, uint32_t address_last,
                                      bool invalidated_by_gpu);
  typedef void* GlobalWatchHandle;
  // Registers a callback invoked when something is invalidated in the GPU
  // memory copy by the CPU or (if triggered explicitly - such as by a resolve)
  // by the GPU. It will be fired for writes to pages previously requested, but
  // may also be fired regardless of whether it was used by GPU emulation - for
  // example, if the game changes protection level of a memory range containing
  // the watched range.
  //
  // The callback is called within the global critical region.
  GlobalWatchHandle RegisterGlobalWatch(GlobalWatchCallback callback, void* callback_context);
  void UnregisterGlobalWatch(GlobalWatchHandle handle);
  typedef void (*WatchCallback)(const std::unique_lock<std::recursive_mutex>& global_lock,
                                void* context, void* data, uint64_t argument,
                                bool invalidated_by_gpu);
  typedef void* WatchHandle;
  // Registers a callback invoked when the specified memory range is invalidated
  // in the GPU memory copy by the CPU or (if triggered explicitly - such as by
  // a resolve) by the GPU. It will be fired for writes to pages previously
  // requested, but may also be fired regardless of whether it was used by GPU
  // emulation - for example, if the game changes protection level of a memory
  // range containing the watched range.
  //
  // Generally the context is the subsystem pointer (for example, the texture
  // cache), the data is the object (such as a texture), and the argument is
  // additional subsystem/object-specific data (such as whether the range
  // belongs to the base mip level or to the rest of the mips).
  //
  // Called with the global critical region locked. Do NOT watch or unwatch
  // ranges from within it! The watch for the callback is cancelled after the
  // callback - the handle becomes invalid.
  WatchHandle WatchMemoryRange(uint32_t start, uint32_t length, WatchCallback callback,
                               void* callback_context, void* callback_data,
                               uint64_t callback_argument);
  // Unregisters previously registered watched memory range.
  void UnwatchMemoryRange(WatchHandle handle);

  // Checks if the range has been updated, uploads new data if needed and
  // ensures the host GPU memory backing the range are resident. Returns true if
  // the range has been fully updated and is usable.
  bool RequestRanges(const std::pair<uint32_t, uint32_t>* ranges, size_t count);
  bool RequestRange(uint32_t start, uint32_t length);

  // Native residency path. Uploads `size` bytes from `host_src` into the buffer
  // at `guest_offset`, bypassing the page validity bookkeeping entirely: no
  // page is marked valid, no write watch is armed, MakeRangeValid and with it
  // EnablePhysicalMemoryAccessCallbacks are never called. The caller owns
  // residency for the range itself - it decides, per draw, whether the bytes
  // the GPU is about to read are already there.
  //
  // The caller must pass the bytes the guest range actually holds, which makes
  // mixing this with the emulated path safe in both directions and in any
  // order:
  //  - over a page the emulated path considers valid, the buffer keeps
  //    matching guest memory, so a later emulated draw over the same range
  //    reads the right thing;
  //  - over a page it considers invalid, validity is left alone, so a later
  //    RequestRange re-uploads the same bytes - redundant, never stale.
  // Nothing here can make the emulated path's view of a page inconsistent with
  // the host protection state, because it never touches either.
  //
  // Returns false for an out of bounds range, if host GPU memory could not be
  // allocated for it, or if the backend could not stage the copy; the caller is
  // expected to fall back to RequestRange for that range rather than to skip
  // the draw. A partial upload is possible on a mid-range staging failure, and
  // is harmless for the same reason the mixed-mode cases above are - the bytes
  // written are the bytes guest memory holds.
  //
  // Must be called on the command processor thread, from inside a submission,
  // with the same lifetime rules as any other per-draw upload.
  //
  // `write_seq_out`, when given, receives the page write sequence this upload
  // was stamped with - see MaxWriteSeq. Only written on success.
  bool UploadRangeUnwatched(uint32_t guest_offset, uint32_t size, const void* host_src,
                            uint64_t* write_seq_out = nullptr);

  // The highest page write sequence over [start, start + length), or 0 if
  // nothing has ever been written there.
  //
  // A caller that owns residency for a range itself keeps a fingerprint per
  // (start, length) window whose meaning is "the BUFFER holds these bytes over
  // this window". Nothing about that claim survives another write into the
  // buffer that overlaps the window, and there are two of those in every
  // frame: an overlapping native range with a different (start, length) key -
  // guest allocators reshuffle sub-allocations, so the windows move while the
  // buffer offsets stay - and the emulated path's page-granular UploadRanges
  // for a texture that happens to share a 4 KiB page with vertex data. Both
  // rewrite bytes inside the window without touching guest memory, so
  // re-hashing guest memory cannot see it: the fingerprint keeps matching and
  // the window keeps being served, spliced with foreign bytes.
  //
  // So every write of data into the buffer stamps the system pages it touched
  // with a monotonically increasing sequence, and a caller that recorded the
  // sequence its own upload was given asks one question before trusting a
  // fingerprint:
  //
  //     MaxWriteSeq(start, length) > entry.upload_seq  =>  the window moved
  //
  // Page granularity is coarse in the safe direction - a write to any part of
  // a page the window touches demotes the window to a re-upload, which costs a
  // redundant copy and never renders wrong bytes.
  //
  // Command processor thread only, like the upload paths that stamp it.
  uint64_t MaxWriteSeq(uint32_t start, uint32_t length) const;

  // Marks the range and, if not exact_range, potentially its surroundings
  // (to up to the first GPU-written page, as an access violation exception
  // count optimization) as modified by the CPU, also invalidating GPU-written
  // pages directly in the range.
  std::pair<uint32_t, uint32_t> MemoryInvalidationCallback(uint32_t physical_address_start,
                                                           uint32_t length, bool exact_range);

  // Marks the range as containing GPU-generated data (such as resolves),
  // triggering modification callbacks, making it valid (so pages are not
  // copied from the main memory until they're modified by the CPU) and
  // protecting it. Before writing anything from the GPU side, RequestRange must
  // be called, to make sure, if the GPU writes don't overwrite *everything* in
  // the pages they touch, the CPU data is properly loaded to the unmodified
  // regions in those pages.
  void RangeWrittenByGpu(uint32_t start, uint32_t length);

 protected:
  SharedMemory(memory::Memory& memory);
  // Call in implementation-specific initialization.
  void InitializeCommon();
  void InitializeSparseHostGpuMemory(uint32_t granularity_log2);
  // Call last in implementation-specific shutdown, also callable from the
  // destructor.
  void ShutdownCommon();

  // Sparse allocations are 4 MB, so not too many of them are allocated, but
  // also not to waste too much memory for padding (with 16 MB there's too
  // much).
  static constexpr uint32_t kHostGpuMemoryOptimalSparseAllocationLog2 = 22;
  static_assert(kHostGpuMemoryOptimalSparseAllocationLog2 <= kBufferSizeLog2);

  memory::Memory& memory() const { return memory_; }

  uint32_t page_size_log2() const { return page_size_log2_; }

  uint32_t host_gpu_memory_sparse_granularity_log2() const {
    return host_gpu_memory_sparse_granularity_log2_;
  }

  // Allocations in the host buffer are aligned the same way as in the guest
  // physical memory (for instance, if an allocation is 64 KB, it can represent
  // 0-64 KB, 64-128 KB, 128-192 KB in the guest memory, and so on, but not
  // something like 16-80 KB. This is assumed by the rules for texture data
  // access in the texture cache.
  virtual bool AllocateSparseHostGpuMemoryRange(uint32_t offset_allocations,
                                                uint32_t length_allocations);

  // Mark the memory range as updated and protect it.
  void MakeRangeValid(uint32_t start, uint32_t length, bool written_by_gpu);

  // Bumps the write sequence and stamps every system page [start, start +
  // length) touches with it, returning the new sequence. Called by every path
  // that writes bytes into the buffer - the emulated page uploads, the native
  // unwatched uploads and the GPU-written (resolve) ranges - so that
  // MaxWriteSeq can answer for all of them. Always bumps, even for an empty or
  // out of bounds range, so a returned sequence is never reused.
  uint64_t StampWrittenPages(uint32_t start, uint32_t length);

  // Uploads a range of host pages - only called if host GPU sparse memory
  // allocation succeeded if needed. While uploading, MakeRangeValid must be
  // called for each successfully uploaded range as early as possible, before
  // the memcpy, to make sure invalidation that happened during the CPU -> GPU
  // memcpy isn't missed (upload_page_ranges is in pages because of this -
  // MakeRangeValid has page granularity). upload_page_ranges are sorted in
  // ascending address order, so front and back can be used to determine the
  // overall bounds of pages to be uploaded.
  virtual bool UploadRanges(
      const std::vector<std::pair<uint32_t, uint32_t>>& upload_page_ranges) = 0;

  // Backend half of UploadRangeUnwatched, called with the range already bounds
  // checked and its host GPU memory already allocated. Stages `size` bytes and
  // enqueues the copy into the buffer with the same barrier discipline as
  // UploadRanges, and must not mark anything valid. The default refuses, which
  // simply routes every caller to the emulated path on backends that have no
  // native residency path.
  virtual bool UploadRangeUnwatchedImpl(uint32_t guest_offset, uint32_t size,
                                        const void* host_src);
 private:
  memory::Memory& memory_;

  // Log2 of invalidation granularity (the system page size, but the dependency
  // on it is not hard - the access callback takes a range as an argument, and
  // touched pages of the buffer of this size will be invalidated).
  uint32_t page_size_log2_;

  bool EnsureHostGpuMemoryAllocated(uint32_t start, uint32_t length);
  uint32_t host_gpu_memory_sparse_granularity_log2_ = UINT32_MAX;
  std::vector<uint64_t> host_gpu_memory_sparse_allocated_;
  uint32_t host_gpu_memory_sparse_allocations_ = 0;
  uint32_t host_gpu_memory_sparse_used_bytes_ = 0;

  void* memory_invalidation_callback_handle_ = nullptr;
  void* memory_data_provider_handle_ = nullptr;

  // Ranges that need to be uploaded, generated by GetRangesToUpload (a
  // persistently allocated vector).
  std::vector<std::pair<uint32_t, uint32_t>> upload_ranges_;

  // Page write sequencing - see MaxWriteSeq. One sequence per system page of
  // the buffer (1 MB at 512 MB / 4 KiB), allocated once in InitializeCommon.
  // Written and read only from the command processor thread, so unsynchronized
  // like the upload paths themselves.
  uint64_t upload_seq_ = 0;
  std::vector<uint64_t> page_write_seq_;
  // Mutex between the guest memory subsystem and the command processor, to be
  // locked when checking or updating validity of pages/ranges and when firing
  // watches.
  rex::thread::global_critical_region global_critical_region_;

  // ***************************************************************************
  // Things below should be fully protected by global_critical_region.
  // ***************************************************************************

  // Pages whose contents in the buffer are in sync with guest memory.
  std::vector<uint64_t> system_page_flags_valid_;
  // Subset of valid pages containing data written by the GPU.
  std::vector<uint64_t> system_page_flags_valid_and_gpu_written_;
  uint32_t num_system_page_flags_ = 0;

  static std::pair<uint32_t, uint32_t> MemoryInvalidationCallbackThunk(
      void* context_ptr, uint32_t physical_address_start, uint32_t length, bool exact_range);

  struct GlobalWatch {
    GlobalWatchCallback callback;
    void* callback_context;
  };
  std::vector<GlobalWatch*> global_watches_;
  struct WatchNode;
  // Watched range placed by other GPU subsystems.
  struct WatchRange {
    union {
      struct {
        WatchCallback callback;
        void* callback_context;
        void* callback_data;
        uint64_t callback_argument;
        WatchNode* node_first;
        uint32_t page_first;
        uint32_t page_last;
      };
      WatchRange* next_free;
    };
  };
  // Node for faster checking of watches when pages have been written to - all
  // 512 MB are split into smaller equally sized buckets, and then ranges are
  // linearly checked.
  struct WatchNode {
    union {
      struct {
        WatchRange* range;
        // Link to another node of this watched range in the next bucket.
        WatchNode* range_node_next;
        // Links to nodes belonging to other watched ranges in the bucket.
        WatchNode* bucket_node_previous;
        WatchNode* bucket_node_next;
      };
      WatchNode* next_free;
    };
  };
  static constexpr uint32_t kWatchBucketSizeLog2 = 22;
  static constexpr uint32_t kWatchBucketCount = 1 << (kBufferSizeLog2 - kWatchBucketSizeLog2);
  WatchNode* watch_buckets_[kWatchBucketCount] = {};
  // Allocation from pools - taking new WatchRanges and WatchNodes from the free
  // list, and if there are none, creating a pool if the current one is fully
  // used, and linearly allocating from the current pool.
  static constexpr uint32_t kWatchRangePoolSize = 8192;
  static constexpr uint32_t kWatchNodePoolSize = 8192;
  std::vector<WatchRange*> watch_range_pools_;
  std::vector<WatchNode*> watch_node_pools_;
  uint32_t watch_range_current_pool_allocated_ = 0;
  uint32_t watch_node_current_pool_allocated_ = 0;
  WatchRange* watch_range_first_free_ = nullptr;
  WatchNode* watch_node_first_free_ = nullptr;
  // Triggers the watches (global and per-range), removing triggered range
  // watches.
  void FireWatches(uint32_t page_first, uint32_t page_last, bool invalidated_by_gpu);
  // Unlinks and frees the range and its nodes. Call this in the global critical
  // region.
  void UnlinkWatchRange(WatchRange* range);
};

}  // namespace rex::graphics
