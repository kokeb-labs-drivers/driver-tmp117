/**
 * @file test_errors.cpp
 * @brief The `Error` enum follows the standard, and its values are returned by real calls.
 *
 * - The base set (0 to 9) has the names and values every driver of the org shares.
 * - Every chip-specific value (16 and up) MUST be returned by some call: the enumerators are found
 *   through `ToString()` (-Wswitch keeps it complete), so a new value fails this test until a
 *   scenario below produces it.
 * - Base values MAY go unreturned; `kRequireEveryBaseValue` says whether this driver returns them
 *   all (the TMP117 does not: Crc and DeviceFault). Set it to false, and list the unreturned ones
 * on the API guide, for a chip without, say, a CRC.
 */
#include <array>
#include <cstring>

#include "mock_tmp117.hpp"
#include "test_harness.hpp"
#include "test_support.hpp"
#include "tmp117.hpp"

using tmp117::Error;
using tmp117_test::FullPort;
using tmp117_test::MinimalPort;

static_assert(static_cast<int>(Error::Ok) == 0 && static_cast<int>(Error::NotInitialized) == 1 &&
                  static_cast<int>(Error::InvalidArgument) == 2 &&
                  static_cast<int>(Error::Bus) == 3 && static_cast<int>(Error::Timeout) == 4 &&
                  static_cast<int>(Error::DeviceIdMismatch) == 5 &&
                  static_cast<int>(Error::Crc) == 6 && static_cast<int>(Error::Busy) == 7 &&
                  static_cast<int>(Error::DeviceFault) == 8 &&
                  static_cast<int>(Error::Unsupported) == 9,
              "the base Error set has fixed values (standards/driver.md)");

namespace {

// The TMP117 has no CRC and no fault flag, so Crc and DeviceFault are never returned (they stay in
// the shared base set for a uniform ABI). Every chip-specific value is required.
constexpr bool kRequireEveryBaseValue = false;
constexpr int kFirstChipSpecific = 16;

std::array<bool, 256>& Seen() noexcept {
  static constinit std::array<bool, 256> seen{};
  return seen;
}

void Record(Error e) {
  Seen()[static_cast<uint8_t>(e)] = true;
}

void RunScenarios() {
  using tmp117_test::Rig;
  { // Ok, NotInitialized, NoMeasurement
    Rig<MinimalPort> rig;
    Record(rig.sensor.ReadTemperatureRaw().error);
    Record(rig.sensor.EnsureInitialized().error);
    Record(rig.sensor.ReadTemperatureRaw().error);
  }
  { // InvalidArgument
    tmp117::Config c{};
    c.low_limit_mc = 100;
    c.high_limit_mc = 0;
    Rig<MinimalPort> rig(c);
    Record(rig.sensor.EnsureInitialized().error);
  }
  { // Bus
    Rig<MinimalPort> rig;
    rig.port.state.fail_transfer = 0;
    Record(rig.sensor.EnsureInitialized().error);
  }
  { // Timeout
    Rig<MinimalPort> rig;
    rig.port.state.chip.conversion_stuck = true;
    if (rig.Start()) {
      Record(rig.sensor.WaitDataReady(5).error);
    }
  }
  { // DeviceIdMismatch
    Rig<MinimalPort> rig;
    rig.port.state.chip.device_id = 0x0000;
    Record(rig.sensor.EnsureInitialized().error);
  }
  { // Busy
    Rig<MinimalPort> rig;
    if (rig.Start()) {
      Record(rig.sensor.StartOneShot().error);
      Record(rig.sensor.StartOneShot().error);
    }
  }
  { // Unsupported
    Rig<MinimalPort> rig;
    if (rig.Start()) {
      Record(rig.sensor.SmbusAlertResponse().error);
    }
  }
  { // UnexpectedReset: a configuration that differs from the factory word, so a reset shows (A9)
    tmp117::Config c{};
    c.averaging = tmp117::Averaging::X32;
    Rig<MinimalPort> rig(c);
    if (rig.Start()) {
      rig.port.state.chip.PowerCycle();
      rig.Wait(3);
      Record(rig.sensor.CheckHealth().error);
    }
  }
  { // EepromVerifyFailed
    Rig<MinimalPort> rig;
    rig.port.state.chip.reload_flips_high_limit = true;
    if (rig.Start()) {
      Record(rig.sensor.ProgramPowerOnDefaults({}).error);
    }
  }
  { // BadReply: a reserved bit that must read 0 reads 1
    Rig<MinimalPort> rig;
    if (rig.Start()) {
      rig.port.state.reads = 0;
      rig.port.state.flip_at = 0;
      rig.port.state.flip_mask = 0x0001;
      Record(rig.sensor.ReadStatus().error);
    }
  }
}

} // namespace

TEST_CASE("Every chip-specific Error value is returned by some call") {
  RunScenarios();
  int enumerators = 0;
  for (int v = 0; v < 256; ++v) {
    const auto e = static_cast<Error>(v);
    if (std::strcmp(tmp117::ToString(e), "Unknown") == 0) {
      continue;
    }
    ++enumerators;
    const bool required = v >= kFirstChipSpecific || kRequireEveryBaseValue;
    const bool seen = Seen()[static_cast<std::size_t>(v)];
    if (!seen) {
      std::printf("    %s: Error::%s is never returned\n", required ? "FAIL" : "note",
                  tmp117::ToString(e));
    }
    CHECK(seen || !required);
    CHECK(v < 10 || v >= kFirstChipSpecific); // 10 to 15 are reserved for the base set
  }
  CHECK(enumerators >= 2);
}

TEST_CASE("A reset during a one-shot is seen even with the default configuration (A9)") {
  tmp117_test::Rig<tmp117_test::MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.sensor.StartOneShot().ok());
  rig.port.state.chip.PowerCycle(); // back to continuous: MOD is neither 11 nor 01
  rig.Wait(5);                      // the load is over, so EEPROM_Busy is not the evidence
  CHECK_EQ(rig.sensor.ReadStatus().error, Error::UnexpectedReset);
}

TEST_CASE("Switch-off still sends after the driver lost its state (RD-05)") {
  tmp117::Config c{};
  c.averaging = tmp117::Averaging::X32; // differs from the factory word, so a reset shows (A9)
  tmp117_test::Rig<tmp117_test::MinimalPort> rig(c);
  REQUIRE(rig.Start());
  rig.port.state.chip.PowerCycle();
  rig.Wait(5);
  REQUIRE(rig.sensor.ReadStatus().error == Error::UnexpectedReset);
  const int before = rig.port.state.transfers;
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  CHECK(rig.port.state.transfers > before);
  CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Continuous).error, Error::NotInitialized);
}

TEST_CASE("A failed Configure does not grant the general-call opt-in") {
  tmp117_test::Rig<tmp117_test::MinimalPort> rig;
  REQUIRE(rig.Start());
  tmp117::Config c{};
  c.allow_general_call = true;
  c.averaging = tmp117::Averaging::X64;
  rig.port.state.fail_transfer = rig.port.state.transfers;
  CHECK_EQ(rig.sensor.Configure(c).error, Error::Bus);
  CHECK(!rig.sensor.GetConfig().allow_general_call);
}
