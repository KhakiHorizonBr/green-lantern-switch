#pragma once
#include <cstdint>
#include <vector>
#include <optional>

// Fixed-size page arena. Retired pages remain unavailable until the caller
// proves completion of the submission that last read them. Adjacent free pages
// naturally coalesce during the first-fit scan; no allocations on Reserve.
namespace green_lantern::native {
class ResidentPages {
 public:
  static constexpr uint32_t kPage = 64u << 10;
  struct Block { uint32_t first, count; };
  void Initialize(uint32_t bytes) { pages_.assign(bytes / kPage, {}); }
  template<class Complete> void Collect(Complete complete) {
    for (auto& p : pages_)
      if (p.state == 2 && complete(p.serial)) p = {};
  }
  template<class Complete> std::optional<Block> Reserve(uint32_t bytes, Complete complete) {
    if (!bytes) return {};
    Collect(complete);
    const uint32_t count = uint32_t((uint64_t(bytes) + kPage - 1) / kPage);
    uint32_t run = 0;
    for (uint32_t i = 0; i < pages_.size(); ++i) {
      run = pages_[i].state ? 0 : run + 1;
      if (run == count) {
        Block b{i + 1 - count, count};
        for (uint32_t n = b.first; n <= i; ++n) pages_[n].state = 1;
        return b;
      }
    }
    return {};
  }
  void Retire(Block b, uint64_t serial) {
    for (uint32_t n = b.first; n < b.first + b.count; ++n) pages_[n] = {serial, 2};
  }
  uint32_t OccupiedBytes() const {
    uint32_t n = 0; for (const auto& p : pages_) n += p.state != 0;
    return n * kPage;
  }
 private:
  struct Page { uint64_t serial = 0; uint8_t state = 0; };
  std::vector<Page> pages_;
};
}
