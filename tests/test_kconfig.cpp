/**
 * @file test_kconfig.cpp
 * @brief KconfigDefaults(): the defaults without Kconfig, and menuconfig values when they are set.
 *
 * Built twice: without CONFIG_ macros (KCONFIG_SET undefined) and with the values an sdkconfig.h
 * would define (KCONFIG_SET=1, see tests/CMakeLists.txt).
 */
#include "test_harness.hpp"
#include "tmp117_kconfig.hpp"

#ifdef KCONFIG_SET
TEST_CASE("Kconfig values reach the Config") {
  constexpr tmp117::Config kC = tmp117::KconfigDefaults();
  CHECK_EQ(kC.averaging, tmp117::Averaging::X32);
  CHECK_EQ(kC.cycle_time, tmp117::CycleTime::S4);
  CHECK_EQ(kC.reset_timeout_ms, 20U);
  CHECK_EQ(kC.eeprom_timeout_ms, 80U);
  CHECK_EQ(kC.mode, tmp117::Mode::Continuous); // not a Kconfig option: datasheet default
  CHECK_EQ(tmp117::Validate(kC), tmp117::Error::Ok);
}
#else
TEST_CASE("Without Kconfig, KconfigDefaults() equals Config{}") {
  constexpr tmp117::Config kC = tmp117::KconfigDefaults();
  constexpr tmp117::Config kD{};
  CHECK_EQ(kC.averaging, kD.averaging);
  CHECK_EQ(kC.cycle_time, kD.cycle_time);
  CHECK_EQ(kC.reset_timeout_ms, kD.reset_timeout_ms);
  CHECK_EQ(kC.eeprom_timeout_ms, kD.eeprom_timeout_ms);
}
#endif
