/**
 ******************************************************************************
 * ReXGlue : Xenos GPU plugin - native residency                              *
 ******************************************************************************
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * New to this tree, not derived from Xenia. It plugs into the Xenia-derived
 * primitive processor and command processor through the two hooks added
 * alongside it.
 */

#include "native_residency.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include <rex/graphics/flags.h>
#include <rex/graphics/vulkan/command_processor.h>
#include <rex/graphics/vulkan/primitive_processor.h>
#include <rex/graphics/xenos.h>
#include <rex/hash.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>

namespace rex::graphics::vulkan {

void NativeResidency::LatchFrameModeAtSwap() {
  // Judge the frame that just ended before anything about it is reset. A run
  // of frames that mostly could not be served natively is not a mode worth
  // staying in: it costs the fingerprinting and pays nothing back, and it
  // reads as instability rather than as the refusal it is. Three of them clear
  // the master cvar; setting it again (the F5 bind) re-arms.
  uint64_t candidate_draws = frame_native_draws_ + frame_fallback_draws_;
  if (native_frame_ && candidate_draws >= kLatchMinCandidateDraws) {
    if (frame_fallback_draws_ * 2 > candidate_draws) {
      if (++latch_streak_ >= kLatchFrameCount) {
        latch_streak_ = 0;
        REXCVAR_SET(gpu_native_residency, false);
        REXGPU_WARN(
            "Native residency disabled: {} consecutive frames fell back to the emulated path "
            "for more than half of their draws (last frame {} of {}). Re-enable it to try "
            "again.",
            uint32_t(kLatchFrameCount), frame_fallback_draws_, candidate_draws);
      }
    } else {
      latch_streak_ = 0;
    }
  }
  frame_native_draws_ = 0;
  frame_fallback_draws_ = 0;

  ++frame_epoch_;
  bool native_frame_previous = native_frame_;
  native_frame_ = REXCVAR_GET(gpu_native_residency);
  native_index_frame_ = native_frame_ && REXCVAR_GET(gpu_native_index);
  native_vertex_frame_ = native_frame_ && REXCVAR_GET(gpu_native_vertex);
  dedup_frame_ = REXCVAR_GET(gpu_native_dedup);
  // Only ever true in a native frame: the oracle judges this path, and the
  // emulated path must not change shape because a diagnostic cvar is set.
  verify_frame_ = native_frame_ && REXCVAR_GET(gpu_native_verify);
  // Prefetch is a scheduling experiment on the normal deduplicated path. It
  // stays out of both diagnostic modes so their meaning does not change.
  prefetch_frame_ =
      native_vertex_frame_ && dedup_frame_ && !verify_frame_ && REXCVAR_GET(gpu_native_prefetch);
  hash_max_bytes_frame_ = REXCVAR_GET(gpu_native_hash_max_bytes);
  // The oracle exists to compare fingerprints against the guest bytes, so a
  // verify frame takes one for every range whatever the threshold says. That
  // keeps the verify soak exactly as sensitive as it was before this knob, at
  // any setting of it.
  hash_min_bytes_frame_ = verify_frame_ ? 0 : REXCVAR_GET(gpu_native_hash_min_bytes);
  // Draws are only ever issued between swaps, but leave no stale permission
  // behind for a mode that has just been switched off.
  draw_index_residency_allowed_ = false;
  draw_vertex_residency_allowed_ = false;

  if (native_frame_ && !native_frame_previous) {
    // Switching the path on. Nothing learned before this point is trusted:
    // while it was off, the emulated path was free to upload over any of these
    // ranges, and a fingerprint says what guest memory held, not what the
    // buffer holds. Cheaper to re-copy the first frame than to reason about it.
    range_table_.clear();
    ++range_table_generation_;
    latch_streak_ = 0;
  } else if (native_frame_ && !(frame_epoch_ & (kEntrySweepPeriodFrames - 1))) {
    // Periodic eviction rather than per-frame, because it walks the table.
    bool erased_entry = false;
    for (auto it = range_table_.begin(); it != range_table_.end();) {
      // A range recorded in the frame that just ended owns a stable Entry*
      // carried into the prefetch wave below. Do not invalidate it during the
      // periodic sweep; PrefetchFrame stamps last_epoch immediately after.
      bool recorded_for_prefetch = it->second.prefetch_record_epoch + 1 == frame_epoch_;
      if (!recorded_for_prefetch && frame_epoch_ - it->second.last_epoch > kEntryMaxAgeFrames) {
        it = range_table_.erase(it);
        erased_entry = true;
      } else {
        it = std::next(it);
      }
    }
    if (erased_entry) {
      ++range_table_generation_;
    }
  }

  if (prefetch_frame_) {
    // Keep both vector allocations alive across frames. Move-assignment made
    // the old prefetch buffer release its storage and left current empty, so
    // the 2,000+ range working set was allocated again every frame.
    prefetch_ranges_.swap(current_frame_ranges_);
    current_frame_ranges_.clear();
  } else {
    current_frame_ranges_.clear();
    prefetch_ranges_.clear();
  }
}

void NativeResidency::PrefetchFrame(memory::Memory& memory, SharedMemory& shared_memory) {
  if (!prefetch_frame_ || prefetch_ranges_.empty()) {
    return;
  }

  // Uploads still use the existing proven copy path. Their transfer barriers
  // are individually conservative for this first experiment, but they all
  // execute here while the outgoing pass is ending, rather than interleaved
  // with the next frame's draws. This isolates pass scheduling before barrier
  // coalescing changes any other variable.
  for (VertexRange& range : prefetch_ranges_) {
    Entry* entry_ptr = range.entry;
    range.entry = nullptr;
    if (!range.length || range.start >= SharedMemory::kBufferSize ||
        SharedMemory::kBufferSize - range.start < range.length ||
        range.length > hash_max_bytes_frame_ ||
        IntersectsResolveDestination(range.start, range.length)) {
      continue;
    }
    Entry& entry = *entry_ptr;
    if (entry.blacklisted) {
      continue;
    }

    const void* source = memory.TranslatePhysical<const void*>(range.start);
    PERF_counter_add(kNativeBytesHashed, range.length);
    const uint64_t hash = XXH3_64bits(source, range.length);
    if (!shared_memory.UploadRangeUnwatched(range.start, range.length, source)) {
      uint64_t prefetch_record_epoch = entry.prefetch_record_epoch;
      entry = Entry{};
      entry.prefetch_record_epoch = prefetch_record_epoch;
      continue;
    }
    range.hash = hash;
    range.entry = entry_ptr;
    PERF_counter_inc(kNativePrefetchRanges);
  }

  // Stamp entries only after every copy. Overlapping vertex windows contain
  // the same guest bytes in their overlap, and MaxWriteSeq now records the
  // final batch state rather than making an earlier copy look clobbered by a
  // later one from this same prefetch wave.
  for (const VertexRange& range : prefetch_ranges_) {
    if (!range.entry) {
      continue;
    }
    Entry& entry = *range.entry;
    entry.hash = range.hash;
    entry.hash_valid = true;
    entry.last_epoch = frame_epoch_;
    entry.upload_seq = shared_memory.MaxWriteSeq(range.start, range.length);
    entry.prefetched = true;
  }
  prefetch_ranges_.clear();
}

void NativeResidency::BeginDraw(bool allowed) {
  draw_index_residency_allowed_ = native_index_frame_ && allowed;
  draw_vertex_residency_allowed_ = native_vertex_frame_ && allowed;
  draw_index_native_ = false;
  draw_used_native_ = false;
  draw_fell_back_ = false;
}

void NativeResidency::EndDraw() {
  if (draw_fell_back_) {
    // A draw that had to ask the emulated path for any one of its ranges is a
    // fallback draw, however much of the rest of it was served natively - the
    // watch it wanted to avoid was armed.
    ++frame_fallback_draws_;
    PERF_counter_inc(kNativeFallbackDraws);
  } else if (draw_used_native_) {
    ++frame_native_draws_;
    PERF_counter_inc(kNativeDraws);
  }
  draw_used_native_ = false;
  draw_fell_back_ = false;
}

void NativeResidency::NoteResolveDestination(uint32_t start, uint32_t length) {
  if (!length || start >= SharedMemory::kBufferSize) {
    return;
  }
  length = std::min(length, SharedMemory::kBufferSize - start);
  uint32_t page_first = start >> kResolvePageSizeLog2;
  uint32_t page_last = (start + length - 1) >> kResolvePageSizeLog2;
  for (uint32_t page = page_first; page <= page_last; ++page) {
    resolve_pages_[page >> 6] |= uint64_t(1) << (page & 63);
  }
}

bool NativeResidency::IntersectsResolveDestination(uint32_t start, uint32_t length) const {
  if (!length || start >= SharedMemory::kBufferSize) {
    return false;
  }
  length = std::min(length, SharedMemory::kBufferSize - start);
  uint32_t page_first = start >> kResolvePageSizeLog2;
  uint32_t page_last = (start + length - 1) >> kResolvePageSizeLog2;
  for (uint32_t page = page_first; page <= page_last; ++page) {
    if (resolve_pages_[page >> 6] & (uint64_t(1) << (page & 63))) {
      return true;
    }
  }
  return false;
}

bool NativeResidency::UploadRange(memory::Memory& memory, SharedMemory& shared_memory,
                                  uint32_t start, uint32_t length, Entry& entry, uint64_t hash,
                                  bool hash_valid) {
  // Guest bytes verbatim, exactly as the emulated path uploads them: the
  // shared memory buffer holds guest-endian data and the translated shaders
  // swizzle what they fetch out of it. Swapping here would swap twice.
  uint64_t upload_seq = 0;
  if (!shared_memory.UploadRangeUnwatched(
          start, length, memory.TranslatePhysical<const void*>(start), &upload_seq)) {
    // The bytes that did make it are the bytes guest memory holds, so nothing
    // is stale - but this range is not resident, so drop what is claimed about
    // it and let the caller ask the emulated path.
    uint64_t prefetch_record_epoch = entry.prefetch_record_epoch;
    entry = Entry{};
    entry.prefetch_record_epoch = prefetch_record_epoch;
    return false;
  }
  entry.hash = hash;
  // An unfingerprinted range records no claim about guest memory at all, only
  // that these bytes went into the buffer at this sequence in this frame.
  entry.hash_valid = hash_valid;
  entry.last_epoch = frame_epoch_;
  // What guest memory held (hash) and what the buffer held (upload_seq) at the
  // same instant. A hit needs both to still be true.
  entry.upload_seq = upload_seq;
  entry.prefetched = false;
  return true;
}

NativeResidency::VertexRangeResult NativeResidency::RequestVertexRange(memory::Memory& memory,
                                                                       SharedMemory& shared_memory,
                                                                       uint32_t start,
                                                                       uint32_t length,
                                                                       bool caller_cache_hit) {
  if (!length || start >= SharedMemory::kBufferSize ||
      (SharedMemory::kBufferSize - start) < length) {
    return caller_cache_hit && !verify_frame_ ? VertexRangeResult::kCallerCacheHit
                                              : VertexRangeResult::kFallback;
  }
  if (caller_cache_hit && !verify_frame_ && !prefetch_frame_) {
    // Preserve the old cache fast path when frame-ahead recording is off: no
    // residency-table operation is owed in this mode.
    return VertexRangeResult::kCallerCacheHit;
  }

  uint64_t key = RangeKey(start, length);
  if (!dedup_frame_) {
    // Paranoia mode: no fingerprint is believed, every request copies. Nothing
    // is recorded either, so that turning dedup back on starts from a clean
    // table rather than from entries that were never maintained.
    if (range_table_.erase(key)) {
      ++range_table_generation_;
    }
    Entry scratch;
    return UploadRange(memory, shared_memory, start, length, scratch, 0, false)
               ? VertexRangeResult::kNative
               : VertexRangeResult::kFallback;
  }

  // The residency entry is also the first-seen marker for next-frame
  // prefetch. Retain hot node pointers in a two-way cache in front of the map.
  // Nodes survive rehash; any erase invalidates all handles through
  // range_table_generation_. Mixing both halves of the range key avoids the
  // repeated buffer sizes clustering in the low bits.
  uint64_t cache_hash = (key ^ (key >> 32)) * UINT64_C(11400714819323198485);
  auto& cached_set = cached_vertex_ranges_[cache_hash >> (64 - kRangeCacheSetBits)];
  Entry* entry_pointer = nullptr;
  bool inserted = false;
  if (cached_set[0].generation == range_table_generation_ && cached_set[0].key == key) {
    entry_pointer = cached_set[0].entry;
  } else if (cached_set[1].generation == range_table_generation_ && cached_set[1].key == key) {
    std::swap(cached_set[0], cached_set[1]);
    entry_pointer = cached_set[0].entry;
  }
  if (!entry_pointer) {
    auto [it, was_inserted] = range_table_.try_emplace(key, Entry{});
    entry_pointer = &it->second;
    inserted = was_inserted;
    cached_set[1] = cached_set[0];
    cached_set[0] = {key, range_table_generation_, entry_pointer};
  }
  Entry& entry = *entry_pointer;
  if (prefetch_frame_ && entry.prefetch_record_epoch != frame_epoch_) {
    entry.prefetch_record_epoch = frame_epoch_;
    current_frame_ranges_.push_back({start, length, 0, &entry});
  }

  if (caller_cache_hit && !verify_frame_ && !entry.prefetched) {
    return VertexRangeResult::kCallerCacheHit;
  }
  if (length > hash_max_bytes_frame_) {
    // Hashing and copying something this big every frame would cost more than
    // the watch it saves, and it is the shape of range this path is least sure
    // about. The emulated path is better at it.
    return VertexRangeResult::kFallback;
  }
  // Ahead of the dedup, not after it: a resolve can land on a range that was
  // already stamped earlier in this frame, and from then on guest memory is
  // the stale copy. Once a range is refused here it is refused for the rest of
  // the session.
  if (IntersectsResolveDestination(start, length)) {
    return VertexRangeResult::kFallback;
  }

  // Is this range worth fingerprinting? A fingerprint only ever buys the copy
  // it lets us skip, and below the threshold that trade has been measured to
  // lose: the range is copied in unconditionally instead, with no XXH3 taken
  // and none believed. Everything else about the path is unaffected - the
  // refusals above, the epoch dedup, the write-sequence invalidation below and
  // the blacklist all behave identically, because copying the current guest
  // bytes is exactly what the emulated path would have done with them. The
  // oracle forces this to true (see LatchFrameModeAtSwap).
  const bool hash_this = verify_frame_ || length >= hash_min_bytes_frame_;

  if (inserted || entry.blacklisted) {
    // Never seen, or seen and caught lying by the verify oracle.
    if (!hash_this) {
      return UploadRange(memory, shared_memory, start, length, entry, 0, false)
                 ? VertexRangeResult::kNative
                 : VertexRangeResult::kFallback;
    }
    PERF_counter_add(kNativeBytesHashed, length);
    uint64_t hash = XXH3_64bits(memory.TranslatePhysical<const void*>(start), length);
    return UploadRange(memory, shared_memory, start, length, entry, hash, true)
               ? VertexRangeResult::kNative
               : VertexRangeResult::kFallback;
  }

  // Has anything been written into the buffer over this window since this
  // entry was stamped? The entry's claim is about the buffer, and the two
  // things that break it - an upload for an overlapping window with a
  // different key, and the emulated path's page-granular upload for a texture
  // sharing a page - both leave guest memory alone, so every guest-side check
  // below (epoch, fingerprint, and the verify oracle, which re-hashes guest
  // bytes too) is structurally blind to them. Only the buffer's own write
  // sequence can see it, and it has to be asked on EVERY hit path: an
  // overlapping upload can land between two requests for this window inside a
  // single frame, which is exactly what the epoch fast path would otherwise
  // wave through.
  bool clobbered = shared_memory.MaxWriteSeq(start, length) > entry.upload_seq;
  if (clobbered) {
    PERF_counter_inc(kNativeSeqEvictions);
  }

  if (entry.prefetched) {
    // A frame-boundary snapshot is only a prediction. The guest may generate
    // or rewrite dynamic vertices between swap and draw, so the first consumer
    // must prove the bytes are unchanged even when the caller's fetch constant
    // cache says the range is resident.
    entry.prefetched = false;
    PERF_counter_add(kNativeBytesHashed, length);
    const uint64_t hash = XXH3_64bits(memory.TranslatePhysical<const void*>(start), length);
    if (!clobbered && entry.hash_valid && hash == entry.hash) {
      PERF_counter_inc(kNativePrefetchHits);
      return VertexRangeResult::kNative;
    }
    PERF_counter_inc(kNativePrefetchMisses);
    return UploadRange(memory, shared_memory, start, length, entry, hash, true)
               ? VertexRangeResult::kNative
               : VertexRangeResult::kFallback;
  }

  if (!clobbered && entry.last_epoch == frame_epoch_) {
    // Already made resident this frame - the common case, a vertex buffer
    // shared by many draws of one frame. Free unless the oracle is on.
    if (!verify_frame_) {
      return VertexRangeResult::kNative;
    }
  }

  if (!hash_this) {
    // Below the threshold: no fingerprint is taken and the entry's old one, if
    // it has one, is not consulted - the range is simply made to hold what
    // guest memory holds right now, which is the strongest thing the emulated
    // path ever claims about it. Whatever the compare would have decided, this
    // is at least as fresh.
    return UploadRange(memory, shared_memory, start, length, entry, 0, false)
               ? VertexRangeResult::kNative
               : VertexRangeResult::kFallback;
  }

  PERF_counter_add(kNativeBytesHashed, length);
  uint64_t hash = XXH3_64bits(memory.TranslatePhysical<const void*>(start), length);
  if (!entry.hash_valid) {
    // The threshold was lowered under this entry, or the oracle was switched on
    // over one born unfingerprinted. There is nothing to compare against, so
    // copy and establish the fingerprint; a stale field must never be read as
    // one, and none of the oracle's verdicts below can be reached from here
    // because every one of them is a statement about a fingerprint that moved.
    return UploadRange(memory, shared_memory, start, length, entry, hash, true)
               ? VertexRangeResult::kNative
               : VertexRangeResult::kFallback;
  }
  if (hash == entry.hash && !clobbered) {
    // Static geometry: one hash per frame, no copy.
    entry.last_epoch = frame_epoch_;
    return VertexRangeResult::kNative;
  }
  // The oracle's verdicts below are all statements about the guest rewriting
  // its own bytes, so they are only reachable when the fingerprint itself
  // moved. A clobbered window with an unchanged fingerprint is this path's
  // own bookkeeping catching up, not a guest rewrite, and must not be
  // reported as one.
  if (hash != entry.hash && verify_frame_ && entry.last_epoch == frame_epoch_) {
    // The oracle's finding: this range was copied earlier in this same frame
    // and the guest has rewritten it since, so a frame-granular fingerprint is
    // not enough for it. Copy it on every request from now on. This is the one
    // case that would otherwise render wrong, and it is reported loudly
    // because the whole design assumes it does not happen.
    entry.blacklisted = true;
    REXGPU_WARN(
        "gpu_native_verify: range 0x{:08X} + 0x{:X} bytes was rewritten by the guest inside "
        "one frame; copying it per draw from now on",
        start, length);
  } else if (hash != entry.hash && verify_frame_ && caller_cache_hit &&
             verify_stale_warnings_++ < kVerifyWarningLimit) {
    // The other thing the oracle is here for: the caller believed this range
    // was still resident because the fetch constant had not moved, but its
    // bytes have. Without the oracle nothing would have re-read them - in
    // either mode, this one predates the native path - so it is a finding
    // about the emulated path's cache as much as about this one. No blacklist:
    // the fingerprint caught it a frame later than the rewrite, and per-draw
    // copying would not have caught it any earlier.
    REXGPU_WARN(
        "gpu_native_verify: range 0x{:08X} + 0x{:X} bytes was rewritten in place while its "
        "fetch constant stayed put; the vertex buffer residency cache would have missed it "
        "({} of at most {} reports)",
        start, length, verify_stale_warnings_, uint32_t(kVerifyWarningLimit));
    if (verify_stale_warnings_ == kVerifyWarningLimit) {
      REXGPU_WARN(
          "gpu_native_verify: further in-place rewrite reports suppressed; the oracle keeps "
          "re-reading and re-uploading these ranges regardless");
    }
  }
  return UploadRange(memory, shared_memory, start, length, entry, hash, true)
             ? VertexRangeResult::kNative
             : VertexRangeResult::kFallback;
}

bool NativeResidency::LastRequestedRangeHash(uint32_t start, uint32_t length,
                                             uint64_t* out) const {
  if (!out) {
    return false;
  }
  auto it = range_table_.find(RangeKey(start, length));
  if (it != range_table_.end() && it->second.hash_valid) {
    *out = it->second.hash;
    return true;
  }
  it = range_table_.find(ConversionSourceKey(start, length));
  if (it != range_table_.end() && it->second.hash_valid) {
    *out = it->second.hash;
    return true;
  }
  *out = 0;
  return false;
}

bool NativeResidency::MayFingerprintConversionSource(const void* guest_source, uint32_t start,
                                                     uint32_t length) {
  if (!draw_index_residency_allowed_) {
    // Not a candidate at all - either the native path is off for this frame or
    // the command processor excluded this draw. Not a fallback, so not counted
    // as one, and the cache keeps its watch exactly as it always has.
    return false;
  }
  // Past this point the draw was a candidate, so every exit either serves it
  // natively or marks it as having fallen back; the draw itself is totalled
  // once, in EndDraw.
  auto fall_back = [&]() {
    NoteDrawFallback();
    return false;
  };
  if (!length || start >= SharedMemory::kBufferSize ||
      (SharedMemory::kBufferSize - start) < length) {
    return fall_back();
  }
  if (length > hash_max_bytes_frame_) {
    // Same limit the vertex path applies: past it the fingerprint costs more
    // than the watch it replaces.
    return fall_back();
  }
  // With gpu_native_dedup off - the paranoia mode - the fingerprint is still
  // taken, but no reuse is trusted on the strength of being in the same frame:
  // every one is re-checked against the guest bytes, which is what the watch
  // did, without the watch. The verify oracle wants the same, plus a verdict.
  bool fingerprint_reuse = verify_frame_ || !dedup_frame_;
  // No resolve guard here, deliberately: nothing is copied into the shared
  // memory buffer for a conversion, and both paths read the same guest bytes
  // to convert from, so a resolve destination is no more or less stale for
  // this than it already is for the emulated path.
  uint64_t key = ConversionSourceKey(start, length);
  auto it = range_table_.find(key);
  if (it != range_table_.end() && it->second.blacklisted) {
    // Caught rewriting itself inside a frame by the oracle - watched again for
    // the rest of the run. Stamped so that the eviction sweep keeps the verdict
    // for as long as the range is still being drawn from.
    it->second.last_epoch = frame_epoch_;
    return fall_back();
  }
  if (fingerprint_reuse) {
    // Taken before the conversion reads the indices, so that a rewrite while it
    // is reading them is caught by the next reuse instead of being baked into
    // the fingerprint.
    PERF_counter_add(kNativeBytesHashed, length);
    Entry& entry = (it != range_table_.end())
                       ? it->second
                       : range_table_.try_emplace(key, Entry{}).first->second;
    entry.hash = XXH3_64bits(guest_source, length);
    // Conversion sources are outside gpu_native_hash_min_bytes: there is
    // nothing to copy for one, so the fingerprint's alternative is re-arming
    // the watch this replaced, not an unconditional copy. An entry on this key
    // is therefore always fingerprinted.
    entry.hash_valid = true;
    entry.last_epoch = frame_epoch_;
  }
  NoteDrawNativeUsed();
  return true;
}

bool NativeResidency::IsConversionSourceUnchanged(const void* guest_source, uint32_t start,
                                                  uint32_t length) {
  if (!verify_frame_ && dedup_frame_) {
    // The epoch rule, and the only rule in a normal run: what was made resident
    // during a frame is good for the rest of that frame. The converted index
    // cache empties itself at every frame boundary, so an entry never outlives
    // the frame its source was fingerprinted in.
    return true;
  }
  auto it = range_table_.find(ConversionSourceKey(start, length));
  if (it == range_table_.end()) {
    // Nothing recorded to check against - the oracle was switched on after this
    // entry was converted, or the fingerprint has been swept.
    return true;
  }
  if (it->second.blacklisted) {
    // Known to rewrite itself inside a frame; a reuse of it is never trusted,
    // even one cached before the blacklist was applied.
    return false;
  }
  if (!it->second.hash_valid) {
    // Unreachable while the key spaces stay apart - conversion source entries
    // are always fingerprinted, see MayFingerprintConversionSource - and here
    // so that the rule holds everywhere rather than by inspection: a hash field
    // that was never filled in is not a fingerprint that happens to be zero,
    // and must never be compared against one.
    return true;
  }
  PERF_counter_add(kNativeBytesHashed, length);
  if (XXH3_64bits(guest_source, length) == it->second.hash) {
    return true;
  }
  if (verify_frame_) {
    // The oracle's verdict, and the only case that blacklists: with dedup off
    // every reuse is re-checked anyway, so there would be nothing to escalate
    // to. Reported loudly because the whole design assumes it does not happen.
    it->second.blacklisted = true;
    REXGPU_WARN(
        "gpu_native_verify: converted index source 0x{:08X} + 0x{:X} bytes was rewritten by the "
        "guest inside one frame; watching it again for the rest of the run",
        start, length);
  }
  return false;
}

// The primitive processor hook, defined here rather than next to the rest of
// VulkanPrimitiveProcessor so the whole native path sits in one translation
// unit. It has to be a member because the per-frame index buffer pool it
// allocates from is protected.
//
// Contract: on success the caller switches the processing result from
// kGuestDMA to kHostConverted and takes the handle written back here; every
// other field of the result - host index format, shader index endian,
// primitive reset - stays exactly as the guest DMA path computed it, because
// the host buffer holds the guest bytes unchanged. On failure the caller falls
// through to the untouched emulated path for this draw. A draw is never
// dropped here.
bool VulkanPrimitiveProcessor::TryMakeIndexBufferNativelyResident(
    const void* guest_indices, uint32_t guest_index_base, uint32_t size_bytes,
    xenos::IndexFormat host_index_format, uint32_t index_count, bool host_vertex_shader_is_vertex,
    size_t& host_index_buffer_handle_out) {
  if (!size_bytes || !index_count) {
    // A draw with no indices at all - the caller drops it right after this,
    // and the upload pool would have nothing to hand out. Not a real draw, so
    // not counted either way.
    return false;
  }
  NativeResidency* residency = command_processor_.native_residency();
  if (!residency || !residency->IsIndexResidencyAllowedForDraw()) {
    // Not a candidate at all - either the native path is off for this frame or
    // the command processor excluded this draw. Not a fallback, so not counted
    // as one.
    return false;
  }

  // Past this point the draw was a candidate, so every exit either serves it
  // natively or marks it as having fallen back. The draw itself is totalled
  // once, in NativeResidency::EndDraw, so that a draw whose index buffer went
  // native and whose vertex buffer did not counts as one fallback draw rather
  // than as one of each.
  auto fall_back = [&]() {
    residency->NoteDrawFallback();
    return false;
  };

  if (!host_vertex_shader_is_vertex) {
    // Point sprite expansion, rectangle list expansion and every tessellation
    // mode load the guest index in the shader out of the shared memory buffer
    // (SpirvShaderTranslator kSysFlag_ComputeOrPrimitiveVertexIndexLoad), so
    // the indices have to actually be there.
    return fall_back();
  }
  if (host_index_format == xenos::IndexFormat::kInt32 && !AreFull32BitVertexIndicesUsed()) {
    // Same again: without fullDrawIndexUint32 the vertex shader loads 32-bit
    // indices indirectly from the shared memory buffer instead of letting the
    // index buffer supply them.
    return fall_back();
  }
  if (residency->IntersectsResolveDestination(guest_index_base, size_bytes)) {
    // Guest memory may be stale over a resolve destination - see
    // NativeResidency::NoteResolveDestination.
    return fall_back();
  }

  size_t handle;
  void* host_indices = RequestHostConvertedIndexBufferForCurrentFrame(
      host_index_format, index_count, false, guest_index_base, handle);
  if (!host_indices) {
    return fall_back();
  }

  // Byte for byte, deliberately. The Vulkan backend endian-swaps the fetched
  // index inside the vertex shader (vertex_index_endian, taken from
  // VGT_DMA_SIZE.swap_mode and applied to host converted index buffers exactly
  // as it is to guest DMA ones), and the primitive reset index has already
  // been normalized by the caller if it needed to be. Swapping here would swap
  // twice.
  std::memcpy(host_indices, guest_indices, size_bytes);

  host_index_buffer_handle_out = handle;
  residency->NoteDrawIndexNative();
  PERF_counter_add(kNativeBytesUploaded, size_bytes);
  return true;
}

// The converted index cache's two hooks, the other half of the index path: the
// cache keeps its guest sources watched only to notice a rewrite between two
// draws of one frame, and these hand that job to the fingerprint machinery
// above. Both are called from PrimitiveProcessor::CacheTransaction, so from
// inside Process, so on the command processor thread with the current draw's
// permissions already latched by BeginDraw.
bool VulkanPrimitiveProcessor::MayFingerprintConversionSource(const void* guest_source,
                                                              uint32_t base, uint32_t size_bytes) {
  NativeResidency* residency = command_processor_.native_residency();
  return residency && residency->MayFingerprintConversionSource(guest_source, base, size_bytes);
}

bool VulkanPrimitiveProcessor::IsConversionSourceUnchanged(const void* guest_source, uint32_t base,
                                                           uint32_t size_bytes) {
  NativeResidency* residency = command_processor_.native_residency();
  return !residency || residency->IsConversionSourceUnchanged(guest_source, base, size_bytes);
}

}  // namespace rex::graphics::vulkan
