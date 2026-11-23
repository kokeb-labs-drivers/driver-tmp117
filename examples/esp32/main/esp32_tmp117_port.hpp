/**
 * @file esp32_tmp117_port.hpp
 * @brief ESP-IDF port for the TMP117 driver: i2c_master for the bus, GPIO for ALERT, esp_rom
 * delays and ESP_LOG for the log hook.
 *
 * Start from this file for any ESP-IDF target. The pins are in Esp32Tmp117Pins; the defaults match
 * the wiring table in examples/esp32/README.md (ESP32-C6 DevKitC-1).
 */
#ifndef TMP117_ESP32_TMP117_PORT_HPP
#define TMP117_ESP32_TMP117_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "tmp117.hpp"

/// The port's default SCL clock, checked against the driver's bus requirements (KB-CPP-123).
inline constexpr uint32_t kEsp32Tmp117ClockHz = 100'000;
static_assert(kEsp32Tmp117ClockHz <= tmp117::kMaxClockHz, "SCL above the TMP117's maximum");
static_assert(tmp117::kRepeatedStart, "this port reads with i2c_master_transmit_receive");

/// Pins and bus settings of the ESP-IDF port.
struct Esp32Tmp117Pins {
  i2c_port_num_t port = I2C_NUM_0;
  int sda = 6;
  int scl = 7;
  int alert = 11;                          ///< GPIO that reads the ALERT pin, -1 when not wired.
  uint32_t clock_hz = kEsp32Tmp117ClockHz; ///< Up to `tmp117::kMaxClockHz`.
  int timeout_ms = 10;                     ///< Per transaction.
};

/// The TMP117 I2C port on ESP-IDF, with every optional hook.
class Esp32Tmp117Port : public tmp117::I2cInterface<Esp32Tmp117Port> {
public:
  explicit Esp32Tmp117Port(const Esp32Tmp117Pins& pins = {}) noexcept : pins_(pins) {}

  bool EnsureInitialized() noexcept {
    if (bus_ != nullptr) {
      return true;
    }
    i2c_master_bus_config_t bus{};
    bus.i2c_port = pins_.port;
    bus.sda_io_num = static_cast<gpio_num_t>(pins_.sda);
    bus.scl_io_num = static_cast<gpio_num_t>(pins_.scl);
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus, &bus_) != ESP_OK) {
      bus_ = nullptr;
      return false;
    }
    if (pins_.alert >= 0) {
      // ALERT is open drain with an external pull-up (Figure 8-1): the GPIO only reads it.
      gpio_config_t io{};
      io.pin_bit_mask = 1ULL << static_cast<unsigned>(pins_.alert);
      io.mode = GPIO_MODE_INPUT;
      if (gpio_config(&io) != ESP_OK) {
        return false;
      }
    }
    return true;
  }

  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return (dev != nullptr) && (i2c_master_transmit(dev, data, len, pins_.timeout_ms) == ESP_OK);
  }

  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return (dev != nullptr) &&
           (i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, pins_.timeout_ms) == ESP_OK);
  }

  void DelayUs(uint32_t us) noexcept {
    esp_rom_delay_us(us);
  }

  bool ReadAlertPin(bool& level) noexcept {
    if (pins_.alert < 0) {
      return false;
    }
    level = gpio_get_level(static_cast<gpio_num_t>(pins_.alert)) != 0;
    return true;
  }

  void Log(tmp117::LogLevel level, const char* message) noexcept {
    const esp_log_level_t esp_level = (level == tmp117::LogLevel::Error)     ? ESP_LOG_ERROR
                                      : (level == tmp117::LogLevel::Warning) ? ESP_LOG_WARN
                                      : (level == tmp117::LogLevel::Info)    ? ESP_LOG_INFO
                                                                             : ESP_LOG_DEBUG;
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

private:
  // The device handle for one 7-bit address, created on first use (one chip per port in this
  // example).
  i2c_master_dev_handle_t device(uint8_t addr) noexcept {
    if ((device_ != nullptr) && (device_addr_ == addr)) {
      return device_;
    }
    if (device_ != nullptr) {
      static_cast<void>(i2c_master_bus_rm_device(device_));
      device_ = nullptr;
    }
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = pins_.clock_hz;
    if (i2c_master_bus_add_device(bus_, &cfg, &device_) != ESP_OK) {
      device_ = nullptr;
      return nullptr;
    }
    device_addr_ = addr;
    return device_;
  }

  static constexpr const char* kTag = "tmp117";
  Esp32Tmp117Pins pins_;
  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  uint8_t device_addr_ = 0;
};

#endif // TMP117_ESP32_TMP117_PORT_HPP
