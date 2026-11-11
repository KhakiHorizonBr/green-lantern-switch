// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
namespace green_lantern::deko {
inline uint32_t TargetHeight(uint32_t current,uint32_t required) {
  required=(std::clamp(required,16u,2048u)+15u)&~15u;
  return std::max(current,required);
}
template<class Compatible>
uint32_t TileSpan(uint32_t tile,uint32_t remaining,uint32_t source_base,
                  uint32_t source_pitch,Compatible compatible) {
  if(tile>=2048 || !remaining || (source_pitch && tile<source_base))return 0;
  uint32_t limit=std::min(remaining,2048-tile);
  if(source_pitch)limit=std::min(limit,source_pitch-(tile-source_base)%source_pitch);
  uint32_t n=1;while(n<limit && compatible(tile+n))++n;
  return n;
}
}
