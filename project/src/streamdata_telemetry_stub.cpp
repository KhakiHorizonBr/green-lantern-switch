// The generated PPC code includes an optional STREAMDATA telemetry hook used
// by the Switch memory probes. Desktop builds do not collect that telemetry.
// The Switch target provides the real implementation in its runtime sources.
#include <cstdint>

extern "C" void GreenLanternRecordStreamDataAlloc(uint32_t requested_bytes,
                                                   uint32_t result_pointer,
                                                   uint32_t caller_lr) {
  (void)requested_bytes;
  (void)result_pointer;
  (void)caller_lr;
}
