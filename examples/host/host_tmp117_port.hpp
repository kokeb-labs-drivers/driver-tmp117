/**
 * @file host_tmp117_port.hpp
 * @brief The host port: the driver's bus hooks over a simulated TMP117, so the example runs on any
 * PC.
 *
 * `tmp117_test::MockPortBase` (tests/mock_tmp117_bus.hpp) supplies the required bus hooks over
 * the register model the host tests use. This class adds what a real port adds: `DelayUs()` (here
 * it advances the simulated chip's clock instead of waiting), `ReadAlertPin()` and `Log()` to the
 * console. A port for
 * a real bus on a PC (Linux spidev, a USB bridge) implements the same hooks: see docs/porting.md.
 */
#ifndef TMP117_HOST_TMP117_PORT_HPP
#define TMP117_HOST_TMP117_PORT_HPP

#include <cstdint>
#include <cstdio>

#include "mock_tmp117.hpp"

/// The host port: simulated chip, real console.
class HostTmp117Port : public tmp117_test::MockPortBase<HostTmp117Port> {
public:
  /// The bus settings a real port would use; checked against the driver's bus requirements
  /// (handbook KB-CPP-123) like any port's.
  static constexpr uint32_t kClockHz = 100'000;
  static_assert(kClockHz <= tmp117::kMaxClockHz, "SCL above the TMP117's maximum");

  /// Time passes in the simulated chip; nothing sleeps.
  void DelayUs(uint32_t us) noexcept {
    state.chip.Elapse(us);
  }

  /// The simulated ALERT pin: a real port reads a GPIO here.
  bool ReadAlertPin(bool& level) noexcept {
    level = state.chip.AlertPinHigh();
    return true;
  }

  /// Driver messages (they carry their own `tmp117:` prefix) go to the console, one line each.
  void Log(tmp117::LogLevel level, const char* message) noexcept {
    (void)level;
    (void)std::puts(message); // plain text: never pass it as a format string
  }
};

#endif // TMP117_HOST_TMP117_PORT_HPP
