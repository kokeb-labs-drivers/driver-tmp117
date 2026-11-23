/**
 * @file ports.cpp
 * @brief The port with every optional hook, as shown on the Porting page. Compiled, never run.
 */
#include <cstddef>
#include <cstdint>

#include "tmp117.hpp"

bool platform_i2c_init() noexcept;
bool platform_i2c_write(uint8_t addr7, const uint8_t* data, std::size_t len) noexcept;
bool platform_i2c_write_read(uint8_t addr7, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                             std::size_t rx_len) noexcept;
void platform_delay_us(uint32_t us) noexcept;
bool platform_gpio_read(int pin, bool& high) noexcept;
void platform_log(const char* line) noexcept;

// --8<-- [start:full-port]
class FullPort : public tmp117::I2cInterface<FullPort> {
public:
  bool EnsureInitialized() noexcept {
    return platform_i2c_init();
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return platform_i2c_write(addr, data, len);
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return platform_i2c_write_read(addr, tx, tx_len, rx, rx_len);
  }

  // Required by every call that waits (reset, conversions).
  void DelayUs(uint32_t us) noexcept {
    platform_delay_us(us);
  }

  // Optional: the ALERT pin level, for ReadAlertPin(). Return false if the pin cannot be read.
  bool ReadAlertPin(bool& level) noexcept {
    return platform_gpio_read(kAlertGpio, level);
  }

  // Optional: one line per lifecycle event or failure. The driver never formats; the port does.
  void Log(tmp117::LogLevel level, const char* message) noexcept {
    if (level > tmp117::LogLevel::Warning) {
      return;
    }
    platform_log(message); // plain text: never use it as a format string
  }

private:
  static constexpr int kAlertGpio = 4;
};
// --8<-- [end:full-port]

// --8<-- [start:alert-pin]
bool AlertIsActive(tmp117::TMP117<FullPort>& sensor) {
  // Needs the ReadAlertPin hook; a port without it gets Error::Unsupported. The pin is active low
  // unless the driver was configured with AlertPolarity::ActiveHigh.
  const auto level = sensor.ReadAlertPin();
  return level && !level.value;
}
// --8<-- [end:alert-pin]
