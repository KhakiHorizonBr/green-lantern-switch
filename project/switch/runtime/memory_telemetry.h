#pragma once

#include <cstdint>

namespace green_lantern::horizon {

// Starts a low-frequency process/guest memory sampler. The CSV is written to
// sdmc:/switch/green_lantern/logs/green_lantern_memory.csv.
void StartMemoryTelemetry();

// Called by the generated guest pool allocator for successful/failed requests
// routed through pool index 2 (STREAMDATA). Kept C-linkage for generated code.
extern "C" void GreenLanternRecordStreamDataAlloc(uint32_t requested_bytes,
                                                   uint32_t result_pointer,
                                                   uint32_t caller_lr);

}  // namespace green_lantern::horizon
