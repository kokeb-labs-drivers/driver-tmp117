/**
 * @file alert_pin_returns_void.cpp
 * @brief Must NOT compile: an optional hook with the wrong signature is rejected, not ignored.
 *
 * EXPECT: ReadAlertPin hook must be
 */
#include "mock_tmp117.hpp"
#include "tmp117.hpp"

class VoidPinPort : public tmp117_test::MockPortBase<VoidPinPort> {
public:
  void DelayUs(uint32_t us) noexcept {
    (void)us;
  }
  void ReadAlertPin(bool& level) noexcept {
    level = false;
  }
};

int main() {
  VoidPinPort port;
  tmp117::TMP117<VoidPinPort> sensor(port);
  return static_cast<int>(sensor.EnsureInitialized().error);
}
