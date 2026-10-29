#include "native/resident_pages.h"
#include <cassert>
#include <random>
#include <vector>
using green_lantern::native::ResidentPages;
int main() {
  ResidentPages pool; constexpr auto page = ResidentPages::kPage;
  pool.Initialize(8 * page);
  auto never = [](uint64_t){return false;};
  assert(!pool.Reserve(0, never)); assert(!pool.Reserve(9*page, never));
  auto a = *pool.Reserve(3*page, never), b = *pool.Reserve(5*page, never);
  pool.Retire(a, 7); pool.Retire(b, 9);
  assert(!pool.Reserve(page, [](uint64_t s){return s <= 6;}));
  assert(!pool.Reserve(4*page, [](uint64_t s){return s <= 7;}));
  assert(pool.Reserve(8*page, [](uint64_t s){return s <= 9;}));
  pool.Initialize(64*page);
  struct Live { ResidentPages::Block b; uint64_t retire; };
  std::vector<Live> live;
  std::mt19937 rng(2391);
  for (uint64_t step=1; step<=20000; ++step) {
    const uint64_t completed = step > 12 ? step - 12 : 0;
    for (auto i=live.begin(); i!=live.end();) {
      if (i->retire && i->retire <= completed) i=live.erase(i); else ++i;
    }
    if (!live.empty() && rng()%2) {
      auto& x=live[rng()%live.size()];
      if (!x.retire) { x.retire=step; pool.Retire(x.b, step); }
    }
    auto next=pool.Reserve((1+rng()%8)*page-1, [completed](uint64_t s){return s<=completed;});
    if (next) {
      assert(next->first+next->count<=64);
      for (const auto& x:live)
        assert(next->first+next->count<=x.b.first || x.b.first+x.b.count<=next->first);
      live.push_back({*next,0});
    }
    assert(pool.OccupiedBytes()<=64*page);
  }
}
