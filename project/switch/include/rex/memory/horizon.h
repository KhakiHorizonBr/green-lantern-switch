#pragma once

#include <cstddef>

namespace rex::memory::horizon {
bool InitializeGuestArena(void** arena_base);
void ShutdownGuestArena();
bool IsPhysicalGuestRange(const void* address, size_t size);
bool CommitGuestRange(void* address, size_t size, void* mirror_address);
bool DecommitGuestRange(void* address, size_t size);
}  // namespace rex::memory::horizon
