#include <switch.h>

#include <rex/chrono/clock.h>

#include <ctime>

namespace rex::chrono {

uint64_t Clock::host_tick_frequency_platform() {
  return armGetSystemTickFreq();
}

uint64_t Clock::host_tick_count_platform() {
  return armGetSystemTick();
}

uint64_t Clock::QueryHostSystemTime() {
  timespec now{};
  if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
    return 0;
  }

  // ReXGlue exposes host system time as Windows FILETIME (100 ns since 1601).
  constexpr uint64_t kUnixToFileTimeSeconds = 11644473600ull;
  return (static_cast<uint64_t>(now.tv_sec) + kUnixToFileTimeSeconds) * 10000000ull +
         static_cast<uint64_t>(now.tv_nsec / 100);
}

uint64_t Clock::QueryHostUptimeMillis() {
  const uint64_t frequency = host_tick_frequency_platform();
  return frequency ? host_tick_count_platform() * 1000ull / frequency : 0;
}

}  // namespace rex::chrono
