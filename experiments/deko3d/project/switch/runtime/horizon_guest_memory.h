#pragma once

#include <switch.h>

#include <cstddef>

namespace green_lantern::horizon {

// Selects a free 4.5 GiB guest arena and installs the eagerly backed 512 MiB
// physical heap at the ReXGlue alias offsets. Virtual/XEX pages are committed
// separately through CommitGuestRange.
Result InitializeGuestMemory(void** out_arena_base);
void ShutdownGuestMemory();

bool IsPhysicalGuestRange(const void* address, size_t size);
Result CommitGuestRange(void* address, size_t size, void* mirror_address = nullptr);
Result DecommitGuestRange(void* address, size_t size);

}  // namespace green_lantern::horizon

namespace rex::memory::horizon {
bool InitializeGuestArena(void** arena_base);
void ShutdownGuestArena();
bool IsPhysicalGuestRange(const void* address, size_t size);
bool CommitGuestRange(void* address, size_t size, void* mirror_address);
bool DecommitGuestRange(void* address, size_t size);
}  // namespace rex::memory::horizon
