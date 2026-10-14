#pragma once

#include "horizon_shared_memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace green_lantern::horizon {

void SetSparseGuestArenaBase(void* base);

// Commits guest pages from lazily-created 16 MiB shared-memory slabs. A slab can
// be mapped at two guest aliases and is released after its last page is freed.
class SparseGuestPages {
 public:
  static constexpr size_t kPageSize = 0x1000;
  static constexpr size_t kSlabSize = 0x1000000;
  static constexpr size_t kPagesPerSlab = kSlabSize / kPageSize;

  SparseGuestPages() = default;
  ~SparseGuestPages();

  SparseGuestPages(const SparseGuestPages&) = delete;
  SparseGuestPages& operator=(const SparseGuestPages&) = delete;

  // Commits zero-filled pages, optionally mirroring their backing into a second
  // guest range. Ranges must be nonempty and page-aligned. A failed call rolls
  // back all pages newly committed by that call.
  Result Commit(void* guest_address, size_t size, void* mirror_address = nullptr);

  // Releases committed pages in the range; uncommitted pages are ignored.
  Result Decommit(void* guest_address, size_t size);
  Result Reset();

  bool IsCommitted(const void* guest_address) const;
  size_t committed_pages() const { return committed_pages_; }
  size_t mapped_slabs() const { return slabs_.size(); }
  const char* last_failure_stage() const { return last_failure_stage_; }
  uintptr_t last_failure_address() const { return last_failure_address_; }

 private:
  struct Slab {
    std::array<uint64_t, kPagesPerSlab / 64> committed{};
    std::unique_ptr<SharedMemoryBlock> backing;
    uintptr_t primary_address = 0;
    uintptr_t mirror_address = 0;
    size_t committed_count = 0;
  };

  uintptr_t FindSlabBase(uintptr_t guest_address) const;
  bool IsCommitted(const Slab& slab, size_t page_index) const;
  void MarkCommitted(Slab& slab, size_t page_index, bool committed);
  Result EnsureSlab(uintptr_t slab_base, uintptr_t mirror_base, Slab** out_slab);
  Result ReleaseEmptySlab(uintptr_t slab_base);

  std::unordered_map<uintptr_t, Slab> slabs_;
  size_t committed_pages_ = 0;
  const char* last_failure_stage_ = "none";
  uintptr_t last_failure_address_ = 0;
};

}  // namespace green_lantern::horizon
