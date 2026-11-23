/**
 * @file test_support.hpp
 * @brief Small helpers shared by the host tests: a driver on a mock port, and frame checks.
 */
#ifndef TMP117_TEST_SUPPORT_HPP
#define TMP117_TEST_SUPPORT_HPP

#include <cstdint>

#include "mock_tmp117.hpp"
#include "test_harness.hpp"
#include "tmp117.hpp"

namespace tmp117_test {

/// A driver on a mock port, built in place (a port is not copyable).
template <typename Port>
struct Rig {
  Port port;
  tmp117::TMP117<Port> sensor;

  explicit Rig(const tmp117::Config& config = {}) : port(), sensor(port, config) {}

  /// `EnsureInitialized()`, then `ms` milliseconds so the first conversion has finished.
  bool Start(uint32_t ms = 200) {
    const tmp117::Result<void> r = sensor.EnsureInitialized();
    if (!r.ok()) {
      return false;
    }
    port.DelayUs(ms * 1000U);
    return true;
  }

  /// Time passes with no driver call.
  void Wait(uint32_t ms) {
    port.DelayUs(ms * 1000U);
  }

  [[nodiscard]] int Transfers() const {
    return port.state.transfers;
  }
  [[nodiscard]] int Writes(uint8_t pointer) const {
    return port.state.WritesTo(pointer);
  }
};

/// 1/128 degC counts of a temperature in whole degrees.
constexpr int32_t Counts(int32_t degrees) {
  return degrees * 128;
}

} // namespace tmp117_test

#endif // TMP117_TEST_SUPPORT_HPP
