/**
 * @file main.cpp
 * @brief The TMP117 driver on your PC: start a simulated chip, read the temperature, and take one
 * on-demand sample.
 *
 * No hardware and no SDK: the host port (host_tmp117_port.hpp) connects the real driver to the
 * register model the host tests use. The same calls run on an MCU with that MCU's port
 * (examples/esp32/ for ESP-IDF). Exit code 0 when every call succeeded.
 */
#include <cstdint>
#include <cstdio>

#include "host_tmp117_port.hpp"
#include "tmp117.hpp"

int main() {
  HostTmp117Port port;
  port.state.chip.temperature_counts = (23 * 128) + 45; // 23.35 degC in the simulated room
  tmp117::TMP117<HostTmp117Port> sensor(port);          // no I/O yet

  std::printf("TMP117 driver %s on the host\n", tmp117::GetDriverVersion());
  if (const auto r = sensor.EnsureInitialized(); !r) {
    std::printf("EnsureInitialized failed: %s\n", tmp117::ToString(r.error));
    return 1;
  }

  for (int i = 0; i < 3; ++i) {
    port.DelayUs(1'100'000); // one conversion cycle at the defaults (1 s, 8 averaged)
    const auto t = sensor.ReadTemperatureMilliC();
    if (!t) {
      std::printf("read failed: %s\n", tmp117::ToString(t.error));
      return 1;
    }
    const long mc = static_cast<long>(t.value);
    std::printf("%s%ld.%03ld C\n", (mc < 0) ? "-" : "", (mc < 0 ? -mc : mc) / 1000,
                (mc < 0 ? -mc : mc) % 1000);
  }

  const auto once = sensor.ReadOneShotMilliC(); // shutdown between samples
  if (!once) {
    std::printf("one-shot failed: %s\n", tmp117::ToString(once.error));
    return 1;
  }
  std::printf("one-shot %ld mC\n", static_cast<long>(once.value));
  return 0;
}
