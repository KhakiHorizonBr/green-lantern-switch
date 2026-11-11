// Generated from the validated constant-moves.jsonl corpus.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
namespace green_lantern::deko {
inline constexpr uint32_t kSystemSourceBytes=504, kSystemDestinationBytes=528;
inline void PackSystem(const void* source, void* destination) {
  auto* dst=static_cast<uint8_t*>(destination);
  const auto* src=static_cast<const uint8_t*>(source);
  std::memset(dst,0,kSystemDestinationBytes);
  std::memcpy(dst+0,src+0,4);
  std::memcpy(dst+4,src+4,4);
  std::memcpy(dst+8,src+8,4);
  std::memcpy(dst+12,src+12,4);
  std::memcpy(dst+16,src+16,4);
  std::memcpy(dst+20,src+20,4);
  std::memcpy(dst+24,src+24,4);
  std::memcpy(dst+32,src+28,16);
  std::memcpy(dst+48,src+44,16);
  std::memcpy(dst+64,src+60,16);
  std::memcpy(dst+80,src+76,16);
  std::memcpy(dst+96,src+92,16);
  std::memcpy(dst+112,src+108,16);
  std::memcpy(dst+128,src+496,4);
  std::memcpy(dst+132,src+500,4);
  std::memcpy(dst+144,src+124,12);
  std::memcpy(dst+156,src+136,4);
  std::memcpy(dst+160,src+140,12);
  std::memcpy(dst+172,src+152,4);
  std::memcpy(dst+176,src+156,8);
  std::memcpy(dst+184,src+164,8);
  std::memcpy(dst+192,src+172,4);
  std::memcpy(dst+208,src+176,16);
  std::memcpy(dst+224,src+192,16);
  std::memcpy(dst+240,src+208,16);
  std::memcpy(dst+256,src+224,16);
  std::memcpy(dst+272,src+240,16);
  std::memcpy(dst+288,src+256,16);
  std::memcpy(dst+304,src+272,4);
  std::memcpy(dst+308,src+276,4);
  std::memcpy(dst+312,src+280,4);
  std::memcpy(dst+316,src+284,4);
  std::memcpy(dst+320,src+288,16);
  std::memcpy(dst+336,src+304,4);
  std::memcpy(dst+340,src+308,4);
  std::memcpy(dst+344,src+312,4);
  std::memcpy(dst+348,src+316,4);
  std::memcpy(dst+352,src+320,8);
  std::memcpy(dst+360,src+328,8);
  std::memcpy(dst+368,src+336,16);
  std::memcpy(dst+384,src+352,16);
  std::memcpy(dst+400,src+368,16);
  std::memcpy(dst+416,src+384,16);
  std::memcpy(dst+432,src+400,16);
  std::memcpy(dst+448,src+416,16);
  std::memcpy(dst+464,src+432,16);
  std::memcpy(dst+480,src+448,16);
  std::memcpy(dst+496,src+464,16);
  std::memcpy(dst+512,src+480,16);
}
inline uint32_t SystemOffset(uint32_t offset) {
  if(offset >= 0u && offset < 4u) return 0u + offset - 0u;
  if(offset >= 4u && offset < 8u) return 4u + offset - 4u;
  if(offset >= 8u && offset < 12u) return 8u + offset - 8u;
  if(offset >= 12u && offset < 16u) return 12u + offset - 12u;
  if(offset >= 16u && offset < 20u) return 16u + offset - 16u;
  if(offset >= 20u && offset < 24u) return 20u + offset - 20u;
  if(offset >= 24u && offset < 28u) return 24u + offset - 24u;
  if(offset >= 28u && offset < 44u) return 32u + offset - 28u;
  if(offset >= 44u && offset < 60u) return 48u + offset - 44u;
  if(offset >= 60u && offset < 76u) return 64u + offset - 60u;
  if(offset >= 76u && offset < 92u) return 80u + offset - 76u;
  if(offset >= 92u && offset < 108u) return 96u + offset - 92u;
  if(offset >= 108u && offset < 124u) return 112u + offset - 108u;
  if(offset >= 496u && offset < 500u) return 128u + offset - 496u;
  if(offset >= 500u && offset < 504u) return 132u + offset - 500u;
  if(offset >= 124u && offset < 136u) return 144u + offset - 124u;
  if(offset >= 136u && offset < 140u) return 156u + offset - 136u;
  if(offset >= 140u && offset < 152u) return 160u + offset - 140u;
  if(offset >= 152u && offset < 156u) return 172u + offset - 152u;
  if(offset >= 156u && offset < 164u) return 176u + offset - 156u;
  if(offset >= 164u && offset < 172u) return 184u + offset - 164u;
  if(offset >= 172u && offset < 176u) return 192u + offset - 172u;
  if(offset >= 176u && offset < 192u) return 208u + offset - 176u;
  if(offset >= 192u && offset < 208u) return 224u + offset - 192u;
  if(offset >= 208u && offset < 224u) return 240u + offset - 208u;
  if(offset >= 224u && offset < 240u) return 256u + offset - 224u;
  if(offset >= 240u && offset < 256u) return 272u + offset - 240u;
  if(offset >= 256u && offset < 272u) return 288u + offset - 256u;
  if(offset >= 272u && offset < 276u) return 304u + offset - 272u;
  if(offset >= 276u && offset < 280u) return 308u + offset - 276u;
  if(offset >= 280u && offset < 284u) return 312u + offset - 280u;
  if(offset >= 284u && offset < 288u) return 316u + offset - 284u;
  if(offset >= 288u && offset < 304u) return 320u + offset - 288u;
  if(offset >= 304u && offset < 308u) return 336u + offset - 304u;
  if(offset >= 308u && offset < 312u) return 340u + offset - 308u;
  if(offset >= 312u && offset < 316u) return 344u + offset - 312u;
  if(offset >= 316u && offset < 320u) return 348u + offset - 316u;
  if(offset >= 320u && offset < 328u) return 352u + offset - 320u;
  if(offset >= 328u && offset < 336u) return 360u + offset - 328u;
  if(offset >= 336u && offset < 352u) return 368u + offset - 336u;
  if(offset >= 352u && offset < 368u) return 384u + offset - 352u;
  if(offset >= 368u && offset < 384u) return 400u + offset - 368u;
  if(offset >= 384u && offset < 400u) return 416u + offset - 384u;
  if(offset >= 400u && offset < 416u) return 432u + offset - 400u;
  if(offset >= 416u && offset < 432u) return 448u + offset - 416u;
  if(offset >= 432u && offset < 448u) return 464u + offset - 432u;
  if(offset >= 448u && offset < 464u) return 480u + offset - 448u;
  if(offset >= 464u && offset < 480u) return 496u + offset - 464u;
  if(offset >= 480u && offset < 496u) return 512u + offset - 480u;
  std::abort(); // No unproven offset may reach a shader.
}
}
