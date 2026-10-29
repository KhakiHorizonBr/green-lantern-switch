// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include <rex/logging.h>

namespace green_lantern::native::diagnostics {

inline uint64_t NowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

// CPU wall-clock observations only. A fence signal observed here is not the
// GPU execution start/end on this clock; the measured GPU duration is separate.
class Timeline {
 public:
  struct Event {
    const char* thread;
    const char* stage;
    uint64_t frame, submission, start, duration, value;
  };
  void Configure(bool enabled, const std::string& path) {
    enabled_ = false;
    if (!enabled) return;
    std::error_code error;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    file_ = std::fopen(path.c_str(), "w");
    if (!file_) { enabled_ = false; REXLOG_WARN("[gl-timeline] Cannot open {}", path); return; }
    try { events_.reserve(kCapacity); }
    catch (const std::bad_alloc&) {
      std::fclose(file_); file_ = nullptr;
      REXLOG_WARN("[gl-timeline] Disabled: no memory for diagnostic buffer"); return;
    }
    enabled_ = true;
    REXLOG_INFO("[gl-timeline] CPU observations -> {}; bounded {} events per flush", path, kCapacity);
  }
  bool enabled() const { return enabled_; }
  void Add(const char* thread, const char* stage, uint64_t frame, uint64_t submission,
           uint64_t start, uint64_t duration = 0, uint64_t value = 0) {
    if (!enabled_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_) return;
    if (events_.size() == kCapacity) { ++dropped_; return; }
    events_.push_back({thread, stage, frame, submission, start, duration, value});
  }
  void Flush() {
    if (!enabled_) return;
    std::vector<Event> batch;
    uint64_t dropped;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      events_.swap(batch);
      try { events_.reserve(kCapacity); }
      catch (const std::bad_alloc&) {
        enabled_ = false;
        REXLOG_WARN("[gl-timeline] Disabled: no memory for next diagnostic buffer");
      }
      dropped = dropped_; dropped_ = 0;
    }
    for (const Event& e : batch)
      std::fprintf(file_, "{\"thread\":\"%s\",\"stage\":\"%s\",\"frame\":%llu,\"submission\":%llu,\"start_ns\":%llu,\"duration_ns\":%llu,\"value\":%llu}\n",
          e.thread, e.stage, (unsigned long long)e.frame, (unsigned long long)e.submission,
          (unsigned long long)e.start, (unsigned long long)e.duration, (unsigned long long)e.value);
    if (dropped) {
      std::fprintf(file_, "{\"dropped_events\":%llu}\n", (unsigned long long)dropped);
      REXLOG_WARN("[gl-timeline] Dropped {} events (bounded diagnostic buffer)", dropped);
    }
    std::fflush(file_);
  }
  void Close() { Flush(); if (file_) { std::fclose(file_); file_ = nullptr; } enabled_ = false; }
 private:
  static constexpr size_t kCapacity = 32768;
  std::atomic<bool> enabled_{false};
  FILE* file_ = nullptr;
  std::mutex mutex_;
  std::vector<Event> events_;
  uint64_t dropped_ = 0;
};
inline Timeline& Trace() { static Timeline trace; return trace; }
class Scope {
 public:
  Scope(const char* thread, const char* stage, uint64_t frame, uint64_t submission = 0)
      : thread_(thread), stage_(stage), frame_(frame), submission_(submission),
        start_(Trace().enabled() ? NowNs() : 0) {}
  ~Scope() { if (start_) Trace().Add(thread_, stage_, frame_, submission_, start_, NowNs() - start_); }
 private:
  const char *thread_, *stage_;
  uint64_t frame_, submission_, start_;
};

// Exact nearest-rank percentiles of a bounded window, with explicit overflow.
class Distribution {
 public:
  void Add(uint64_t ns) {
    if (values_.size() == 8192) { ++dropped_; return; }
    try { values_.push_back(ns); } catch (const std::bad_alloc&) { ++dropped_; }
  }
  void Report(const char* name) {
    if (values_.empty()) return;
    std::sort(values_.begin(), values_.end());
    auto percentile = [&](size_t p) { return double(values_[(values_.size() * p + 99) / 100 - 1]) / 1e6; };
    size_t over_33 = 0, over_25 = 0;
    for (uint64_t ns : values_) { over_33 += ns > 33333333; over_25 += ns > 25000000; }
    REXLOG_INFO("[gl-frame-distribution] {}: n={} p50={:.3f} p95={:.3f} p99={:.3f} max={:.3f} ms; over33.33ms={} over25ms={} dropped={}",
        name, values_.size(), percentile(50), percentile(95), percentile(99), double(values_.back()) / 1e6,
        over_33, over_25, dropped_);
    values_.clear(); dropped_ = 0;
  }
 private:
  std::vector<uint64_t> values_;
  uint64_t dropped_ = 0;
};
inline Distribution& CompletedGpuSubmissions() { static Distribution d; return d; }

}  // namespace green_lantern::native::diagnostics
