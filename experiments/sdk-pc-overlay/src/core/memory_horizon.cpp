#include <rex/memory/horizon.h>
#include <rex/memory/utils.h>

#include <filesystem>

namespace rex::memory {

size_t page_size() { return 0x1000; }
size_t allocation_granularity() { return 0x1000; }
bool IsWritableExecutableMemorySupported() { return false; }

void* AllocFixed(void* base_address, size_t length, AllocationType allocation_type,
                 PageAccess access) {
  (void)access;
  if (!base_address || !length) return nullptr;
  if (allocation_type == AllocationType::kReserve) return base_address;
  if (horizon::IsPhysicalGuestRange(base_address, length)) return base_address;

  // Alias pairing and arena-relative address handling live in the libnx bridge.
  void* mirror = nullptr;
  return horizon::CommitGuestRange(base_address, length, mirror) ? base_address : nullptr;
}

bool DeallocFixed(void* base_address, size_t length, DeallocationType type) {
  if (type == DeallocationType::kRelease || horizon::IsPhysicalGuestRange(base_address, length)) {
    return true;
  }
  return horizon::DecommitGuestRange(base_address, length);
}

bool Protect(void* base_address, size_t length, PageAccess access, PageAccess* old_access) {
  (void)base_address;
  (void)length;
  (void)access;
  if (old_access) *old_access = PageAccess::kReadWrite;
  // Shared-memory mappings on Horizon are always RW; guest permissions are
  // tracked by BaseHeap but cannot currently be enforced with libnx SVCs.
  return true;
}

bool QueryProtect(void* base_address, size_t& length, PageAccess& access_out) {
  (void)base_address;
  length = 0;
  access_out = PageAccess::kReadWrite;
  return false;
}

FileMappingHandle CreateFileMappingHandle(const std::filesystem::path&, size_t, PageAccess,
                                          bool) {
  return kFileMappingHandleInvalid;
}
void CloseFileMappingHandle(FileMappingHandle, const std::filesystem::path&) {}
void* MapFileView(FileMappingHandle, void*, size_t, PageAccess, size_t) { return nullptr; }
bool UnmapFileView(FileMappingHandle, void*, size_t) { return false; }

}  // namespace rex::memory
