/**
 * @file configuration.cpp
 * @brief The Configuration page's examples, compiled by the host build (never run).
 */
#include "my_port.hpp"
#include "tmp117.hpp"
#include "tmp117_kconfig.hpp"

// --8<-- [start:custom]
tmp117::Config LowNoiseConfig() {
  tmp117::Config c{}; // datasheet reset values
  c.averaging = tmp117::Averaging::X32;
  c.cycle_time = tmp117::CycleTime::S1; // 32 conversions need at least 1 s: Table 7-7
  c.low_limit_mc = 5000;                // alert below 5 degC
  c.high_limit_mc = 60000;              // alert above 60 degC
  c.offset_mc = -250;                   // a measured board offset, subtracted by the chip
  return c;                             // tmp117::Validate(c) checks it without the chip
}

tmp117::Error StartLowNoise(MyPort& port) {
  static tmp117::TMP117<MyPort> sensor(port, LowNoiseConfig()); // stored, no I/O
  return sensor.EnsureInitialized().error; // InvalidArgument if low is above high
}
// --8<-- [end:custom]

// --8<-- [start:kconfig]
tmp117::Error StartFromMenuconfig(MyPort& port) {
  static tmp117::TMP117<MyPort> sensor(port, tmp117::KconfigDefaults());
  return sensor.EnsureInitialized().error;
}
// --8<-- [end:kconfig]
