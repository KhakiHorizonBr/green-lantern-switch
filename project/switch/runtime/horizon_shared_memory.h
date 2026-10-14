#pragma once

#include <switch.h>

#include <cstddef>

namespace green_lantern::horizon {

// Owns one Horizon shared-memory object and all of its mappings in this
// process. Callers choose free, page-aligned ASLR addresses for each alias.
class SharedMemoryBlock {
 public:
  SharedMemoryBlock() = default;
  ~SharedMemoryBlock();

  SharedMemoryBlock(const SharedMemoryBlock&) = delete;
  SharedMemoryBlock& operator=(const SharedMemoryBlock&) = delete;

  Result Create(size_t size, u32 owner_permission = Perm_Rw,
                u32 remote_permission = Perm_DontCare);
  Result MapAlias(void* address, u32 permission = Perm_Rw);
  Result UnmapAlias(void* address);
  Result Close();

  size_t size() const { return size_; }
  Handle handle() const { return handle_; }
  size_t alias_count() const { return alias_count_; }

 private:
  static constexpr size_t kPageSize = 0x1000;
  static constexpr size_t kMaxAliases = 16;

  Handle handle_ = INVALID_HANDLE;
  size_t size_ = 0;
  void* aliases_[kMaxAliases]{};
  size_t alias_count_ = 0;
};

}  // namespace green_lantern::horizon
