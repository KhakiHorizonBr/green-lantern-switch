#include "memory_telemetry.h"

#include <malloc.h>
#include <switch.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// Consulta o mapeamento host sem acessar o endereço guest investigado.
extern "C" void GreenLanternTraceVector(uint64_t base, uint32_t phase,
                                        const uint64_t* v) {
  static std::atomic<uint32_t> initial_samples{0};
  static std::atomic<uint32_t> invalid_samples{0};
  auto readable = [base](uint32_t address, uint32_t length) {
    MemoryInfo info{};
    uint32_t page_info = 0;
    const uint64_t host = base + address;
    const Result rc = svcQueryMemory(&info, &page_info, host);
    return R_SUCCEEDED(rc) && (info.perm & Perm_R) && host >= info.addr &&
           length <= info.size && host - info.addr <= info.size - length;
  };
  bool valid = true;
  if (phase == 2) {
    valid = readable(uint32_t(v[5]) & ~15u, 256) &&
            readable(uint32_t(v[6]) & ~15u, 464);
  }
  const bool early = phase != 2 && initial_samples.fetch_add(1) < 8;
  if (!early && (valid || invalid_samples.fetch_add(1) >= 8)) return;
  uint32_t table = 0;
  if (readable(0x82B4FE48, 4)) {
    std::memcpy(&table, reinterpret_cast<const void*>(base + 0x82B4FE48), 4);
    table = __builtin_bswap32(table);
  }
  std::fprintf(stderr, "[gl-vector] phase=%u valid=%u table=%08X sp=%08X "
      "r3=%08X r4=%08X r5=%08X r6=%08X r10=%08X r11=%08X r28=%08X lr=%08X\n",
      phase, unsigned(valid), table, uint32_t(v[0]), uint32_t(v[1]),
      uint32_t(v[2]), uint32_t(v[3]), uint32_t(v[4]), uint32_t(v[5]),
      uint32_t(v[6]), uint32_t(v[7]), uint32_t(v[8]));
  std::fflush(stderr);
}

extern "C" {
size_t RexGmCommittedBytes(void);
size_t RexGmPoolBytes(void);
size_t RexGmMappedBytes(void);
size_t RexGmChunkCount(void);
bool RexSwitchGetVulkanMemoryBudget(uint64_t* usage, uint64_t* budget);
size_t RexSwitchGetMemoryAreas(uint64_t* values, size_t capacity);
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

  FILE* areas_log = std::fopen("sdmc:/switch/green_lantern/logs/green_lantern_memory_areas.csv", "w");
  if (areas_log) std::fprintf(areas_log, "elapsed_s,area,live_bytes,peak_bytes,allocations,failures,last_failed_bytes,max_request_bytes,tracking_overflow\n");
  unsigned elapsed_seconds = 0;
  for (;;) {
    WriteSample(log, elapsed_seconds);
    if (areas_log) {
      uint64_t values[43] = {};
      RexSwitchGetMemoryAreas(values, 43);
      const char* names[7] = {"vk_other", "buffers_other", "upload_staging", "render_targets",
                              "sampled_images", "storage_images", "native_upload_ring"};
      for (unsigned i = 0; i < 7; ++i) {
        std::fprintf(areas_log, "%u,%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n", elapsed_seconds, names[i],
          (unsigned long long)values[i*6], (unsigned long long)values[i*6+1],
          (unsigned long long)values[i*6+2], (unsigned long long)values[i*6+3],
          (unsigned long long)values[i*6+4], (unsigned long long)values[i*6+5],
          (unsigned long long)values[42]);
      }
      std::fflush(areas_log);
    }

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
  if (result_pointer == 0 && requested_bytes != 0) {
    std::fprintf(stderr, "[gl-streamdata] allocation failed: bytes=%u caller=%08X\n",
                 requested_bytes, caller_lr);
    std::fflush(stderr);
  }
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
