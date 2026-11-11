#include "horizon_guest_memory.h"

#include "horizon_shared_memory.h"
#include "sparse_guest_pages.h"

#include <array>
#include <cstdint>
#include <cstdio>

extern "C" void RexSwitchRuntimeSetupDiagnostic(const char*);

namespace green_lantern::horizon {
void SetSparseGuestArenaBase(void* base);
namespace {
constexpr u64 kGuestArenaSize = 0x120000000ull;
constexpr u64 kPageSize = 0x1000;
constexpr u64 kXpsViewOffset = 0x7F000000;
constexpr u64 kPhysicalViewA = 0xA0000000;
constexpr u64 kPhysicalViewC = 0xC0000000;
constexpr u64 kPhysicalViewE = 0xE0000000;
constexpr u64 kPhysicalRawView = 0x100000000;
constexpr std::array<size_t, 11> kPhysicalChunks = {
    0x01000000, 0x04000000, 0x04000000, 0x04000000, 0x04000000, 0x04000000,
    0x04000000, 0x04000000, 0x01000000, 0x01000000, 0x01000000,
};

std::array<SharedMemoryBlock, kPhysicalChunks.size()> g_physical_blocks;
SparseGuestPages g_sparse_pages;
void* g_arena_base = nullptr;

void ReportInitFailure(const char* stage, Result result, size_t chunk = 0,
                       u64 guest_offset = 0) {
  char message[160];
  std::snprintf(message, sizeof(message),
                "guest arena init: %s; result=0x%08X; chunk=%u; guest_offset=0x%llX",
                stage, static_cast<unsigned>(result), static_cast<unsigned>(chunk),
                static_cast<unsigned long long>(guest_offset));
  RexSwitchRuntimeSetupDiagnostic(message);
}

bool FindGuestArena(void** out_base) {
  u64 aslr_base = 0, aslr_size = 0, heap_base = 0, heap_size = 0;
  if (R_FAILED(svcGetInfo(&aslr_base, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0)) ||
      R_FAILED(svcGetInfo(&aslr_size, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0)) ||
      R_FAILED(svcGetInfo(&heap_base, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0)) ||
      R_FAILED(svcGetInfo(&heap_size, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0)) ||
      aslr_base + aslr_size < aslr_base || heap_base + heap_size < heap_base) {
    return false;
  }

  const u64 aslr_end = aslr_base + aslr_size;
  const u64 heap_end = heap_base + heap_size;
  u64 best_base = 0;
  u64 best_size = 0;
  for (u64 cursor = aslr_base; cursor < aslr_end;) {
    MemoryInfo info{};
    u32 page_info = 0;
    if (R_FAILED(svcQueryMemory(&info, &page_info, cursor)) || info.size == 0 ||
        info.addr + info.size <= cursor) {
      return false;
    }
    const u64 region_end = info.addr + info.size;
    if (info.type == MemType_Unmapped) {
      const u64 begin = info.addr > aslr_base ? info.addr : aslr_base;
      const u64 end = region_end < aslr_end ? region_end : aslr_end;
      const bool overlaps_heap = begin < heap_end && end > heap_base;
      if (!overlaps_heap && end >= begin && end - begin >= kGuestArenaSize &&
          end - begin > best_size) {
        best_base = (begin + kPageSize - 1) & ~(kPageSize - 1);
        best_size = end - begin;
      }
    }
    cursor = region_end;
  }
  if (!best_base || best_size < kGuestArenaSize) return false;
  *out_base = reinterpret_cast<void*>(best_base);
  return true;
}

void ReleasePhysicalBlocks() {
  for (size_t i = g_physical_blocks.size(); i > 0; --i) {
    g_physical_blocks[i - 1].Close();
  }
}

Result MapPhysicalBacking(void* arena_base) {
  auto* const base = static_cast<u8*>(arena_base);
  size_t backing_offset = 0;
  for (size_t chunk = 0; chunk < g_physical_blocks.size(); ++chunk) {
    auto& block = g_physical_blocks[chunk];
    const size_t chunk_size = kPhysicalChunks[chunk];
    Result result = block.Create(chunk_size);
    if (R_FAILED(result)) {
      ReportInitFailure("svcCreateSharedMemory", result, chunk, backing_offset);
      ReleasePhysicalBlocks();
      return result;
    }

    if (chunk == 0) {
      result = block.MapAlias(base + kXpsViewOffset);
      if (R_FAILED(result)) {
        ReportInitFailure("svcMapSharedMemory XPS alias", result, chunk, kXpsViewOffset);
        break;
      }
    }
    const u64 guest_offsets[] = {kPhysicalViewA, kPhysicalViewC, kPhysicalViewE,
                                 kPhysicalRawView};
    for (u64 guest_offset : guest_offsets) {
      result = block.MapAlias(base + guest_offset + backing_offset);
      if (R_FAILED(result)) {
        ReportInitFailure("svcMapSharedMemory physical alias", result, chunk,
                          guest_offset + backing_offset);
        break;
      }
    }
    if (R_FAILED(result)) break;
    backing_offset += chunk_size;
  }
  if (backing_offset != 0x20000000ull) {
    ReleasePhysicalBlocks();
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
  }
  return 0;
}
}  // namespace

Result InitializeGuestMemory(void** out_arena_base) {
  if (!out_arena_base) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
  if (g_arena_base) {
    *out_arena_base = g_arena_base;
    return 0;
  }
  if (!FindGuestArena(&g_arena_base)) {
    const Result result = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    ReportInitFailure("no contiguous 4.5 GiB free ASLR range", result);
    return result;
  }
  const Result result = MapPhysicalBacking(g_arena_base);
  if (R_FAILED(result)) {
    g_arena_base = nullptr;
    return result;
  }
  SetSparseGuestArenaBase(g_arena_base);
  *out_arena_base = g_arena_base;
  return 0;
}

void ShutdownGuestMemory() {
  if (R_FAILED(g_sparse_pages.Reset())) return;
  SetSparseGuestArenaBase(nullptr);
  ReleasePhysicalBlocks();
  g_arena_base = nullptr;
}

bool IsPhysicalGuestRange(const void* address, size_t size) {
  if (!g_arena_base || !size) return false;
  const uintptr_t base = reinterpret_cast<uintptr_t>(g_arena_base);
  const uintptr_t value = reinterpret_cast<uintptr_t>(address);
  if (value < base || value - base > kGuestArenaSize || size > kGuestArenaSize - (value - base)) {
    return false;
  }
  const u64 offset = value - base;
  const u64 end = offset + size;
  return (offset >= kXpsViewOffset && end <= 0x80000000ull) ||
         (offset >= kPhysicalViewA && end <= 0x120000000ull);
}

Result CommitGuestRange(void* address, size_t size, void* mirror_address) {
  if (!g_arena_base) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
  return g_sparse_pages.Commit(address, size, mirror_address);
}

Result DecommitGuestRange(void* address, size_t size) {
  if (!g_arena_base) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
  return g_sparse_pages.Decommit(address, size);
}

void* GuestArenaBase() { return g_arena_base; }

}  // namespace green_lantern::horizon

namespace rex::memory::horizon {
bool InitializeGuestArena(void** arena_base) {
  return R_SUCCEEDED(green_lantern::horizon::InitializeGuestMemory(arena_base));
}
void ShutdownGuestArena() { green_lantern::horizon::ShutdownGuestMemory(); }
bool IsPhysicalGuestRange(const void* address, size_t size) {
  return green_lantern::horizon::IsPhysicalGuestRange(address, size);
}
bool CommitGuestRange(void* address, size_t size, void*) {
  const uintptr_t arena = reinterpret_cast<uintptr_t>(green_lantern::horizon::GuestArenaBase());
  const uintptr_t value = reinterpret_cast<uintptr_t>(address);
  if (!arena || value < arena) return false;
  const u64 offset = value - arena;
  void* mirror = nullptr;
  if (offset >= 0x80000000ull && offset < 0x90000000ull) {
    mirror = reinterpret_cast<void*>(value + 0x10000000ull);
  } else if (offset >= 0x90000000ull && offset < 0xA0000000ull) {
    mirror = reinterpret_cast<void*>(value - 0x10000000ull);
  }
  return R_SUCCEEDED(green_lantern::horizon::CommitGuestRange(address, size, mirror));
}
bool DecommitGuestRange(void* address, size_t size) {
  return R_SUCCEEDED(green_lantern::horizon::DecommitGuestRange(address, size));
}
}  // namespace rex::memory::horizon
