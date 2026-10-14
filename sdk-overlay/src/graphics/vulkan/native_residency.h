#pragma once
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

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <rex/graphics/shared_memory.h>
#include <rex/memory.h>

namespace rex::graphics::vulkan {

// Owns the policy for the native residency draw path: instead of making draw
// data visible to the GPU by page-watching guest memory inside the shared
// memory buffer, the data is copied out of guest memory into host owned
// storage as the draw is issued. That trades a page watch - an mprotect per
// invalidated range, which is where a third of the command processor thread
// goes - for a copy.
//
// Every method runs on the command processor thread, from IssueSwap or from
// inside IssueDraw, so nothing here is synchronized. With gpu_native_residency
// at its default the only work done is the once-per-frame latch, and the draw
// path behaves exactly as it did before this file existed.
//
// Two kinds of draw data are covered. Index buffers are copied into the
// primitive processor's per-frame index buffer pool, which is why that half
// lives in VulkanPrimitiveProcessor::TryMakeIndexBufferNativelyResident in the
// accompanying .cc. Vertex buffers stay where the translated shaders expect
// them - the shared memory buffer, at their guest offsets - and are copied
// there through SharedMemory::UploadRangeUnwatched, which skips the page
// validity bookkeeping and with it the watch arming. Because those copies go
// to the same place the emulated path uploads to, a vertex range only has to
// be copied when its bytes have actually changed, which is what the
// fingerprint cache below decides.
class NativeResidency {
 public:
  NativeResidency() : resolve_pages_(kResolvePageWords, 0) {
    // The busy-map fixture has roughly 2,300 distinct vertex ranges per
    // frame. Prepare enough room for a meaningfully larger scene and keep the
    // node tables sparse: both are command-thread hot paths, and their default
    // near-1.0 load factor showed up directly in the busy-window profile.
    range_table_.max_load_factor(0.5f);
    range_table_.reserve(kExpectedFrameRangeCount);
    current_frame_ranges_.reserve(kExpectedFrameRangeCount);
    prefetch_ranges_.reserve(kExpectedFrameRangeCount);
  }

  // Frame boundary, called from IssueSwap. The mode is latched once per frame
  // so that no draw sees the cvars change underneath it and no frame is half
  // native and half emulated. Also where the fallback-ratio latch is judged
  // and where stale fingerprint entries are swept.
  void LatchFrameModeAtSwap();

  // Copies the vertex working set observed in the previous frame before the
  // next frame enters its render pass. LatchFrameModeAtSwap prepares the set;
  // IssueSwap calls this only after BeginSubmission has made recording safe.
  void PrefetchFrame(memory::Memory& memory, SharedMemory& shared_memory);

  bool native_frame() const { return native_frame_; }
  // Frames since startup. The fingerprint cache stamps its entries with this
  // to tell "already made resident this frame" from "seen in an older frame".
  uint64_t frame_epoch() const { return frame_epoch_; }

  // Per draw, from IssueDraw before the primitive processor runs. `allowed` is
  // false for draws that take the emulated path regardless of the cvars -
  // currently memory export draws, whose shaders read the index buffer out of
  // the shared memory buffer themselves and whose destination ranges the
  // emulated path has to keep watched.
  void BeginDraw(bool allowed);
  bool IsIndexResidencyAllowedForDraw() const { return draw_index_residency_allowed_; }
  bool IsVertexResidencyAllowedForDraw() const { return draw_vertex_residency_allowed_; }
  bool WasIndexNativeForDraw() const { return draw_index_native_; }

  // Per-draw outcome, recorded by both halves of the path and totalled once in
  // EndDraw so that a draw counts as exactly one native draw or one fallback
  // draw however many ranges it referenced.
  void NoteDrawNativeUsed() { draw_used_native_ = true; }
  void NoteDrawIndexNative() {
    draw_index_native_ = true;
    draw_used_native_ = true;
  }
  void NoteDrawFallback() { draw_fell_back_ = true; }
  void EndDraw();

  // Records a resolve destination, unconditionally and for the rest of the
  // session. RenderTargetCache::Resolve writes resolved pixels into the shared
  // memory buffer, and into guest memory only under a readback resolve, so
  // guest memory can be stale with respect to the buffer over these ranges.
  // Copying guest memory over one would undo the resolve, so any native range
  // that touches one takes the emulated path forever. Fed even while the
  // native path is off so the set is already complete when it is switched on.
  void NoteResolveDestination(uint32_t start, uint32_t length);
  bool IntersectsResolveDestination(uint32_t start, uint32_t length) const;

  enum class VertexRangeResult {
    kCallerCacheHit,
    kNative,
    kFallback,
  };

  // Records the range for next-frame prefetch and makes [start, start +
  // length) readable by the GPU out of the shared memory buffer. A caller
  // cache hit that does not owe prefetch validation is returned separately so
  // it keeps the existing draw-accounting behavior.
  VertexRangeResult RequestVertexRange(memory::Memory& memory, SharedMemory& shared_memory,
                                       uint32_t start, uint32_t length,
                                       bool caller_cache_hit = false);

  // Read-only census accessor. It never fingerprints or touches guest memory;
  // false means the range is unknown or was deliberately kept below
  // gpu_native_hash_min_bytes.
  bool LastRequestedRangeHash(uint32_t start, uint32_t length, uint64_t* out) const;

  // Makes [start, start + length) readable by the GPU out of the shared memory
  // buffer without arming a write watch for it, by fingerprinting the guest
  // bytes and copying them in when the fingerprint has moved.
  //
  // A range shorter than gpu_native_hash_min_bytes skips the fingerprint and is
  // copied in on every request that the epoch stamp does not already satisfy.
  // Nothing else about it changes: it is still deduplicated within a frame,
  // still invalidated by the buffer's write sequence, still refused over a
  // resolve destination. Copying current guest bytes is what the emulated path
  // does, so dropping the compare cannot make anything stale - it can only make
  // this path copy where it would have proved a copy unnecessary.
  //
  // Returns false for any range this path refuses to own - one that intersects
  // a resolve destination, one larger than gpu_native_hash_max_bytes, one out
  // of bounds, or one whose copy could not be staged. The caller must then
  // request the range through the emulated path instead; a draw is never
  // dropped over a refusal.
  //
  // `caller_cache_hit` says the caller's own residency cache considered this
  // range already handled and would have skipped the call entirely had the
  // verify oracle not been on. Only ever true under the oracle, and only used
  // to report what that cache was hiding.
  // The converted index cache's half of the same policy. That cache watches
  // the guest indices it converted from so that a conversion reused later in
  // the frame is dropped if the guest has rewritten them, and re-arms the
  // watch on every miss because the cache is emptied at every frame boundary -
  // which is the mprotect traffic left on this thread once the index path has
  // taken the plain guest DMA requests away from it.
  //
  // Nothing has to be copied for it: the converted indices already live in the
  // per-frame host index pool, so all the watch provides is the invalidation,
  // and the rule that replaces it is the one the vertex path already runs on -
  // a range made resident in a frame is trusted for the rest of that frame,
  // with the cache's own per-frame clear as the boundary. Refuses the same
  // shapes RequestVertexRange refuses, plus ranges the oracle has caught being
  // rewritten inside a frame.
  bool MayFingerprintConversionSource(const void* guest_source, uint32_t start, uint32_t length);

  // Audits that trust, and only under gpu_native_verify - the fingerprint is
  // taken in MayFingerprintConversionSource, before the conversion reads the
  // indices, and compared here every time the conversion is reused. A range
  // that fails is blacklisted, so it goes back to being watched for the rest
  // of the run, and the caller converts again from what guest memory holds.
  bool IsConversionSourceUnchanged(const void* guest_source, uint32_t start, uint32_t length);

  // True while the verify oracle wants to see every range of every draw, which
  // means the caller's own "this fetch constant has not changed" cache has to
  // be bypassed - it would otherwise hide exactly the intra-frame rewrites the
  // oracle exists to find. Expensive by construction, and only ever true with
  // gpu_native_verify set.
  bool VerifyBypassesCallerCache() const { return verify_frame_; }

 private:
  static constexpr uint32_t kResolvePageSizeLog2 = 12;
  static constexpr uint32_t kResolvePageCount = SharedMemory::kBufferSize >> kResolvePageSizeLog2;
  static constexpr size_t kResolvePageWords = kResolvePageCount / 64;
  static constexpr size_t kExpectedFrameRangeCount = 4096;
  static constexpr size_t kRangeCacheSetBits = 12;
  static constexpr size_t kRangeCacheSetCount = size_t{1} << kRangeCacheSetBits;

  // Fingerprint entries older than this many frames are dropped, so that a
  // level transition's worth of dead geometry does not stay in the table for
  // the rest of the run. At 60-100 fps this is 6-10 seconds.
  static constexpr uint64_t kEntryMaxAgeFrames = 600;
  // The sweep walks the whole table, so it does not run every frame. Any power
  // of two; entries therefore survive up to kEntryMaxAgeFrames + this.
  static constexpr uint64_t kEntrySweepPeriodFrames = 64;

  // A frame with fewer candidate draws than this is not evidence of anything -
  // menus and load screens issue a handful of draws, and one refused draw out
  // of two is not a fallback storm. Below it the latch neither trips nor
  // resets.
  static constexpr uint64_t kLatchMinCandidateDraws = 32;
  // Consecutive frames spent above a 50% fallback ratio before the master cvar
  // is cleared.
  static constexpr uint32_t kLatchFrameCount = 3;

  // The verify oracle's in-place-rewrite report is not self-limiting the way
  // the per-range blacklist is, and a run that hits it hits it every draw. Cap
  // the reports; the oracle keeps working after the log goes quiet.
  static constexpr uint32_t kVerifyWarningLimit = 32;

  struct Entry {
    // XXH3 of the guest bytes as of the last time they were copied in. Only
    // meaningful while hash_valid says it was taken.
    uint64_t hash = 0;
    // Whether `hash` holds a fingerprint at all. Ranges below
    // gpu_native_hash_min_bytes are made resident without being hashed, so
    // their entry carries an epoch and an upload sequence but no fingerprint,
    // and must never be compared against one. Kept as its own flag rather than
    // inferred from hash == 0, which XXH3 can legitimately return.
    bool hash_valid = false;
    // Frame epoch this range was last made resident in.
    uint64_t last_epoch = 0;
    // The shared memory buffer's page write sequence at the moment this range
    // was last copied in - see SharedMemory::MaxWriteSeq. The fingerprint says
    // what guest memory held; this says what the buffer held, and the entry is
    // only worth anything while both are still true. Unused for conversion
    // source entries, which are read out of guest memory and never copied into
    // the buffer at all.
    uint64_t upload_seq = 0;
    // Set by frame-ahead prefetch and cleared by the first draw that validates
    // the guest fingerprint. A prefetch is never trusted merely by epoch.
    bool prefetched = false;
    // Set by the verify oracle when the fingerprint was proven wrong for this
    // range: it is then copied on every request, with no dedup, for the rest
    // of the run.
    bool blacklisted = false;
    // Epoch in which this vertex range was first encountered for the next
    // prefetch wave. This replaces a second unordered set: the entry already
    // has the exact same key and stable node address.
    uint64_t prefetch_record_epoch = 0;
  };

  static constexpr uint64_t RangeKey(uint32_t start, uint32_t length) {
    return uint64_t(start) | (uint64_t(length) << 32);
  }

  // Converted index cache sources share the fingerprint table with the vertex
  // ranges, but must not share its entries: an entry's epoch stamp means "these
  // bytes are in the shared memory buffer", which is true of a vertex range
  // that was copied there and false of a conversion source, which is only ever
  // read. The tag bit keeps the two key spaces apart. Free by construction -
  // no range in a 512 MB buffer is 2 GiB long.
  static constexpr uint64_t kConversionSourceKeyBit = uint64_t(1) << 63;
  static constexpr uint64_t ConversionSourceKey(uint32_t start, uint32_t length) {
    return RangeKey(start, length) | kConversionSourceKeyBit;
  }

  // Copies the range in and updates its entry. `hash_valid` says whether
  // `hash` is a fingerprint of the bytes being copied or a placeholder for a
  // range this frame chose not to fingerprint. Returns false if the copy could
  // not be staged, in which case the entry is left saying nothing is resident.
  bool UploadRange(memory::Memory& memory, SharedMemory& shared_memory, uint32_t start,
                   uint32_t length, Entry& entry, uint64_t hash, bool hash_valid);

  uint64_t frame_epoch_ = 0;
  bool native_frame_ = false;
  bool native_index_frame_ = false;
  bool native_vertex_frame_ = false;
  bool dedup_frame_ = true;
  bool verify_frame_ = false;
  bool prefetch_frame_ = false;
  uint32_t hash_max_bytes_frame_ = 0;
  // Latched like the rest, and forced to 0 whenever verify_frame_ is set so
  // that the oracle never grades a frame that skipped its fingerprints.
  uint32_t hash_min_bytes_frame_ = 0;
  bool draw_index_residency_allowed_ = false;
  bool draw_vertex_residency_allowed_ = false;
  bool draw_index_native_ = false;
  bool draw_used_native_ = false;
  bool draw_fell_back_ = false;

  // Per-frame draw outcome, read and reset by the latch at the frame boundary.
  uint64_t frame_native_draws_ = 0;
  uint64_t frame_fallback_draws_ = 0;
  uint32_t latch_streak_ = 0;
  uint32_t verify_stale_warnings_ = 0;

  // Cross-frame fingerprints, keyed by (guest offset, length). Cleared
  // whenever the native path is switched on, so that nothing decided during an
  // emulated stretch is trusted afterwards.
  //
  // Note what the key does NOT provide: two entries whose windows overlap
  // describe the same buffer bytes, and an upload for either one rewrites the
  // other's window without going anywhere near its entry. Nothing in this
  // table can see that; the buffer's page write sequence is what does, on
  // every hit path in RequestVertexRange.
  std::unordered_map<uint64_t, Entry> range_table_;

  // A frame can cycle thousands of ranges through only 96 hardware fetch
  // slots, so a pointer retained per slot thrashes. Keep a small two-way
  // front-cache keyed by the complete range instead. The table node still
  // owns the entry and encounter order remains in current_frame_ranges_. Any
  // table erase bumps range_table_generation_, conservatively invalidating
  // every cached pointer before it can be dereferenced.
  struct CachedVertexRange {
    uint64_t key = 0;
    uint64_t generation = 0;
    Entry* entry = nullptr;
  };
  std::array<std::array<CachedVertexRange, 2>, kRangeCacheSetCount> cached_vertex_ranges_{};
  uint64_t range_table_generation_ = 1;

  struct VertexRange {
    uint32_t start;
    uint32_t length;
    // Scratch used only while this range is in prefetch_ranges_. References
    // into an unordered_map survive rehash; this lets the two-pass prefetch
    // stamp the exact entry it inserted without hashing the key a second time.
    uint64_t hash = 0;
    Entry* entry = nullptr;
  };
  std::vector<VertexRange> current_frame_ranges_;
  std::vector<VertexRange> prefetch_ranges_;

  // One bit per 4 KiB page of the shared memory buffer, set for every page any
  // resolve has ever written. 16 KiB, allocated once.
  std::vector<uint64_t> resolve_pages_;
};

}  // namespace rex::graphics::vulkan
