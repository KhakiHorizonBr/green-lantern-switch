// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace green_lantern::deko {
// deko3d consumes a dense array of views even when some MRT write masks are
// zero. Preserve shader output indices; filling holes must never compact RTs.
template<class View,size_t N>
bool PlanAttachments(std::array<const View*,N>& views,uint32_t count,uint32_t& holes) {
  holes=0;if(count>N)return false;
  const View* fallback=nullptr;
  for(uint32_t i=0;i<count;++i)if(views[i]){fallback=views[i];break;}
  if(count && !fallback)return false;
  for(uint32_t i=0;i<count;++i)if(!views[i]){views[i]=fallback;holes|=1u<<i;}
  return true;
}
}
