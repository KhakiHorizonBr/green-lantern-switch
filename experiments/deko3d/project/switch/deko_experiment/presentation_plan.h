// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
namespace green_lantern::deko {
struct PresentationPlan {float viewport_width=0,viewport_height=0;uint32_t width=0,height=0;};
// The title's display mode and our swapchain are both 16:9. The internal
// resolve may be padded or lower-resolution; sample only its visible extent
// and scale that to the display. Extending the viewport while clipping to the
// display adjusts UV coverage without changing the compiled output shader.
inline bool PlanPresentation(uint32_t source_width,uint32_t source_height,
    uint32_t visible_width,uint32_t visible_height,uint32_t display_width,uint32_t display_height,
    PresentationPlan& p) {
  if(!source_width || !source_height || !display_width || !display_height)return false;
  visible_width=std::min(visible_width?visible_width:source_width,source_width);
  visible_height=std::min(visible_height?visible_height:source_height,source_height);
  p.viewport_width=float(double(display_width)*source_width/visible_width);
  p.viewport_height=float(double(display_height)*source_height/visible_height);
  if(p.viewport_width>16384 || p.viewport_height>16384)return false;
  p.width=display_width;p.height=display_height;return true;
}
}
