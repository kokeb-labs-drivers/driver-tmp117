/**
 * @file quickstart.cpp
 * @brief The Quickstart page's example, compiled by the host build (never run).
 */
#include "my_port.hpp"

// --8<-- [start:quickstart]
#include "tmp117.hpp"

int read_board_temperature(MyPort& port, int32_t& milli_celsius) {
  tmp117::TMP117<MyPort> sensor(port); // address 0x48 (ADD0 to GND), no I/O yet

  if (auto r = sensor.EnsureInitialized(); !r) { // 1. wait for the power-up load, Device_ID, config
    return static_cast<int>(r.error);            //    Bus, Timeout or DeviceIdMismatch: wiring
  }
  const auto t = sensor.ReadTemperatureMilliC(); // 2. the latest continuous result
  if (!t) {
    return static_cast<int>(t.error); //    NoMeasurement: wait one cycle and ask again
  }
  milli_celsius = t.value; // 3. thousandths of a degree Celsius, 7.8125 mC per count
  return 0;
}
// --8<-- [end:quickstart]
