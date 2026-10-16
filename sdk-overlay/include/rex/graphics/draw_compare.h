// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_set>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/graphics/register_file.h>
#include <rex/logging.h>

REXCVAR_DECLARE(std::string, gpu_compare_ps_hash);
REXCVAR_DECLARE(std::string, gpu_compare_resolve_address);
REXCVAR_DECLARE(std::string, dump_shaders);

namespace rex::graphics {

inline bool WantsResolveComparison(uint32_t address, uint32_t bytes) {
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
                                     uint64_t vs, uint64_t ps, const void* pixels) {
  static uint32_t count = 0;
  static const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  if (!pixels || !bytes || bytes > (32u << 20) || count >= 4) return;
  const auto directory = std::filesystem::path(REXCVAR_GET(dump_shaders)) /
                         "resolve-compare" / fmt::format("{}-{}", backend, epoch);
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    REXLOG_WARN("[gl-pixels] directory failed: {}", error.message());
    return;
  }
  const auto stem = directory / fmt::format("resolve_{:02}_f{}_{:08X}", count++, frame, address);
  auto write = [&](const char* suffix, const void* data, size_t length) {
    FILE* file = rex::filesystem::OpenFile(stem.string() + suffix, "wb");
    if (!file) return false;
    bool complete = std::fwrite(data, 1, length, file) == length;
    return std::fclose(file) == 0 && complete;
  };
  const auto metadata = fmt::format(
      "{{\"backend\":\"{}\",\"address\":\"{:08X}\",\"bytes\":{},\"frame\":{},"
      "\"vs\":\"{:016X}\",\"ps\":\"{:016X}\",\"register_count\":{},"
      "\"layout\":\"raw guest resolve bytes from synchronized GPU readback; consult registers\"}}\n",
      backend, address, bytes, frame, vs, ps, RegisterFile::kRegisterCount);
  const bool ok = write(".pixels.bin", pixels, bytes) &&
                  write(".registers.bin", regs.values, sizeof(regs.values)) &&
                  write(".json", metadata.data(), metadata.size());
  REXLOG_INFO("[gl-pixels] capture complete={} path={}", ok, stem.string());
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
