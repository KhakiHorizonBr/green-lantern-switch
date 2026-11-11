#pragma once
#if defined(__SWITCH__)
#include <cstdint>
extern "C" void GreenLanternTraceVector(uint64_t base, uint32_t phase,
                                       const uint64_t* values);
template <typename Context, typename Base>
inline void GreenLanternVectorSnapshot(Context& ctx, Base base, uint32_t phase) {
  const uint64_t values[] = {ctx.r1.u32, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
      ctx.r6.u32, ctx.r10.u32, ctx.r11.u32, ctx.r28.u32, ctx.lr};
  GreenLanternTraceVector(reinterpret_cast<uint64_t>(base), phase, values);
}
#else
#define GreenLanternVectorSnapshot(...) ((void)0)
#endif
