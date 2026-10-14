#include "horizon_shared_memory.h"

namespace green_lantern::horizon {

SharedMemoryBlock::~SharedMemoryBlock() { Close(); }

Result SharedMemoryBlock::Create(size_t size, u32 owner_permission, u32 remote_permission) {
  if (handle_ != INVALID_HANDLE) {
    return MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
  }
  if (size == 0 || (size & (kPageSize - 1)) != 0) {
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
  }
  if (!envIsSyscallHinted(0x50)) {
    return KERNELRESULT(NotImplemented);
  }

  const Result result =
      svcCreateSharedMemory(&handle_, size, owner_permission, remote_permission);
  if (R_SUCCEEDED(result)) {
    size_ = size;
  } else {
    handle_ = INVALID_HANDLE;
  }
  return result;
}

Result SharedMemoryBlock::MapAlias(void* address, u32 permission) {
  if (handle_ == INVALID_HANDLE) {
    return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
  }
  if (reinterpret_cast<uintptr_t>(address) & (kPageSize - 1)) {
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
  }
  if (alias_count_ == kMaxAliases) {
    return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
  }
  for (size_t i = 0; i < alias_count_; ++i) {
    if (aliases_[i] == address) {
      return MAKERESULT(Module_Libnx, LibnxError_AlreadyMapped);
    }
  }

  const Result result = svcMapSharedMemory(handle_, address, size_, permission);
  if (R_SUCCEEDED(result)) {
    aliases_[alias_count_++] = address;
  }
  return result;
}

Result SharedMemoryBlock::UnmapAlias(void* address) {
  if (handle_ == INVALID_HANDLE) {
    return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
  }
  size_t index = 0;
  while (index < alias_count_ && aliases_[index] != address) {
    ++index;
  }
  if (index == alias_count_) {
    return MAKERESULT(Module_Libnx, LibnxError_NotFound);
  }

  const Result result = svcUnmapSharedMemory(handle_, address, size_);
  if (R_SUCCEEDED(result)) {
    for (size_t i = index + 1; i < alias_count_; ++i) {
      aliases_[i - 1] = aliases_[i];
    }
    aliases_[--alias_count_] = nullptr;
  }
  return result;
}

Result SharedMemoryBlock::Close() {
  while (alias_count_ > 0) {
    const Result result = UnmapAlias(aliases_[alias_count_ - 1]);
    if (R_FAILED(result)) {
      return result;
    }
  }
  if (handle_ == INVALID_HANDLE) {
    return 0;
  }

  const Result result = svcCloseHandle(handle_);
  if (R_SUCCEEDED(result)) {
    handle_ = INVALID_HANDLE;
    size_ = 0;
  }
  return result;
}

}  // namespace green_lantern::horizon
