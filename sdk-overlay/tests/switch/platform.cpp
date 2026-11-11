#include <rex/platform.h>
#include <switch.h>
#include <cstdio>
static_assert(REX_PLATFORM_SWITCH && REX_ARCH_ARM64);
static_assert(!REX_PLATFORM_LINUX && !REX_PLATFORM_MAC && !REX_PLATFORM_WIN32);
static_assert(sizeof(void*) == 8 && std::endian::native == std::endian::little);
static_assert(std::byteswap(uint32_t{0x12345678}) == 0x78563412);
int main() {
  consoleInit(nullptr);
  std::printf("ReXGlue platform header: C++23 / Horizon / ARM64 / LE64\n");
  for (unsigned frame = 0; frame < 300 && appletMainLoop(); ++frame) consoleUpdate(nullptr);
  consoleExit(nullptr);
}
