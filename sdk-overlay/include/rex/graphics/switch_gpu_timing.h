#pragma once

#include <chrono>
#include <cstdint>

extern "C" void RexSwitchPerfCount(unsigned id);
extern "C" void RexSwitchPerfAdd(unsigned id, uint64_t value);

namespace rex::graphics::switch_timing {
// IDs 0..34 belong to the original Switch profiler. Times below are cumulative
// wall microseconds; nested stages overlap and must never be added together.
enum Counter : unsigned {
  kLastPipelineHit = 35,
  kPipelineMapHit,
  kPipelineMiss,
  kShaderTranslationCount,
  kShaderTranslationUs,
  kPipelineDriverUs,
  kPipelineDriverFailure,
  kConfigurePipelineUs,
  kFenceWaitUs,
  kFenceWaitCount,
  kDrawCpuUs,
  kResolveCpuUs,
  kEndSubmissionCpuUs,
  kGpuFrameSpanUs,
  kGpuFrameSpanCount,
  kCounterEnd,
};

inline uint64_t NowUs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

class Scope {
 public:
  explicit Scope(Counter counter) : counter_(counter), start_(NowUs()) {}
  ~Scope() { RexSwitchPerfAdd(counter_, NowUs() - start_); }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
 private:
  Counter counter_;
  uint64_t start_;
};
}  // namespace rex::graphics::switch_timing
