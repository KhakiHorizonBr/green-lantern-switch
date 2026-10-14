#include "memory_telemetry.h"

#include <malloc.h>
#include <switch.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

extern "C" {
size_t RexGmCommittedBytes(void);
size_t RexGmPoolBytes(void);
size_t RexGmMappedBytes(void);
size_t RexGmChunkCount(void);
bool RexSwitchGetVulkanMemoryBudget(uint64_t* usage, uint64_t* budget);
}

namespace green_lantern::horizon {
namespace {

constexpr const char* kLogPath =
    "sdmc:/switch/green_lantern/logs/green_lantern_memory.csv";
constexpr const char* kStreamDataLogPath =
    "sdmc:/switch/green_lantern/logs/green_lantern_streamdata_allocations.csv";
constexpr s64 kSampleIntervalNs = 1'000'000'000LL;
constexpr size_t kThreadStackSize = 64 * 1024;
constexpr size_t kMaxStreamDataCallers = 128;

struct StreamDataCallerStats {
  uint32_t caller_lr = 0;
  uint32_t max_request_bytes = 0;
  uint64_t calls = 0;
  uint64_t requested_bytes = 0;
  uint64_t successes = 0;
  uint64_t failures = 0;
};

StreamDataCallerStats g_streamdata_callers[kMaxStreamDataCallers]{};
uint64_t g_streamdata_overflow_callers = 0;
uint64_t g_streamdata_total_calls = 0;
uint64_t g_streamdata_total_bytes = 0;
uint64_t g_streamdata_total_successes = 0;
uint64_t g_streamdata_total_failures = 0;
uint32_t g_streamdata_global_max_request = 0;
std::atomic_flag g_streamdata_lock = ATOMIC_FLAG_INIT;

Thread g_sampler_thread{};
bool g_sampler_started = false;

void LockStreamDataStats() {
  while (g_streamdata_lock.test_and_set(std::memory_order_acquire)) {
    // The guest allocator already serializes many pool operations; keep this
    // critical section tiny and avoid heap allocation or file I/O here.
  }
}

void UnlockStreamDataStats() {
  g_streamdata_lock.clear(std::memory_order_release);
}

void WriteStreamDataSample(FILE* log, unsigned elapsed_seconds) {
  if (!log) return;

  StreamDataCallerStats snapshot[kMaxStreamDataCallers]{};
  uint64_t total_calls = 0;
  uint64_t total_bytes = 0;
  uint64_t successes = 0;
  uint64_t failures = 0;
  uint64_t overflow_events = 0;
  uint32_t global_max_request = 0;

  LockStreamDataStats();
  std::memcpy(snapshot, g_streamdata_callers, sizeof(snapshot));
  total_calls = g_streamdata_total_calls;
  total_bytes = g_streamdata_total_bytes;
  successes = g_streamdata_total_successes;
  failures = g_streamdata_total_failures;
  overflow_events = g_streamdata_overflow_callers;
  global_max_request = g_streamdata_global_max_request;
  UnlockStreamDataStats();

  std::fprintf(log,
               "summary,%u,,%llu,%llu,%llu,%llu,%u,%llu\n",
               elapsed_seconds, static_cast<unsigned long long>(total_calls),
               static_cast<unsigned long long>(total_bytes),
               static_cast<unsigned long long>(successes),
               static_cast<unsigned long long>(failures), global_max_request,
               static_cast<unsigned long long>(overflow_events));
  for (const auto& caller : snapshot) {
    if (caller.caller_lr == 0) continue;
    std::fprintf(log, "caller,%u,0x%08X,%llu,%llu,%llu,%llu,%u,\n", elapsed_seconds,
                 caller.caller_lr,
                 static_cast<unsigned long long>(caller.calls),
                 static_cast<unsigned long long>(caller.requested_bytes),
                 static_cast<unsigned long long>(caller.successes),
                 static_cast<unsigned long long>(caller.failures),
                 caller.max_request_bytes);
  }
  std::fflush(log);
}

void WriteSample(FILE* log, unsigned elapsed_seconds) {
  const struct mallinfo heap = mallinfo();
  u64 vulkan_usage = UINT64_MAX;
  u64 vulkan_budget = UINT64_MAX;
  (void)RexSwitchGetVulkanMemoryBudget(&vulkan_usage, &vulkan_budget);

  s64 resource_current = -1;
  s64 resource_limit = -1;
  u64 raw_handle = 0;
  if (R_SUCCEEDED(svcGetInfo(&raw_handle, InfoType_ResourceLimit, 0, 0))) {
    const Handle resource_limit_handle = static_cast<Handle>(raw_handle);
    (void)svcGetResourceLimitCurrentValue(&resource_current, resource_limit_handle,
                                          LimitableResource_Memory);
    (void)svcGetResourceLimitLimitValue(&resource_limit, resource_limit_handle,
                                        LimitableResource_Memory);
    svcCloseHandle(resource_limit_handle);
  }

  std::fprintf(log,
               "%u,%zu,%zu,%zu,%zu,%zu,%zu,%llu,%llu,%lld,%lld\n",
               elapsed_seconds, heap.uordblks, heap.fordblks,
               RexGmCommittedBytes(), RexGmPoolBytes(), RexGmMappedBytes(),
               RexGmChunkCount(), static_cast<unsigned long long>(vulkan_usage),
               static_cast<unsigned long long>(vulkan_budget),
               static_cast<long long>(resource_current),
               static_cast<long long>(resource_limit));
  std::fflush(log);
}

void SamplerMain(void*) {
  (void)::mkdir("sdmc:/switch/green_lantern/logs", 0777);
  FILE* log = std::fopen(kLogPath, "w");
  if (!log) return;
  FILE* streamdata_log = std::fopen(kStreamDataLogPath, "w");

  std::fprintf(log,
               "elapsed_s,malloc_used_bytes,malloc_free_bytes,guest_committed_bytes,"
               "guest_pool_reserved_bytes,guest_mapped_bytes,guest_chunks,"
               "vulkan_heap_usage_bytes,vulkan_heap_budget_bytes,"
               "resource_limit_current_bytes,resource_limit_max_bytes\n");
  std::fflush(log);
  if (streamdata_log) {
    std::fprintf(streamdata_log,
                 "record_type,elapsed_s,caller_lr,calls,requested_bytes,successes,failures,max_request_bytes,overflow_events\n");
    std::fflush(streamdata_log);
  }

  unsigned elapsed_seconds = 0;
  for (;;) {
    WriteSample(log, elapsed_seconds);
    WriteStreamDataSample(streamdata_log, elapsed_seconds);
    svcSleepThread(kSampleIntervalNs);
    ++elapsed_seconds;
  }
}

}  // namespace

extern "C" void GreenLanternRecordStreamDataAlloc(uint32_t requested_bytes,
                                                    uint32_t result_pointer,
                                                    uint32_t caller_lr) {
  LockStreamDataStats();
  ++g_streamdata_total_calls;
  g_streamdata_total_bytes += requested_bytes;
  if (result_pointer != 0) {
    ++g_streamdata_total_successes;
  } else {
    ++g_streamdata_total_failures;
  }
  if (requested_bytes > g_streamdata_global_max_request) {
    g_streamdata_global_max_request = requested_bytes;
  }

  StreamDataCallerStats* slot = nullptr;
  StreamDataCallerStats* empty = nullptr;
  for (auto& caller : g_streamdata_callers) {
    if (caller.caller_lr == caller_lr) {
      slot = &caller;
      break;
    }
    if (caller.caller_lr == 0 && !empty) empty = &caller;
  }
  if (!slot && empty && caller_lr != 0) {
    slot = empty;
    slot->caller_lr = caller_lr;
  }
  if (slot) {
    ++slot->calls;
    slot->requested_bytes += requested_bytes;
    if (result_pointer != 0) {
      ++slot->successes;
    } else {
      ++slot->failures;
    }
    if (requested_bytes > slot->max_request_bytes) {
      slot->max_request_bytes = requested_bytes;
    }
  } else {
    ++g_streamdata_overflow_callers;
  }
  UnlockStreamDataStats();
}

void StartMemoryTelemetry() {
  if (g_sampler_started) return;
  const Result create_result = threadCreate(
      &g_sampler_thread, SamplerMain, nullptr, nullptr, kThreadStackSize, 0x2B, -2);
  if (R_FAILED(create_result)) return;
  const Result start_result = threadStart(&g_sampler_thread);
  if (R_FAILED(start_result)) {
    threadClose(&g_sampler_thread);
    return;
  }
  g_sampler_started = true;
}

}  // namespace green_lantern::horizon
