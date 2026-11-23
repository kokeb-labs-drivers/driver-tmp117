/**
 * @file api_tasks.cpp
 * @brief The API guide's examples, compiled by the host build (never run).
 */
#include <cstdio>

#include "my_port.hpp"
#include "tmp117.hpp"

using Sensor = tmp117::TMP117<MyPort>;

// --8<-- [start:lifecycle]
tmp117::Error BringUp(Sensor& sensor) {
  // The one entry point: waits for the power-up load, checks Device_ID, writes the configuration
  // and reads it back. After a failure the next call starts again from the beginning.
  const auto r = sensor.EnsureInitialized();
  if (!r) {
    std::printf("TMP117 init: %s\n", tmp117::ToString(r.error));
  }
  return r.error;
}
// --8<-- [end:lifecycle]

// --8<-- [start:one-shot]
tmp117::Result<int32_t> SampleOnDemand(Sensor& sensor) {
  // Shutdown between samples: the chip draws about 0.15 uA while it waits.
  return sensor.ReadOneShotMilliC(); // start, wait for Data_Ready, read, back to shutdown
}
// --8<-- [end:one-shot]

// --8<-- [start:alerts]
tmp117::Error ArmOverTemperature(Sensor& sensor) {
  // Window mode: ALERT is active (low) outside -20 degC .. 85 degC. A low above a high is refused
  // before any frame is sent.
  if (auto r = sensor.SetAlertFunction(tmp117::AlertFunction::Window); !r) {
    return r.error;
  }
  return sensor.SetLimits(-20000, 85000).error;
}
// --8<-- [end:alerts]

// --8<-- [start:status]
bool OverTemperatureSeen(Sensor& sensor) {
  // The flags clear on every read of the Configuration register, so the driver latches them: this
  // returns what was seen since the last ClearLatchedStatus().
  const auto s = sensor.ReadStatus();
  return s && (s.value.high_alert || sensor.GetLatchedStatus().high_alert);
}
// --8<-- [end:status]

// --8<-- [start:errors]
const char* Describe(tmp117::Error e) {
  switch (e) {
  case tmp117::Error::Bus:
  case tmp117::Error::DeviceIdMismatch:
    return "check the bus wiring and the address pin";
  case tmp117::Error::UnexpectedReset:
    return "the chip restarted: call EnsureInitialized() again, check the supply";
  case tmp117::Error::NoMeasurement:
    return "no result yet: wait one conversion cycle";
  default:
    return tmp117::ToString(e);
  }
}
// --8<-- [end:errors]

// --8<-- [start:deinit]
void PowerDown(Sensor& sensor) {
  // Always sends shutdown, even if the driver believes the chip is already there.
  if (auto r = sensor.Deinitialize(); !r) {
    std::printf("TMP117 shutdown write failed: %s\n", tmp117::ToString(r.error));
  }
}
// --8<-- [end:deinit]
