// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_set>
#if defined(_WIN32)
#include <malloc.h>
#endif

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/graphics/register_file.h>
#include <rex/logging.h>

REXCVAR_DECLARE(std::string, gpu_compare_ps_hash);
REXCVAR_DECLARE(std::string, gpu_compare_resolve_address);
REXCVAR_DECLARE(std::string, dump_shaders);
REXCVAR_DECLARE(uint32_t, gpu_capture_frame);

#if REX_PLATFORM_SWITCH
extern "C" bool GreenLanternConsumeGraphicsCapture(void) __attribute__((weak));
#endif
namespace rex::graphics {

// Horizon FS IPC may temporarily change permissions of the pages supplied to
// a file write. Never lend live register/system state, or malloc pages shared
// with another object, to IPC while guest threads can modify that state.
inline bool WriteCaptureSnapshot(const std::string& path, const void* data, size_t bytes) {
  if (!data || !bytes) return false;
  constexpr size_t page = 4096;
  const size_t allocation = (bytes + page - 1) & ~(page - 1);
#if defined(_WIN32)
  void* snapshot = _aligned_malloc(allocation, page);
#else
  void* snapshot = std::aligned_alloc(page, allocation);
#endif
  if (!snapshot) {
    REXLOG_WARN("[gl-state] snapshot allocation failed: {} bytes", allocation);
    return false;
  }
  std::memcpy(snapshot, data, bytes);
  FILE* file = rex::filesystem::OpenFile(path, "wb");
  bool ok = false;
  if (file) {
    // Keep the IPC source in our isolated pages, not a shared stdio buffer.
    std::setvbuf(file, nullptr, _IONBF, 0);
    ok = std::fwrite(snapshot, 1, bytes, file) == bytes;
    ok = std::fclose(file) == 0 && ok;
  }
#if defined(_WIN32)
  _aligned_free(snapshot);
#else
  std::free(snapshot);
#endif
  return ok;
}

// Accessed only on the command processor thread. The input hook uses an atomic.
struct ManualResolveCapture {
  uint64_t frame = UINT64_MAX;
  uint32_t sessions = 0, files = 0;
  uint64_t bytes = 0;
};
inline ManualResolveCapture manual_resolve_capture;
inline bool ManualResolveCaptureActive(uint64_t frame) {
  return manual_resolve_capture.frame == frame;
}
inline void ArmManualResolveCapture(uint64_t next_frame) {
  auto& state = manual_resolve_capture;
  bool requested = false;
#if REX_PLATFORM_SWITCH
  requested = GreenLanternConsumeGraphicsCapture && GreenLanternConsumeGraphicsCapture();
#else
  requested = REXCVAR_GET(gpu_capture_frame) && next_frame == REXCVAR_GET(gpu_capture_frame);
#endif
  if (requested) {
    if (state.sessions >= 4) {
      REXLOG_WARN("[gl-pixels] four captures used; restart for more");
      return;
    }
    state.frame = next_frame;
    state.bytes = state.files = 0;
    ++state.sessions;
    REXLOG_INFO("[gl-pixels] FSI capture armed frame={} session={}/4", next_frame, state.sessions);
  }
}


inline bool WantsResolveComparison(uint32_t address, uint32_t bytes, uint64_t frame = UINT64_MAX - 1) {
  if (ManualResolveCaptureActive(frame)) {
    auto& s = manual_resolve_capture;
#if REX_PLATFORM_SWITCH
    constexpr uint64_t capture_budget = 64u << 20;
    constexpr uint32_t file_limit = 64;
#else
    constexpr uint64_t capture_budget = 512u << 20;
    constexpr uint32_t file_limit = 256;
#endif
    if (!bytes || bytes > (16u << 20) || s.files >= file_limit || s.bytes + bytes > capture_budget) {
      REXLOG_WARN("[gl-pixels] skipped resolve {:08X}, {} bytes; capture budget/size limit", address, bytes);
      return false;
    }
    ++s.files;
    s.bytes += bytes;
    return true;
  }
  const auto& filter = REXCVAR_GET(gpu_compare_resolve_address);
  if (filter.empty() || REXCVAR_GET(dump_shaders).empty() || !bytes || bytes > (32u << 20)) return false;
  char* end = nullptr;
  const uint64_t selected = std::strtoull(filter.c_str(), &end, 16);
  if (end == filter.c_str() || *end || selected > UINT32_MAX || selected != address) return false;
  static unsigned requests = 0;
  static auto first = std::chrono::steady_clock::now();
  static auto last = first;
  const auto now = std::chrono::steady_clock::now();
  // Let the initial menu pipelines warm up, then take four separated samples.
  if (requests >= 4 || now - first < std::chrono::seconds(5) ||
      (requests && now - last < std::chrono::seconds(2))) return false;
  ++requests;
  last = now;
  REXLOG_INFO("[gl-pixels] requesting synchronized resolve readback {}/4 at {:08X}, {} bytes",
              requests, address, bytes);
  return true;
}

// Input must be GPU-complete, CPU-visible readback data. This helper does not
// read guest RAM, write data back to the guest, or interpret a tiled surface as
// a linear image. The register snapshot preserves pitch/format/endianness.
inline void CaptureResolveComparison(const RegisterFile& regs, const char* backend,
                                     uint32_t address, uint32_t bytes, uint64_t frame,
                                     uint64_t vs, uint64_t ps, const void* pixels,
                                     const std::string& extra_metadata = "") {
  static uint32_t count = 0;
  static const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const bool manual = ManualResolveCaptureActive(frame);
  if (!pixels || !bytes || bytes > (32u << 20) || (!manual && count >= 4)) return;
  const auto directory = manual
#if REX_PLATFORM_SWITCH
      ? std::filesystem::path("sdmc:/switch/green_lantern/logs/capture/fsi")
#else
      ? std::filesystem::path("capture/fsi")
#endif
      : std::filesystem::path(REXCVAR_GET(dump_shaders)) / "resolve-compare" / fmt::format("{}-{}", backend, epoch);
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    REXLOG_WARN("[gl-pixels] directory failed: {}", error.message());
    return;
  }
  const uint32_t ordinal = manual ? manual_resolve_capture.files - 1 : count++;
  const auto stem = directory / fmt::format("resolve_{}_s{}_r{:02}_f{}_{:08X}", epoch,
      manual ? manual_resolve_capture.sessions : 0, ordinal, frame, address);
  auto write = [&](const char* suffix, const void* data, size_t length) {
    if (std::strcmp(suffix, ".registers.bin") == 0)
      return WriteCaptureSnapshot(stem.string() + suffix, data, length);
    FILE* file = rex::filesystem::OpenFile(stem.string() + suffix, "wb");
    if (!file) return false;
    bool complete = std::fwrite(data, 1, length, file) == length;
    return std::fclose(file) == 0 && complete;
  };
  const auto metadata = fmt::format(
      "{{\"backend\":\"{}\",\"address\":\"{:08X}\",\"bytes\":{},\"frame\":{},"
      "\"vs\":\"{:016X}\",\"ps\":\"{:016X}\",\"register_count\":{},"
      "\"layout\":\"raw guest resolve bytes from synchronized GPU readback; consult registers\"{} }}\n",
      manual ? "fsi" : backend, address, bytes, frame, vs, ps, RegisterFile::kRegisterCount, extra_metadata);
  const bool ok = write(".pixels.bin", pixels, bytes) &&
                  write(".registers.bin", regs.values, sizeof(regs.values)) &&
                  write(".json", metadata.data(), metadata.size());
  REXLOG_INFO("[gl-pixels] capture complete={} path={}", ok, stem.string());
}

// Bounded snapshots of distinct draw inputs in a manually selected frame.
// Float constants are saved, but omitted from the dedup key to bound animation.
inline void CaptureManualDrawInputs(const RegisterFile& regs, const char* backend,
    uint64_t frame, uint64_t vs, uint64_t ps, const void* system, size_t system_bytes) {
  static uint64_t saved_frame = UINT64_MAX;
  static std::unordered_set<std::string> layouts;
  static uint32_t ordinal = 0;
  if (frame != saved_frame) { saved_frame = frame; layouts.clear(); ordinal = 0; }
  if (layouts.size() >= 256) return;
  std::string key(reinterpret_cast<const char*>(&vs), sizeof(vs));
  key.append(reinterpret_cast<const char*>(&ps), sizeof(ps));
  key.append(reinterpret_cast<const char*>(&regs.values[0x2000]), 0x400 * sizeof(uint32_t));
  key.append(reinterpret_cast<const char*>(&regs.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0]),
      32 * 6 * sizeof(uint32_t));
  if (layouts.contains(key)) return;
#if REX_PLATFORM_SWITCH
  const auto dir = std::filesystem::path("sdmc:/switch/green_lantern/logs/capture") / backend;
#else
  const auto dir = std::filesystem::path("capture") / backend;
#endif
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  if (error) return;
  const auto stem = dir / fmt::format("draw_f{}_n{:03}_{:016X}_{:016X}", frame, ordinal++, vs, ps);
  const auto write = [&](const char* suffix, const void* data, size_t size) {
    if (std::strcmp(suffix, ".registers.bin") == 0 || std::strcmp(suffix, ".system.bin") == 0)
      return WriteCaptureSnapshot(stem.string() + suffix, data, size);
    FILE* f = rex::filesystem::OpenFile(stem.string() + suffix, "wb");
    if (!f) return false;
    bool ok = std::fwrite(data, 1, size, f) == size;
    return std::fclose(f) == 0 && ok;
  };
  auto json = fmt::format("{{\"backend\":\"{}\",\"frame\":{},\"vs\":\"{:016X}\",\"ps\":\"{:016X}\","
      "\"register_encoding\":\"little-endian uint32\",\"system_constant_bytes\":{},"
      "\"dedup\":\"first VS/PS/render-state/fetch combination in frame; not every draw\"}}\n",
      backend, frame, vs, ps, system_bytes);
  const bool ok = write(".registers.bin", regs.values, sizeof(regs.values)) &&
      write(".system.bin", system, system_bytes) && write(".json", json.data(), json.size());
  layouts.insert(std::move(key));
  REXLOG_INFO("[gl-state] manual inputs complete={} {}", ok, stem.string());
}

// Opt-in CPU snapshots, not a GPU replay or image capture. Compare constants by
// meaning, not by structure offsets: system constant layouts differ by backend.
// Each command processor records on its own ring thread. At most 64 distinct
// register-state layouts are saved for one selected guest pixel shader per run.
template <typename SystemConstants>
void CaptureDrawComparison(const RegisterFile& regs, const char* backend,
                           uint64_t vs, uint64_t ps, uint32_t indices,
                           const SystemConstants& constants) {
  const auto& filter = REXCVAR_GET(gpu_compare_ps_hash);
  if (filter.empty() || REXCVAR_GET(dump_shaders).empty()) return;
  char* end = nullptr;
  const uint64_t selected = std::strtoull(filter.c_str(), &end, 16);
  if (end == filter.c_str() || *end || selected != ps) return;
  static std::unordered_set<std::string> layouts;
  static uint32_t captured = 0;
  static bool failed = false;
  if (failed || captured >= 64) return;
  // Fetch constants and render state; omit animated float constants from the
  // deduplication key, but preserve every register in the actual snapshot.
  std::string key(reinterpret_cast<const char*>(&vs), sizeof(vs));
  key.append(reinterpret_cast<const char*>(&regs.values[0x2000]), 0x400 * sizeof(uint32_t));
  key.append(reinterpret_cast<const char*>(&regs.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0]),
             32 * 6 * sizeof(uint32_t));
  if (layouts.contains(key)) return;
  static const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const auto directory = std::filesystem::path(REXCVAR_GET(dump_shaders)) / "compare" /
                         fmt::format("{}-{}", backend, epoch);
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    failed = true;
    REXLOG_WARN("[gl-state] cannot create capture directory: {}", error.message());
    return;
  }
  const auto stem = directory / fmt::format("draw_{:02}_{:016X}_{:016X}", captured, vs, ps);
  auto write_blob = [&](const char* suffix, const void* data, size_t bytes) {
    FILE* file = rex::filesystem::OpenFile(stem.string() + suffix, "wb");
    if (!file) return false;
    const bool complete = std::fwrite(data, 1, bytes, file) == bytes;
    return std::fclose(file) == 0 && complete;
  };
  uint32_t bias[4];
  std::memcpy(bias, constants.color_exp_bias, sizeof(bias));
  const std::string metadata = fmt::format(
      "{{\"backend\":\"{}\",\"vs\":\"{:016X}\",\"ps\":\"{:016X}\","
      "\"index_count\":{},\"register_count\":{},\"register_encoding\":\"little-endian uint32\","
      "\"flags\":{},\"color_exp_bias_bits\":[{},{},{},{}],\"system_constant_bytes\":{}}}\n",
      backend, vs, ps, indices, RegisterFile::kRegisterCount, uint32_t(constants.flags),
      bias[0], bias[1], bias[2], bias[3], sizeof(constants));
  if (!write_blob(".registers.bin", regs.values, sizeof(regs.values)) ||
      !write_blob(".system.bin", &constants, sizeof(constants)) ||
      !write_blob(".json", metadata.data(), metadata.size())) {
    failed = true;
    REXLOG_WARN("[gl-state] incomplete draw snapshot; disabling capture for this run");
    return;
  }
  layouts.insert(std::move(key));
  ++captured;
  REXLOG_INFO("[gl-state] saved {}/64: {}", captured, stem.string());
}
}  // namespace rex::graphics
