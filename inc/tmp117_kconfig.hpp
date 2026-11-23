/**
 * @file tmp117_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` built from the `CONFIG_TMP117_*` Kconfig options.
 * @copyright Copyright (c) 2026 The driver-tmp117 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header only where you want menuconfig to choose the defaults; the driver never
 * includes it and no driver signature depends on it. Under ESP-IDF the values come from
 * `sdkconfig.h`. Without Kconfig every option falls back to the datasheet default, so
 * `KconfigDefaults()` equals `Config{}`.
 *
 * The options set the driver-side behaviour and the cycle and averaging; the limits and the offset
 * stay in code, because a wrong alert limit is a safety decision and not a menu choice.
 *
 * One configuration per program: the inline function below depends on the `CONFIG_` macros, so
 * every translation unit of one program must see the same values (ESP-IDF has one `sdkconfig.h`
 * per build; each host test is its own program). No driver or library header includes this one
 * (handbook KB-CPP20-018).
 */
#ifndef TMP117_KCONFIG_HPP
#define TMP117_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include "tmp117_types.hpp"

#ifndef CONFIG_TMP117_RESET_TIMEOUT_MS
/** Longest wait for the power-up load in ms when Kconfig does not set it:
 * `Config{}.reset_timeout_ms`. */
#define CONFIG_TMP117_RESET_TIMEOUT_MS 10
#endif
#ifndef CONFIG_TMP117_EEPROM_TIMEOUT_MS
/** Longest wait for one EEPROM word in ms when Kconfig does not set it:
 * `Config{}.eeprom_timeout_ms`. */
#define CONFIG_TMP117_EEPROM_TIMEOUT_MS 50
#endif

namespace tmp117 {

/**
 * @brief The configuration menuconfig selected, on top of the datasheet defaults.
 * @return `Config{}` with the averaging, the cycle time and the two driver deadlines from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
  Config c{};
#ifdef CONFIG_TMP117_AVERAGING_1
  c.averaging = Averaging::X1;
#elif defined(CONFIG_TMP117_AVERAGING_32)
  c.averaging = Averaging::X32;
#elif defined(CONFIG_TMP117_AVERAGING_64)
  c.averaging = Averaging::X64;
#else
  c.averaging = Averaging::X8;
#endif
#ifdef CONFIG_TMP117_CYCLE_15P5MS
  c.cycle_time = CycleTime::Ms15p5;
#elif defined(CONFIG_TMP117_CYCLE_125MS)
  c.cycle_time = CycleTime::Ms125;
#elif defined(CONFIG_TMP117_CYCLE_250MS)
  c.cycle_time = CycleTime::Ms250;
#elif defined(CONFIG_TMP117_CYCLE_500MS)
  c.cycle_time = CycleTime::Ms500;
#elif defined(CONFIG_TMP117_CYCLE_4S)
  c.cycle_time = CycleTime::S4;
#elif defined(CONFIG_TMP117_CYCLE_8S)
  c.cycle_time = CycleTime::S8;
#elif defined(CONFIG_TMP117_CYCLE_16S)
  c.cycle_time = CycleTime::S16;
#else
  c.cycle_time = CycleTime::S1;
#endif
  c.reset_timeout_ms = CONFIG_TMP117_RESET_TIMEOUT_MS;
  c.eeprom_timeout_ms = CONFIG_TMP117_EEPROM_TIMEOUT_MS;
  return c;
}

} // namespace tmp117

#endif // TMP117_KCONFIG_HPP
