#include "sparse_guest_pages.h"

#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace green_lantern::horizon {
namespace {
uintptr_t g_arena_base = 0;
}

SparseGuestPages::~SparseGuestPages() { Reset(); }

uintptr_t SparseGuestPages::FindSlabBase(uintptr_t guest_address) const {
  if (!g_arena_base || guest_address < g_arena_base) return 0;
  const uintptr_t offset = guest_address - g_arena_base;
  return g_arena_base + (offset & ~(uintptr_t(kSlabSize) - 1));
}

bool SparseGuestPages::IsCommitted(const Slab& slab, size_t page_index) const {
  return (slab.committed[page_index >> 6] & (uint64_t(1) << (page_index & 63))) != 0;
}

void SparseGuestPages::MarkCommitted(Slab& slab, size_t page_index, bool committed) {
  uint64_t& word = slab.committed[page_index >> 6];
  const uint64_t mask = uint64_t(1) << (page_index & 63);
  const bool was_committed = (word & mask) != 0;
  if (was_committed == committed) return;
  if (committed) {
    word |= mask;
    ++slab.committed_count;
    ++committed_pages_;
  } else {
    word &= ~mask;
    --slab.committed_count;
    --committed_pages_;
  }
}

Result SparseGuestPages::EnsureSlab(uintptr_t slab_base, uintptr_t mirror_base,
                                    Slab** out_slab) {
  const auto found = slabs_.find(slab_base);
  if (found != slabs_.end()) {
    if (found->second.mirror_address != mirror_base) {
      return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    *out_slab = &found->second;
    return 0;
  }

  Slab slab{};
  slab.primary_address = slab_base;
  slab.mirror_address = mirror_base;
  try {
    slab.backing = std::make_unique<SharedMemoryBlock>();
  } catch (...) {
    last_failure_stage_ = "allocate slab metadata";
    last_failure_address_ = slab_base;
    return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
  }
  Result result = slab.backing->Create(kSlabSize);
  if (R_FAILED(result)) {
    last_failure_stage_ = "svcCreateSharedMemory";
    last_failure_address_ = slab_base;
  }
  if (R_SUCCEEDED(result)) {
    result = slab.backing->MapAlias(reinterpret_cast<void*>(slab_base));
    if (R_FAILED(result)) {
      last_failure_stage_ = "svcMapSharedMemory primary";
      last_failure_address_ = slab_base;
    }
  }
  if (R_SUCCEEDED(result) && mirror_base) {
    result = slab.backing->MapAlias(reinterpret_cast<void*>(mirror_base));
    if (R_FAILED(result)) {
      last_failure_stage_ = "svcMapSharedMemory mirror";
      last_failure_address_ = mirror_base;
    }
  }
  if (R_FAILED(result)) {
    slab.backing->Close();
    return result;
  }

  try {
    auto [it, inserted] = slabs_.emplace(slab_base, std::move(slab));
    if (!inserted) {
      it->second.backing->Close();
      last_failure_stage_ = "insert slab tracking";
      last_failure_address_ = slab_base;
      return MAKERESULT(Module_Libnx, LibnxError_AlreadyMapped);
    }
    *out_slab = &it->second;
  } catch (...) {
    // The moved slab's destructor closes the shared object and both mappings.
    last_failure_stage_ = "allocate slab tracking";
    last_failure_address_ = slab_base;
    return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
  }
  return 0;
}

Result SparseGuestPages::ReleaseEmptySlab(uintptr_t slab_base) {
  const auto it = slabs_.find(slab_base);
  if (it == slabs_.end()) return 0;
  if (it->second.committed_count != 0) return 0;
  const Result result = it->second.backing->Close();
  if (R_FAILED(result)) return result;
  slabs_.erase(it);
  return 0;
}

Result SparseGuestPages::Commit(void* guest_address, size_t size, void* mirror_address) {
  last_failure_stage_ = "none";
  last_failure_address_ = 0;
  uintptr_t begin = reinterpret_cast<uintptr_t>(guest_address);
  uintptr_t mirror_begin = reinterpret_cast<uintptr_t>(mirror_address);
  const uintptr_t max_address = std::numeric_limits<uintptr_t>::max();
  const bool has_mirror = mirror_address != nullptr;
  if (!size || (begin & (kPageSize - 1)) || (size & (kPageSize - 1)) ||
      begin > max_address - size ||
      (has_mirror && ((mirror_begin & (kPageSize - 1)) || mirror_begin > max_address - size)) ||
      !g_arena_base || begin < g_arena_base || (has_mirror && mirror_begin < g_arena_base)) {
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
  }

  if (has_mirror) {
    if (mirror_begin > begin) {
      if (mirror_begin - begin != 0x10000000ull) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
      }
    } else {
      if (begin - mirror_begin != 0x10000000ull) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
      }
      std::swap(begin, mirror_begin);
    }
  }

  for (size_t offset = 0; offset < size; offset += kPageSize) {
    const uintptr_t page = begin + offset;
    const uintptr_t slab_base = FindSlabBase(page);
    const auto slab = slabs_.find(slab_base);
    if (slab != slabs_.end()) {
      const size_t page_index = (page - slab_base) / kPageSize;
      if (IsCommitted(slab->second, page_index)) {
        return MAKERESULT(Module_Libnx, LibnxError_AlreadyMapped);
      }
    }
  }

  const uintptr_t rollback_end = begin + size;
  for (size_t offset = 0; offset < size; offset += kPageSize) {
    const uintptr_t page = begin + offset;
    const uintptr_t slab_base = FindSlabBase(page);
    const uintptr_t page_offset = page - slab_base;
    const uintptr_t mirror_page = has_mirror ? mirror_begin + offset : 0;
    const uintptr_t mirror_slab = has_mirror ? mirror_page - page_offset : 0;
    Slab* slab = nullptr;
    Result result = EnsureSlab(slab_base, mirror_slab, &slab);
    if (R_FAILED(result)) {
      if (last_failure_address_ == 0) {
        last_failure_stage_ = "validate or find slab";
        last_failure_address_ = slab_base;
      }
      Decommit(reinterpret_cast<void*>(begin), offset);
      for (uintptr_t empty = begin; empty < rollback_end; empty += kSlabSize) {
        const uintptr_t base = FindSlabBase(empty);
        ReleaseEmptySlab(base);
      }
      return result;
    }
    const size_t page_index = page_offset / kPageSize;
    std::memset(reinterpret_cast<void*>(page), 0, kPageSize);
    MarkCommitted(*slab, page_index, true);
  }
  return 0;
}

Result SparseGuestPages::Decommit(void* guest_address, size_t size) {
  uintptr_t begin = reinterpret_cast<uintptr_t>(guest_address);
  const uintptr_t max_address = std::numeric_limits<uintptr_t>::max();
  if (!size || (begin & (kPageSize - 1)) || (size & (kPageSize - 1)) ||
      begin > max_address - size || !g_arena_base || begin < g_arena_base) {
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
  }
  const uintptr_t guest_offset = begin - g_arena_base;
  if (guest_offset >= 0x90000000ull && guest_offset < 0xA0000000ull) {
    begin -= 0x10000000ull;
  }

  for (size_t offset = 0; offset < size; offset += kPageSize) {
    const uintptr_t page = begin + offset;
    const uintptr_t slab_base = FindSlabBase(page);
    auto slab_it = slabs_.find(slab_base);
    if (slab_it == slabs_.end()) continue;
    const size_t page_index = (page - slab_base) / kPageSize;
    if (!IsCommitted(slab_it->second, page_index)) continue;

    std::memset(reinterpret_cast<void*>(page), 0, kPageSize);
    if (slab_it->second.committed_count == 1) {
      const Result result = slab_it->second.backing->Close();
      if (R_FAILED(result)) return result;
      MarkCommitted(slab_it->second, page_index, false);
      slabs_.erase(slab_it);
    } else {
      MarkCommitted(slab_it->second, page_index, false);
    }
  }
  return 0;
}

Result SparseGuestPages::Reset() {
  while (!slabs_.empty()) {
    const uintptr_t base = slabs_.begin()->first;
    const Result result = slabs_.begin()->second.backing->Close();
    if (R_FAILED(result)) return result;
    committed_pages_ -= slabs_.begin()->second.committed_count;
    slabs_.erase(base);
  }
  return 0;
}

bool SparseGuestPages::IsCommitted(const void* guest_address) const {
  uintptr_t address = reinterpret_cast<uintptr_t>(guest_address);
  if (!g_arena_base || address < g_arena_base) return false;
  const uintptr_t guest_offset = address - g_arena_base;
  if (guest_offset >= 0x90000000ull && guest_offset < 0xA0000000ull) {
    address -= 0x10000000ull;
  }
  const uintptr_t slab_base = FindSlabBase(address);
  const auto slab = slabs_.find(slab_base);
  if (slab == slabs_.end()) return false;
  const uintptr_t aligned = address & ~(uintptr_t(kPageSize) - 1);
  const size_t page_index = (aligned - slab_base) / kPageSize;
  return IsCommitted(slab->second, page_index);
}

// Installed by horizon_guest_memory.cpp once it has selected the guest arena.
void SetSparseGuestArenaBase(void* base) { g_arena_base = reinterpret_cast<uintptr_t>(base); }

}  // namespace green_lantern::horizon
