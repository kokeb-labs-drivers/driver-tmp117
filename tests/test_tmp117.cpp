/**
 * @file test_tmp117.cpp
 * @brief Driver behaviour over the register model: lifecycle, identity, configuration, measurement,
 * alerts, the latch, reset evidence and the port hooks.
 */
#include <cstdint>

#include "mock_tmp117.hpp"
#include "test_harness.hpp"
#include "test_support.hpp"
#include "tmp117.hpp"

using tmp117::Error;
using tmp117_test::Counts;
using tmp117_test::FullPort;
using tmp117_test::MinimalPort;
using tmp117_test::Rig;
using tmp117_test::Stuck;
namespace reg = tmp117::reg;
namespace chip = tmp117_test::chip;

// --- Lifecycle
// --------------------------------------------------------------------------------------------------

TEST_CASE("Every buffer handed to the port lies inside the driver object (RD-06)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  CHECK(rig.sensor.SetAveraging(tmp117::Averaging::X8).ok());
  CHECK(rig.sensor.ReadTemperatureMilliC().ok());
  CHECK(rig.sensor.ReadStatus().ok());
  CHECK(rig.sensor.ReadOneShotMilliC().ok());
  const auto first = tmp117_test::AddressOf(&rig.sensor);
  CHECK(rig.port.state.buffer_hi != 0U); // buffers were seen
  CHECK(rig.port.state.buffer_lo >= first);
  CHECK(rig.port.state.buffer_hi <= (first + sizeof(rig.sensor)));
}

TEST_CASE("The constructor does no I/O") {
  Rig<MinimalPort> rig;
  CHECK(!rig.sensor.IsInitialized());
  CHECK_EQ(rig.port.state.transfers, 0);
  CHECK_EQ(rig.port.state.bus_up_calls, 0);
}

TEST_CASE("Every I/O call returns NotInitialized before EnsureInitialized, and does no I/O") {
  Rig<FullPort> rig;
  auto& s = rig.sensor;
  CHECK_EQ(s.ReadStatus().error, Error::NotInitialized);
  CHECK_EQ(s.ReadTemperatureRaw().error, Error::NotInitialized);
  CHECK_EQ(s.ReadTemperatureMilliC().error, Error::NotInitialized);
  CHECK_EQ(s.StartOneShot().error, Error::NotInitialized);
  CHECK_EQ(s.ReadOneShotMilliC().error, Error::NotInitialized);
  CHECK_EQ(s.WaitDataReady(10).error, Error::NotInitialized);
  CHECK_EQ(s.SetMode(tmp117::Mode::Shutdown).error, Error::NotInitialized);
  CHECK_EQ(s.SetAveraging(tmp117::Averaging::X1).error, Error::NotInitialized);
  CHECK_EQ(s.SetCycleTime(tmp117::CycleTime::S1).error, Error::NotInitialized);
  CHECK_EQ(s.SetAlertFunction(tmp117::AlertFunction::Window).error, Error::NotInitialized);
  CHECK_EQ(s.SetAlertPolarity(tmp117::AlertPolarity::ActiveLow).error, Error::NotInitialized);
  CHECK_EQ(s.SetAlertSource(tmp117::AlertSource::Flags).error, Error::NotInitialized);
  CHECK_EQ(s.SetLimits(0, 1000).error, Error::NotInitialized);
  CHECK_EQ(s.SetOffsetMilliC(0).error, Error::NotInitialized);
  CHECK_EQ(s.Configure({}).error, Error::NotInitialized);
  CHECK_EQ(s.ReadAlertPin().error, Error::NotInitialized);
  CHECK_EQ(s.ReadUniqueId().error, Error::NotInitialized);
  CHECK_EQ(s.CheckHealth().error, Error::NotInitialized);
  CHECK_EQ(s.ReadRegister(reg::kDeviceId).error, Error::NotInitialized);
  CHECK_EQ(s.SoftReset().error, Error::NotInitialized);
  CHECK_EQ(s.GeneralCallReset().error, Error::NotInitialized);
  CHECK_EQ(s.ProgramPowerOnDefaults({}).error, Error::NotInitialized);
  CHECK_EQ(s.ProgramUserWord(1).error, Error::NotInitialized);
  CHECK_EQ(rig.port.state.transfers, 0);
  CHECK_EQ(rig.port.state.bus_up_calls, 0); // no call starts the chip as a side effect
  CHECK(!s.IsInitialized());
}

TEST_CASE("Start-up waits for the power-up load, then writes limits, offset and configuration") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.sensor.EnsureInitialized().ok());
  CHECK(rig.sensor.IsInitialized());
  CHECK_EQ(rig.port.state.bus_up_calls, 1);
  CHECK_EQ(rig.port.state.chip.writes_during_load, 0); // nothing was sent while the chip loads
  CHECK(rig.port.delayed_us >= tmp117::kPowerOnLoadUs);
  // Golden frames (Figure 7-9, p. 22): pointer, high byte, low byte, in the order of start-up.
  const auto& f = rig.port.state.frames;
  std::size_t first_write = 0;
  while (f[first_write].is_read) {
    ++first_write;
  }
  CHECK_EQ(f[first_write].bytes[0], reg::kHighLimit);
  CHECK_EQ(f[first_write].bytes[1], 0x60);
  CHECK_EQ(f[first_write].bytes[2], 0x00);
  CHECK_EQ(f[first_write + 1].bytes[0], reg::kLowLimit);
  CHECK_EQ(f[first_write + 1].bytes[1], 0x80);
  CHECK_EQ(f[first_write + 2].bytes[0], reg::kTempOffset);
  CHECK_EQ(f[first_write + 3].bytes[0], reg::kConfiguration);
  CHECK_EQ(f[first_write + 3].bytes[1], 0x02);
  CHECK_EQ(f[first_write + 3].bytes[2], 0x20);
  CHECK_EQ(f[first_write].address, tmp117::kI2cAddress);
}

TEST_CASE("A read is a pointer write with a repeated START and two bytes (Figure 7-10)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.sensor.EnsureInitialized().ok());
  bool saw_device_id_read = false;
  for (std::size_t i = 0; i < rig.port.state.frame_count; ++i) {
    const auto& f = rig.port.state.frames[i];
    if (f.is_read && f.bytes[0] == reg::kDeviceId) {
      saw_device_id_read = true;
      CHECK_EQ(f.reply, chip::kDeviceIdValue);
      CHECK_EQ(f.length, 1);
    }
  }
  CHECK(saw_device_id_read);
}

TEST_CASE("EnsureInitialized starts the chip once, then returns Ok with no I/O") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.sensor.EnsureInitialized().ok());
  const int transfers = rig.port.state.transfers;
  for (int i = 0; i < 3; ++i) {
    CHECK(rig.sensor.EnsureInitialized().ok());
  }
  CHECK_EQ(rig.port.state.transfers, transfers);
  CHECK_EQ(rig.port.state.bus_up_calls, 1);
}

TEST_CASE("A slow power-up load is waited for, and no write is sent during it") {
  for (const uint32_t load_us : {500U, 1500U, 4000U, 9000U}) {
    Rig<MinimalPort> rig;
    rig.port.state.chip.por_load_us = load_us;
    rig.port.state.chip.PowerCycle();
    CHECK(rig.sensor.EnsureInitialized().ok());
    CHECK_EQ(rig.port.state.chip.writes_during_load, 0);
  }
}

TEST_CASE("A power-up load that never ends is a Timeout and nothing is written") {
  Rig<MinimalPort> rig;
  rig.port.state.chip.por_load_us = 500000; // 500 ms against the 10 ms default deadline
  rig.port.state.chip.PowerCycle();
  CHECK_EQ(rig.sensor.EnsureInitialized().error, Error::Timeout);
  CHECK_EQ(rig.port.state.chip.writes, 0);
  CHECK(!rig.sensor.IsInitialized());
}

TEST_CASE("A longer deadline waits for a slow load (ambiguity A1)") {
  tmp117::Config c{};
  c.reset_timeout_ms = 100;
  Rig<MinimalPort> rig(c);
  rig.port.state.chip.por_load_us = 50000;
  rig.port.state.chip.PowerCycle();
  CHECK(rig.sensor.EnsureInitialized().ok());
  CHECK_EQ(rig.port.state.chip.writes_during_load, 0);
}

TEST_CASE("Device_ID: 0117h passes, any Rev passes, anything else is DeviceIdMismatch") {
  {
    Rig<MinimalPort> rig;
    rig.port.state.chip.device_rev = 3;
    CHECK(rig.sensor.EnsureInitialized().ok());
    CHECK_EQ(rig.sensor.GetRevision(), 3);
  }
  for (const uint16_t id :
       {uint16_t{0x0116}, uint16_t{0x0118}, uint16_t{0x0000}, uint16_t{0x0FFF}}) {
    Rig<MinimalPort> rig;
    rig.port.state.chip.device_id = id;
    CHECK_EQ(rig.sensor.EnsureInitialized().error, Error::DeviceIdMismatch);
    CHECK_EQ(rig.port.state.chip.writes, 0); // no write over a chip that is not ours
    CHECK(!rig.sensor.IsInitialized());
  }
}

TEST_CASE("A stuck line cannot start the driver") {
  {
    Rig<MinimalPort> rig;
    rig.port.state.stuck = tmp117_test::Stuck::Zeros;
    const Error e = rig.sensor.EnsureInitialized().error;
    CHECK_EQ(e, Error::DeviceIdMismatch);
    CHECK_EQ(rig.port.state.chip.writes, 0);
  }
  {
    Rig<MinimalPort> rig;
    rig.port.state.stuck = tmp117_test::Stuck::Ones;
    const Error e = rig.sensor.EnsureInitialized().error;
    CHECK_EQ(e, Error::BadReply); // bits 1:0 of Configuration always read 0
    CHECK_EQ(rig.port.state.chip.writes, 0);
  }
}

TEST_CASE("After a failed start, EnsureInitialized starts again from the beginning") {
  Rig<MinimalPort> rig;
  rig.port.state.chip.device_id = 0x0000;
  CHECK_EQ(rig.sensor.EnsureInitialized().error, Error::DeviceIdMismatch);
  CHECK_EQ(rig.sensor.ReadTemperatureMilliC().error, Error::NotInitialized);
  rig.port.state.chip.device_id = reg::kDeviceIdValue;
  CHECK(rig.sensor.EnsureInitialized().ok());
}

TEST_CASE("A bus that cannot start is Bus, an invalid Config or address is InvalidArgument") {
  {
    Rig<MinimalPort> rig;
    rig.port.state.bus_up_ok = false;
    CHECK_EQ(rig.sensor.EnsureInitialized().error, Error::Bus);
    CHECK_EQ(rig.port.state.transfers, 0);
  }
  {
    tmp117::Config c{};
    c.low_limit_mc = 5000;
    c.high_limit_mc = 1000; // inverted: refused (ambiguity A10)
    Rig<MinimalPort> rig(c);
    CHECK_EQ(rig.sensor.EnsureInitialized().error, Error::InvalidArgument);
    CHECK_EQ(rig.port.state.transfers, 0); // nothing sent
    CHECK_EQ(rig.port.state.bus_up_calls, 0);
  }
  {
    MinimalPort port;
    tmp117::TMP117<MinimalPort> sensor(port, uint8_t{0x4C}, tmp117::Config{});
    CHECK_EQ(sensor.EnsureInitialized().error, Error::InvalidArgument);
    CHECK_EQ(port.state.transfers, 0);
  }
}

TEST_CASE("The four addresses of Table 7-2 each start a chip that answers there") {
  for (const uint8_t address :
       {tmp117::kAddressGnd, tmp117::kAddressVplus, tmp117::kAddressSda, tmp117::kAddressScl}) {
    MinimalPort port;
    port.state.address = address;
    tmp117::TMP117<MinimalPort> sensor(port, address, tmp117::Config{});
    CHECK(sensor.EnsureInitialized().ok());
  }
  MinimalPort port;
  port.state.address = tmp117::kAddressVplus;
  tmp117::TMP117<MinimalPort> wrong(port, tmp117::kAddressGnd, tmp117::Config{});
  CHECK_EQ(wrong.EnsureInitialized().error, Error::Bus); // nobody answers at 0x48
}

TEST_CASE("A start-up Config reaches the chip and reads back") {
  tmp117::Config c{};
  c.mode = tmp117::Mode::Shutdown;
  c.averaging = tmp117::Averaging::X32;
  c.cycle_time = tmp117::CycleTime::S4;
  c.alert_function = tmp117::AlertFunction::Threshold;
  c.alert_polarity = tmp117::AlertPolarity::ActiveHigh;
  c.alert_source = tmp117::AlertSource::DataReady;
  c.low_limit_mc = -10000;
  c.high_limit_mc = 60000;
  c.offset_mc = 1500;
  Rig<MinimalPort> rig(c);
  REQUIRE(rig.sensor.EnsureInitialized().ok());
  const auto& m = rig.port.state.chip;
  // MOD 01, CONV 101, AVG 10, T/nA, POL, DR/Alert.
  CHECK_EQ(m.RawConfig(), 0x0400U | 0x0280U | 0x0040U | 0x0010U | 0x0008U | 0x0004U);
  CHECK_EQ(m.RawHigh(), 60000 * 128 / 1000);
  CHECK_EQ(tmp117::detail::AsSigned16(m.RawLow()), -1280);
  CHECK_EQ(m.RawOffset(), 192);
}

// --- Measurement
// --------------------------------------------------------------------------------------------------

TEST_CASE("Before the first conversion the result is NoMeasurement, never -256 degC") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.sensor.EnsureInitialized().ok());
  const Error raw = rig.sensor.ReadTemperatureRaw().error;
  CHECK_EQ(raw, Error::NoMeasurement);
  const Error milli = rig.sensor.ReadTemperatureMilliC().error;
  CHECK_EQ(milli, Error::NoMeasurement);
  CHECK(rig.sensor.IsInitialized()); // no reset was seen
}

TEST_CASE("A temperature is read in counts and in millidegrees, rounded half away from zero") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const auto raw = rig.sensor.ReadTemperatureRaw();
  REQUIRE(raw.ok());
  CHECK_EQ(raw.value, Counts(25));
  const auto milli = rig.sensor.ReadTemperatureMilliC();
  CHECK_EQ(milli.value, 25000);
  rig.port.state.chip.temperature_counts = -1; // -0.0078125 degC
  rig.Wait(1100);
  const auto neg = rig.sensor.ReadTemperatureMilliC();
  CHECK_EQ(neg.value, -8);
  rig.port.state.chip.temperature_counts = 32767;
  rig.Wait(1100);
  const auto top = rig.sensor.ReadTemperatureMilliC();
  CHECK_EQ(top.value, 255992);
}

TEST_CASE("A conversion that stalls leaves the last value, not a fault") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.chip.conversion_stuck = true;
  rig.Wait(3000);
  const auto t = rig.sensor.ReadTemperatureMilliC();
  CHECK_EQ(t.value, 25000);
}

TEST_CASE("Verify::Device reads Device_ID first: a line stuck at 0 or 1 cannot fake a reading") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.stuck = tmp117_test::Stuck::Zeros;
  const Error zeros = rig.sensor.ReadTemperatureMilliC(tmp117::Verify::Device).error;
  CHECK_EQ(zeros, Error::DeviceIdMismatch);
  rig.port.state.stuck = tmp117_test::Stuck::Ones;
  const Error ones = rig.sensor.ReadTemperatureRaw(tmp117::Verify::Device).error;
  CHECK_EQ(ones, Error::DeviceIdMismatch);
  rig.port.state.stuck = tmp117_test::Stuck::None;
  const int before = rig.port.state.transfers;
  const auto ok = rig.sensor.ReadTemperatureMilliC(tmp117::Verify::Device);
  CHECK(ok.ok());
  CHECK_EQ(rig.port.state.transfers - before, 2); // the extra transfer is the whole cost
  const int before_plain = rig.port.state.transfers;
  const auto plain = rig.sensor.ReadTemperatureMilliC();
  CHECK(plain.ok());
  CHECK_EQ(rig.port.state.transfers - before_plain, 1);
  // The underlying type is fixed, so 9 is a valid value of `Verify` that names no enumerator.
  const auto bad = rig.sensor.ReadTemperatureRaw(tmp117_test::RawEnum<tmp117::Verify>(9));
  CHECK_EQ(bad.error, Error::InvalidArgument);
}

TEST_CASE(
    "A default read of 0000h or FFFFh is confirmed by Device_ID, other words cost one transfer") {
  for (const Stuck pattern : {Stuck::Zeros, Stuck::Ones}) {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.port.state.stuck = pattern;
    CHECK_EQ(rig.sensor.ReadTemperatureMilliC().error, Error::DeviceIdMismatch);
    CHECK_EQ(rig.sensor.ReadTemperatureRaw().error, Error::DeviceIdMismatch);
  }
  for (const int32_t counts : {0, -1}) { // a working chip at 0 degC and at -1 count
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.port.state.chip.temperature_counts = counts;
    rig.Wait(3000);
    const int before = rig.port.state.transfers;
    const auto t = rig.sensor.ReadTemperatureRaw();
    CHECK(t.ok());
    CHECK_EQ(static_cast<int32_t>(t.value), counts);
    CHECK_EQ(rig.port.state.transfers - before, 2); // Temp_Result, then Device_ID
  }
}

TEST_CASE("Shutdown stops conversions and Deinitialize always sends the shutdown word") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  const int conversions = rig.port.state.chip.conversions;
  rig.Wait(5000);
  CHECK_EQ(rig.port.state.chip.conversions, conversions);
  CHECK_EQ(rig.port.state.chip.RawConfig() & chip::kModMask, 0x0400);
  CHECK(rig.sensor.SetMode(tmp117::Mode::Continuous).ok());
  rig.Wait(200);
  CHECK(rig.port.state.chip.conversions > conversions);
  const int writes = rig.Writes(reg::kConfiguration);
  CHECK(rig.sensor.Deinitialize().ok());
  CHECK_EQ(rig.Writes(reg::kConfiguration), writes + 1);
  CHECK_EQ(rig.port.state.chip.RawConfig() & chip::kModMask, 0x0400);
  CHECK(!rig.sensor.IsInitialized());
}

TEST_CASE(
    "Deinitialize on a driver that never started does nothing; after a failed start it sends") {
  {
    Rig<MinimalPort> rig;
    CHECK(rig.sensor.Deinitialize().ok());
    CHECK_EQ(rig.port.state.transfers, 0);
  }
  {
    Rig<MinimalPort> rig;
    rig.port.state.chip.device_id = 0;
    CHECK_EQ(rig.sensor.EnsureInitialized().error, Error::DeviceIdMismatch);
    CHECK(rig.sensor.Deinitialize().ok()); // the chip may hold our configuration: shut it down
    CHECK_EQ(rig.Writes(reg::kConfiguration), 1);
  }
}

TEST_CASE("Deinitialize sends the shutdown word even when the driver doubts the chip") {
  tmp117::Config c{};
  c.averaging = tmp117::Averaging::X32; // differs from the factory word, so a reset shows
  Rig<MinimalPort> rig(c);
  REQUIRE(rig.Start());
  rig.port.state.chip.PowerCycle(); // the driver does not know
  rig.Wait(2);
  const Error e = rig.sensor.ReadStatus().error;
  CHECK_EQ(e, Error::UnexpectedReset); // now it knows: uninitialised
  const int writes = rig.Writes(reg::kConfiguration);
  CHECK(rig.sensor.Deinitialize().ok());
  CHECK_EQ(rig.Writes(reg::kConfiguration), writes + 1);
}

// --- One-shot
// --------------------------------------------------------------------------------------------------

TEST_CASE("A one-shot conversion returns the temperature and leaves the chip in shutdown") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.chip.temperature_counts = Counts(40);
  const auto t = rig.sensor.ReadOneShotMilliC();
  REQUIRE(t.ok());
  CHECK_EQ(t.value, 40000);
  CHECK_EQ(rig.port.state.chip.RawConfig() & chip::kModMask, 0x0400);
  CHECK_EQ(rig.sensor.GetConfig().mode, tmp117::Mode::Shutdown);
}

TEST_CASE("The one-shot wait follows the AVG setting actually in use") {
  struct Row {
    tmp117::Averaging avg;
    uint32_t conversions;
  };
  for (const Row row : {Row{tmp117::Averaging::X1, 1}, Row{tmp117::Averaging::X8, 8},
                        Row{tmp117::Averaging::X32, 32}, Row{tmp117::Averaging::X64, 64}}) {
    tmp117::Config c{};
    c.averaging = row.avg;
    Rig<MinimalPort> rig(c);
    REQUIRE(rig.Start());
    const uint64_t before = rig.port.delayed_us;
    const auto t = rig.sensor.ReadOneShotMilliC();
    CHECK(t.ok());
    const uint64_t spent = rig.port.delayed_us - before;
    // At least the conversions at their typical time, never longer than the maximum plus margin.
    CHECK(spent >= (uint64_t{row.conversions} * 15500U) - 1000U);
    CHECK(spent <= uint64_t{row.conversions + 2U} * 17500U);
  }
}

TEST_CASE("A one-shot on a slow oscillator still finishes (ambiguity A16)") {
  Rig<MinimalPort> rig;
  rig.port.state.chip.osc_error_pct = 10;
  REQUIRE(rig.Start());
  CHECK(rig.sensor.ReadOneShotMilliC().ok());
}

TEST_CASE("A one-shot that never finishes is a Timeout and the driver sends shutdown") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.chip.conversion_stuck = true;
  const int writes = rig.Writes(reg::kConfiguration);
  CHECK_EQ(rig.sensor.ReadOneShotMilliC().error, Error::Timeout);
  CHECK(rig.Writes(reg::kConfiguration) >= writes + 2); // start, then shutdown
  CHECK_EQ(rig.port.state.chip.RawConfig() & chip::kModMask, 0x0400);
  rig.port.state.chip.conversion_stuck = false;
  CHECK(rig.sensor.ReadOneShotMilliC().ok()); // usable again
}

TEST_CASE("While a one-shot is pending, setters and reads return Busy and send nothing") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.sensor.StartOneShot().ok());
  CHECK_EQ(rig.sensor.StartOneShot().error, Error::Busy);
  const int before = rig.port.state.transfers;
  CHECK_EQ(rig.sensor.SetAveraging(tmp117::Averaging::X1).error, Error::Busy);
  CHECK_EQ(rig.sensor.SetCycleTime(tmp117::CycleTime::S4).error, Error::Busy);
  CHECK_EQ(rig.sensor.SetAlertFunction(tmp117::AlertFunction::Threshold).error, Error::Busy);
  CHECK_EQ(rig.sensor.SetAlertPolarity(tmp117::AlertPolarity::ActiveHigh).error, Error::Busy);
  CHECK_EQ(rig.sensor.SetAlertSource(tmp117::AlertSource::DataReady).error, Error::Busy);
  CHECK_EQ(rig.sensor.SetLimits(0, 1000).error, Error::Busy);
  CHECK_EQ(rig.sensor.SetOffsetMilliC(5).error, Error::Busy);
  CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Continuous).error, Error::Busy);
  CHECK_EQ(rig.sensor.Configure({}).error, Error::Busy);
  CHECK_EQ(rig.sensor.ReadTemperatureRaw().error, Error::Busy);
  CHECK_EQ(rig.sensor.ProgramUserWord(1).error, Error::Busy);
  CHECK_EQ(rig.sensor.ProgramPowerOnDefaults({}).error, Error::Busy);
  CHECK_EQ(rig.port.state.transfers, before);
  // Shutdown always goes through, and ends the pending conversion.
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  CHECK(rig.sensor.SetAveraging(tmp117::Averaging::X1).ok());
}

TEST_CASE("WaitDataReady checks its argument and times out") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const int before = rig.port.state.transfers;
  CHECK_EQ(rig.sensor.WaitDataReady(0).error, Error::InvalidArgument);
  CHECK_EQ(rig.sensor.WaitDataReady(tmp117::kMaxWaitMs + 1U).error, Error::InvalidArgument);
  CHECK_EQ(rig.port.state.transfers, before);
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  REQUIRE(rig.sensor.ReadStatus().ok()); // clears a stale Data_Ready
  rig.port.state.chip.conversion_stuck = true;
  CHECK_EQ(rig.sensor.WaitDataReady(5).error, Error::Timeout);
}

// --- Status, latch and the ALERT pin
// --------------------------------------------------------------------------------------------------

TEST_CASE("One Configuration read returns every flag, and the latch keeps what the chip cleared") {
  tmp117::Config c{};
  c.high_limit_mc = 30000;
  c.low_limit_mc = 10000;
  c.cycle_time = tmp117::CycleTime::Ms125;
  Rig<MinimalPort> rig(c);
  rig.port.state.chip.temperature_counts = Counts(35);
  REQUIRE(rig.Start());
  const auto first = rig.sensor.ReadStatus();
  REQUIRE(first.ok());
  CHECK(first.value.high_alert);
  CHECK(!first.value.low_alert);
  const auto second = rig.sensor.ReadStatus(); // the chip cleared the flag on the first read
  CHECK(second.ok());
  const auto latched = rig.sensor.GetLatchedStatus();
  CHECK(latched.high_alert); // not lost
  rig.sensor.ClearLatchedStatus();
  const auto cleared = rig.sensor.GetLatchedStatus();
  CHECK(!cleared.high_alert && !cleared.low_alert && !cleared.data_ready);
}

TEST_CASE("An alert flag is not lost by the driver's own reads (CheckHealth, WaitDataReady)") {
  tmp117::Config c{};
  c.high_limit_mc = 30000;
  c.low_limit_mc = 10000;
  c.cycle_time = tmp117::CycleTime::Ms125;
  Rig<MinimalPort> rig(c);
  rig.port.state.chip.temperature_counts = Counts(35);
  REQUIRE(rig.Start());
  rig.sensor.ClearLatchedStatus();
  CHECK(rig.sensor.CheckHealth().ok()); // reads Configuration inside
  const auto latched = rig.sensor.GetLatchedStatus();
  CHECK(latched.high_alert);
  CHECK(latched.data_ready);
}

TEST_CASE("Data_Ready is latched across a status read and cleared by ClearLatchedStatus") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.sensor.ClearLatchedStatus();
  REQUIRE(rig.sensor.ReadStatus().ok());
  CHECK(rig.sensor.GetLatchedStatus().data_ready);
  REQUIRE(rig.sensor.ReadStatus().ok()); // the chip shows DR = 0 now
  CHECK(rig.sensor.GetLatchedStatus().data_ready);
  rig.sensor.ClearLatchedStatus();
  CHECK(!rig.sensor.GetLatchedStatus().data_ready);
}

TEST_CASE("The ALERT pin level is read through the optional hook and POL decides what is active") {
  tmp117::Config c{};
  c.high_limit_mc = 30000;
  c.low_limit_mc = 10000;
  c.cycle_time = tmp117::CycleTime::Ms125;
  Rig<FullPort> rig(c);
  rig.port.state.chip.temperature_counts = Counts(35);
  REQUIRE(rig.Start());
  const auto low_active = rig.sensor.ReadAlertPin();
  REQUIRE(low_active.ok());
  CHECK(low_active.value); // above the high limit: the pin is active (low level, active low)
  CHECK(rig.sensor.SetAlertPolarity(tmp117::AlertPolarity::ActiveHigh).ok());
  const auto high_active = rig.sensor.ReadAlertPin();
  CHECK(high_active.value); // the flag still shows (set again by the next conversions)
  rig.port.state.chip.temperature_counts = Counts(20);
  rig.Wait(300);
  REQUIRE(rig.sensor.ReadStatus().ok()); // the read clears the flags
  rig.Wait(300);
  const auto inactive = rig.sensor.ReadAlertPin();
  CHECK(!inactive.value);
  rig.port.pin_ok = false;
  CHECK_EQ(rig.sensor.ReadAlertPin().error, Error::Bus);
}

TEST_CASE("Without the ReadAlertPin hook the call is Unsupported and nothing is sent") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const int before = rig.port.state.transfers;
  CHECK_EQ(rig.sensor.ReadAlertPin().error, Error::Unsupported);
  CHECK_EQ(rig.port.state.transfers, before);
}

TEST_CASE("The SMBus alert response is not part of this release") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const int before = rig.port.state.transfers;
  CHECK_EQ(rig.sensor.SmbusAlertResponse().error, Error::Unsupported);
  CHECK_EQ(rig.port.state.transfers, before);
}

// --- Setters
// --------------------------------------------------------------------------------------------------

TEST_CASE("Each setter changes only the field it names, in one whole-register write") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const uint16_t reset = chip::kConfigurationReset;
  auto cfg = [&rig]() { return rig.port.state.chip.RawConfig(); };
  CHECK(rig.sensor.SetAveraging(tmp117::Averaging::X64).ok());
  CHECK_EQ(cfg(), (uint32_t{reset} & ~uint32_t{chip::kAvgMask}) | 0x0060U);
  CHECK(rig.sensor.SetAveraging(tmp117::Averaging::X8).ok());
  CHECK(rig.sensor.SetCycleTime(tmp117::CycleTime::S16).ok());
  CHECK_EQ(cfg(), (uint32_t{reset} & ~uint32_t{chip::kConvMask}) | 0x0380U);
  CHECK(rig.sensor.SetCycleTime(tmp117::CycleTime::S1).ok());
  CHECK(rig.sensor.SetAlertFunction(tmp117::AlertFunction::Threshold).ok());
  CHECK_EQ(cfg(), reset | chip::kTherm);
  CHECK(rig.sensor.SetAlertFunction(tmp117::AlertFunction::Window).ok());
  CHECK(rig.sensor.SetAlertPolarity(tmp117::AlertPolarity::ActiveHigh).ok());
  CHECK_EQ(cfg(), reset | chip::kPol);
  CHECK(rig.sensor.SetAlertPolarity(tmp117::AlertPolarity::ActiveLow).ok());
  CHECK(rig.sensor.SetAlertSource(tmp117::AlertSource::DataReady).ok());
  CHECK_EQ(cfg(), reset | chip::kDrAlert);
  CHECK(rig.sensor.SetAlertSource(tmp117::AlertSource::Flags).ok());
  CHECK_EQ(cfg(), reset);
  CHECK_EQ(rig.port.state.chip.soft_resets, 0); // bit 1 is never set by a setter
}

TEST_CASE("Limits and the offset are written as counts and read back") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  CHECK(rig.sensor.SetLimits(-5000, 45000).ok());
  CHECK_EQ(tmp117::detail::AsSigned16(rig.port.state.chip.RawLow()), -640);
  CHECK_EQ(rig.port.state.chip.RawHigh(), 5760);
  CHECK(rig.sensor.SetOffsetMilliC(-2000).ok());
  CHECK_EQ(tmp117::detail::AsSigned16(rig.port.state.chip.RawOffset()), -256);
  CHECK_EQ(rig.sensor.GetConfig().high_limit_mc, 45000);
  rig.port.state.chip.temperature_counts = Counts(20);
  rig.Wait(1200);
  const auto t = rig.sensor.ReadTemperatureMilliC();
  CHECK_EQ(t.value, 18000); // the offset is added by the chip
}

TEST_CASE("An invalid argument sends nothing: values are checked, never clamped") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const int before = rig.port.state.transfers;
  CHECK_EQ(rig.sensor.SetLimits(-256001, 0).error, Error::InvalidArgument);
  CHECK_EQ(rig.sensor.SetLimits(0, 255993).error, Error::InvalidArgument);
  CHECK_EQ(rig.sensor.SetLimits(5000, 1000).error, Error::InvalidArgument); // inverted (A10)
  CHECK_EQ(rig.sensor.SetOffsetMilliC(300000).error, Error::InvalidArgument);
  CHECK_EQ(rig.sensor.SetOffsetMilliC(-256001).error, Error::InvalidArgument);
  // The underlying types are fixed, so these are valid values that name no enumerator.
  CHECK_EQ(rig.sensor.SetMode(tmp117_test::RawEnum<tmp117::Mode>(2)).error, Error::InvalidArgument);
  CHECK_EQ(rig.sensor.SetAveraging(tmp117_test::RawEnum<tmp117::Averaging>(4)).error,
           Error::InvalidArgument);
  CHECK_EQ(rig.sensor.SetCycleTime(tmp117_test::RawEnum<tmp117::CycleTime>(8)).error,
           Error::InvalidArgument);
  CHECK_EQ(rig.sensor.SetAlertFunction(tmp117_test::RawEnum<tmp117::AlertFunction>(2)).error,
           Error::InvalidArgument);
  CHECK_EQ(rig.sensor.SetAlertPolarity(tmp117_test::RawEnum<tmp117::AlertPolarity>(2)).error,
           Error::InvalidArgument);
  CHECK_EQ(rig.sensor.SetAlertSource(tmp117_test::RawEnum<tmp117::AlertSource>(2)).error,
           Error::InvalidArgument);
  tmp117::Config bad{};
  bad.eeprom_timeout_ms = 0;
  CHECK_EQ(rig.sensor.Configure(bad).error, Error::InvalidArgument);
  CHECK_EQ(rig.sensor.ProgramPowerOnDefaults(bad).error, Error::InvalidArgument);
  CHECK_EQ(rig.port.state.transfers, before); // not one frame
  // The limits at the edge of the range are accepted.
  CHECK(rig.sensor.SetLimits(-256000, 255992).ok());
}

TEST_CASE("Configure changes only the registers that differ") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const int cfg_writes = rig.Writes(reg::kConfiguration);
  const int high_writes = rig.Writes(reg::kHighLimit);
  tmp117::Config next = rig.sensor.GetConfig();
  next.low_limit_mc = -20000;
  CHECK(rig.sensor.Configure(next).ok());
  CHECK_EQ(rig.Writes(reg::kConfiguration), cfg_writes);
  CHECK_EQ(rig.Writes(reg::kHighLimit), high_writes);
  CHECK_EQ(rig.Writes(reg::kLowLimit), 2); // start-up and this one
}

TEST_CASE("ReadRegister refuses a pointer outside the map and sends nothing") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const int before = rig.port.state.transfers;
  for (const uint8_t p :
       {uint8_t{0x09}, uint8_t{0x0A}, uint8_t{0x0E}, uint8_t{0x10}, uint8_t{0x7F}, uint8_t{0xFF}}) {
    CHECK_EQ(rig.sensor.ReadRegister(p).error, Error::InvalidArgument);
  }
  CHECK_EQ(rig.port.state.transfers, before);
  const auto id = rig.sensor.ReadRegister(reg::kDeviceId);
  CHECK_EQ(id.value, chip::kDeviceIdValue);
  CHECK_EQ(rig.port.state.chip.bad_pointer_accesses, 0);
}

TEST_CASE("ReadRegister of Configuration still latches the flags the chip clears") {
  tmp117::Config c{};
  c.high_limit_mc = 30000;
  c.low_limit_mc = 10000;
  c.cycle_time = tmp117::CycleTime::Ms125;
  Rig<MinimalPort> rig(c);
  rig.port.state.chip.temperature_counts = Counts(35);
  REQUIRE(rig.Start());
  rig.sensor.ClearLatchedStatus();
  const auto raw = rig.sensor.ReadRegister(reg::kConfiguration);
  CHECK((raw.value & chip::kHighAlert) != 0U);
  CHECK(rig.sensor.GetLatchedStatus().high_alert);
  CHECK_EQ(rig.sensor.GetLastConfigWord(), raw.value);
}

TEST_CASE("The traceability words are read, never written") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const auto id = rig.sensor.ReadUniqueId();
  REQUIRE(id.ok());
  CHECK_EQ(id.value.eeprom1, 0x1A2B);
  CHECK_EQ(id.value.eeprom3, 0x3C4D);
  CHECK_EQ(rig.port.state.chip.traceability_writes, 0);
  CHECK_EQ(rig.port.state.chip.readonly_writes, 0);
}

// --- Reset evidence and uncertain writes
// --------------------------------------------------------------------------------------------------

TEST_CASE("A reset the driver did not make is UnexpectedReset, and nothing is written after it") {
  tmp117::Config c{};
  c.averaging = tmp117::Averaging::X32; // differs from the factory word: a reset shows
  Rig<MinimalPort> rig(c);
  REQUIRE(rig.Start());
  rig.port.state.chip.PowerCycle();
  rig.Wait(3);
  CHECK_EQ(rig.sensor.ReadStatus().error, Error::UnexpectedReset);
  CHECK(!rig.sensor.IsInitialized());
  const int writes = rig.port.state.chip.writes;
  CHECK_EQ(rig.sensor.SetLimits(0, 1000).error, Error::NotInitialized);
  CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Continuous).error, Error::NotInitialized);
  CHECK_EQ(rig.sensor.StartOneShot().error, Error::NotInitialized);
  CHECK_EQ(rig.port.state.chip.writes, writes);
  CHECK(rig.sensor.EnsureInitialized().ok()); // the application starts it again
  CHECK_EQ(rig.port.state.chip.RawConfig() & chip::kAvgMask, 0x0040);
}

TEST_CASE("A reset to the factory values is seen by the next temperature read (ambiguity A9)") {
  Rig<MinimalPort>
      rig; // Config{} equals the chip's reset values: the Configuration compare is blind
  REQUIRE(rig.Start());
  REQUIRE(rig.sensor.ReadTemperatureMilliC().ok());
  rig.port.state.chip.PowerCycle();
  rig.Wait(3);
  CHECK(rig.sensor.ReadStatus().ok()); // not detectable from Configuration alone
  const Error e = rig.sensor.ReadTemperatureMilliC().error;
  CHECK_EQ(e, Error::UnexpectedReset); // Temp_Result reads 8000h again after a result was seen
  CHECK(!rig.sensor.IsInitialized());
}

TEST_CASE("A reload still running is evidence of a reset") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.chip.GeneralCallReset(); // another master on the bus resets everything
  CHECK_EQ(rig.sensor.ReadStatus().error, Error::UnexpectedReset);
}

TEST_CASE("CheckHealth: a changed Device_ID leaves the driver uninitialised") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  CHECK(rig.sensor.CheckHealth().ok());
  rig.port.state.chip.device_id = 0x0000;
  CHECK_EQ(rig.sensor.CheckHealth().error, Error::DeviceIdMismatch);
  CHECK(!rig.sensor.IsInitialized());
}

TEST_CASE("A write that failed and had taken effect is adopted after the chip is asked") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.ack_lost_at =
      rig.port.state.transfers; // the next transfer: applied, reported lost
  CHECK_EQ(rig.sensor.SetAveraging(tmp117::Averaging::X32).error, Error::Bus);
  CHECK_EQ(rig.sensor.GetConfig().averaging, tmp117::Averaging::X8); // the copy is the old value
  CHECK(rig.sensor.SetAlertPolarity(tmp117::AlertPolarity::ActiveHigh).ok()); // asks first
  CHECK_EQ(rig.sensor.GetConfig().averaging, tmp117::Averaging::X32);
  CHECK_EQ(rig.port.state.chip.RawConfig() & chip::kAvgMask, 0x0040);
  CHECK(rig.sensor.IsInitialized());
}

TEST_CASE("A write that failed and did not take effect keeps the old value") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.fail_transfer = rig.port.state.transfers; // a NACK, not applied
  CHECK_EQ(rig.sensor.SetLimits(0, 1000).error, Error::Bus);
  CHECK(rig.sensor.SetAveraging(tmp117::Averaging::X1).ok());
  CHECK_EQ(rig.sensor.GetConfig().high_limit_mc, 192000);
  CHECK_EQ(rig.port.state.chip.RawHigh(), chip::kHighLimitReset);
  CHECK(rig.sensor.IsInitialized());
}

TEST_CASE("A Configuration write that failed and did not take effect keeps the old copy") {
  // The settle reads the old word back: the copy must not adopt the attempted fields, or the next
  // write (and the shutdown word) would carry settings the chip never had.
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.fail_transfer = rig.port.state.transfers; // a NACK, not applied
  CHECK_EQ(rig.sensor.SetAveraging(tmp117::Averaging::X32).error, Error::Bus);
  CHECK(rig.sensor.SetCycleTime(tmp117::CycleTime::S4).ok()); // settles first
  CHECK(rig.sensor.GetConfig().averaging == tmp117::Averaging::X8);
  CHECK(rig.sensor.GetConfig().cycle_time == tmp117::CycleTime::S4);
  CHECK_EQ((static_cast<uint32_t>(rig.port.state.chip.RawConfig()) >> 5U) & 3U, 1U); // AVG = 01
  CHECK(rig.sensor.IsInitialized());
}

TEST_CASE(
    "A one-shot start whose ACK was lost and that finished meanwhile leaves the copy in shutdown") {
  // The write took effect, the conversion ran and the chip is back in shutdown (MOD 01) when the
  // settle reads it: not a reset, the one-shot is over and the copy follows the chip.
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.ack_lost_at = rig.port.state.transfers + 1; // the poll read is 0, the write 1
  CHECK_EQ(rig.sensor.StartOneShot().error, Error::Bus);
  rig.port.state.ack_lost_at = -1;
  rig.Wait(500); // longer than eight conversions
  CHECK(rig.sensor.SetCycleTime(tmp117::CycleTime::S4).ok());
  CHECK(rig.sensor.GetConfig().mode == tmp117::Mode::Shutdown);
  CHECK(rig.sensor.IsInitialized());
}

TEST_CASE("A one-shot start in doubt and a chip whose settings differ from the copy is a reset") {
  // The start write was lost; the chip then restarted from other defaults and finished its
  // power-up load, so no EEPROM_Busy shows it: only the setting bits do.
  tmp117::Config c{};
  c.averaging = tmp117::Averaging::X32;
  c.cycle_time = tmp117::CycleTime::Ms125;
  Rig<MinimalPort> rig(c);
  REQUIRE(rig.Start());
  rig.port.state.fail_transfer = rig.port.state.transfers + 1; // the poll read is 0, the write 1
  CHECK_EQ(rig.sensor.StartOneShot().error, Error::Bus);
  rig.port.state.chip.PowerCycle();
  rig.Wait(30);
  CHECK_EQ(rig.sensor.SetCycleTime(tmp117::CycleTime::S4).error, Error::UnexpectedReset);
  CHECK(!rig.sensor.IsInitialized());
}

TEST_CASE("A one-shot start whose ACK was lost is picked up, not repeated") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.ack_lost_at = rig.port.state.transfers + 1; // the poll read is 0, the write 1
  CHECK_EQ(rig.sensor.StartOneShot().error, Error::Bus);
  CHECK(rig.sensor.WaitDataReady(2000).ok()); // settles: the conversion runs, then finishes
  const auto t = rig.sensor.ReadTemperatureMilliC();
  CHECK(t.ok());
}

// --- Resets by command
// --------------------------------------------------------------------------------------------------

TEST_CASE("SoftReset resets this chip and starts again with the configuration") {
  tmp117::Config c{};
  c.averaging = tmp117::Averaging::X32;
  c.high_limit_mc = 80000;
  Rig<MinimalPort> rig(c);
  REQUIRE(rig.Start());
  CHECK(rig.sensor.SoftReset().ok());
  CHECK_EQ(rig.port.state.chip.soft_resets, 1);
  CHECK_EQ(rig.port.state.chip.general_call_resets, 0);
  CHECK(rig.sensor.IsInitialized());
  CHECK_EQ(rig.port.state.chip.RawHigh(), 80000 * 128 / 1000);
  CHECK_EQ(rig.port.state.chip.writes_during_load, 0);
}

TEST_CASE("GeneralCallReset needs the opt-in; without it nothing is sent") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const int before = rig.port.state.transfers;
  CHECK_EQ(rig.sensor.GeneralCallReset().error, Error::Unsupported);
  CHECK_EQ(rig.port.state.transfers, before);
  tmp117::Config c{};
  c.allow_general_call = true;
  Rig<MinimalPort> opted(c);
  REQUIRE(opted.Start());
  const std::size_t frames = opted.port.state.frame_count;
  CHECK(opted.sensor.GeneralCallReset().ok());
  // Golden frame (Figure 7-12, p. 24): address 00h, one byte 06h.
  const auto& f = opted.port.state.frames[frames];
  CHECK_EQ(f.address, 0x00);
  CHECK_EQ(f.length, 1);
  CHECK_EQ(f.bytes[0], 0x06);
  CHECK_EQ(opted.port.state.chip.general_call_resets, 1);
  CHECK(opted.sensor.IsInitialized());
}

TEST_CASE("A failed reset command leaves the driver uninitialised") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.fail_transfer = rig.port.state.transfers;
  CHECK_EQ(rig.sensor.SoftReset().error, Error::Bus);
  CHECK(!rig.sensor.IsInitialized());
}

// --- Port hooks
// --------------------------------------------------------------------------------------------------

TEST_CASE("The Log hook receives lifecycle lines and errors") {
  Rig<FullPort> rig;
  REQUIRE(rig.Start());
  CHECK(rig.port.log_lines > 0);
  rig.port.state.chip.device_id = 0;
  CHECK_EQ(rig.sensor.CheckHealth().error, Error::DeviceIdMismatch);
  CHECK(rig.port.error_lines > 0);
}

TEST_CASE("The bus requirements are constants of the interface header") {
  static_assert(tmp117::kMaxClockHz == 400'000U);
  static_assert(tmp117::kMinClockHz == 5'000U);
  static_assert(tmp117::kRepeatedStart);
  static_assert(tmp117::kI2cAddress == 0x48U);
  CHECK(tmp117::GetDriverVersionMajor() <= 99U);
}
