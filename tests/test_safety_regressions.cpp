/**
 * @file test_safety_regressions.cpp
 * @brief Regression tests for faults found after the first review round: switch-off refused after a
 * failed settle, EEPROM left unlocked across a restart, one-shot completion under both readings of
 * ambiguity A19, a raw read during a one-shot, and stuck lines on the status and identity reads;
 * after the second round: switch-off of an uninitialised driver whose EEPROM_UL read fails (A21),
 * the switch-off force flag, a reserved bit in EEPROM_UL, the one-shot doubt cleared by a write;
 * after the fourth round: a failed shutdown write while a one-shot runs, and the single-flip
 * residual risk of A21. Each test fails on the code it was written against, except the last, which
 * pins a documented residual risk.
 */
#include <cstddef>

#include "mock_tmp117.hpp"
#include "test_harness.hpp"
#include "test_support.hpp"
#include "tmp117.hpp"

using tmp117::Error;
using tmp117_test::MinimalPort;
using tmp117_test::Rig;
using tmp117_test::Stuck;
namespace reg = tmp117::reg;
namespace chip = tmp117_test::chip;

namespace {

/// A configuration that differs from the factory word.
tmp117::Config Custom() {
  tmp117::Config c{};
  c.averaging = tmp117::Averaging::X32;
  c.cycle_time = tmp117::CycleTime::Ms125;
  c.high_limit_mc = 60000;
  c.low_limit_mc = 0;
  return c;
}

/// The configuration whose Configuration word is 0000h: continuous, AVG 00, CONV 000, alert mode,
/// active low, flags.
tmp117::Config AllZero() {
  tmp117::Config c{};
  c.averaging = tmp117::Averaging::X1;
  c.cycle_time = tmp117::CycleTime::Ms15p5;
  return c;
}

uint32_t ModOf(const Rig<MinimalPort>& rig) {
  return (static_cast<uint32_t>(rig.port.state.chip.RawConfig()) & chip::kModMask) >> 10U;
}

} // namespace

TEST_CASE("Switch-off is sent when the settle read of a write in doubt fails") {
  // Doubt on a limit, then every read fails: the settle cannot run, the shutdown must still go out.
  {
    Rig<MinimalPort> rig(Custom());
    REQUIRE(rig.Start());
    rig.port.state.fail_transfer = rig.Transfers(); // the first write of SetLimits
    CHECK_EQ(rig.sensor.SetLimits(1000, 50000).error, Error::Bus);
    rig.port.state.fail_transfer = -1;
    rig.port.state.fail_reads = true;
    const int before = rig.Writes(reg::kConfiguration);
    CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
    CHECK_EQ(rig.Writes(reg::kConfiguration), before + 1);
    CHECK_EQ(ModOf(rig), 1U);
  }
  // Doubt on Configuration, then every reply breaks a fixed bit (BadReply): same.
  {
    Rig<MinimalPort> rig(Custom());
    REQUIRE(rig.Start());
    rig.port.state.fail_transfer = rig.Transfers();
    CHECK_EQ(rig.sensor.SetAveraging(tmp117::Averaging::X8).error, Error::Bus);
    rig.port.state.fail_transfer = -1;
    rig.port.state.stuck = Stuck::Ones;
    const int before = rig.Writes(reg::kConfiguration);
    CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
    CHECK_EQ(rig.Writes(reg::kConfiguration), before + 1);
    CHECK_EQ(ModOf(rig), 1U);
  }
  // A one-shot that timed out while the bus reads fail: the shutdown after the timeout still goes.
  {
    Rig<MinimalPort> rig(Custom());
    REQUIRE(rig.Start());
    CHECK(rig.sensor.StartOneShot().ok());
    rig.port.state.fail_reads = true;
    const int before = rig.Writes(reg::kConfiguration);
    CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
    CHECK_EQ(rig.Writes(reg::kConfiguration), before + 1);
    CHECK_EQ(ModOf(rig), 1U);
  }
}

TEST_CASE("Start-up locks an unlocked EEPROM before any other write") {
  Rig<MinimalPort> rig(Custom());
  rig.port.state.chip.ForceUnlock(); // a host restart during programming leaves EUN set
  REQUIRE(rig.sensor.EnsureInitialized().ok());
  const auto& m = rig.port.state.chip;
  CHECK(!m.EunSet());
  CHECK_EQ(m.eeprom_words_programmed, 0);
  // The first write is the lock (EEPROM_UL), then the limits, offset and Configuration.
  std::size_t first_write = 0;
  while ((first_write < rig.port.state.frame_count) && rig.port.state.frames[first_write].is_read) {
    ++first_write;
  }
  REQUIRE(first_write < rig.port.state.frame_count);
  CHECK_EQ(rig.port.state.frames[first_write].bytes[0], reg::kEepromUnlock);
  CHECK_EQ(rig.port.state.frames[first_write].bytes[1], 0U);
}

TEST_CASE(
    "Start-up writes nothing else when the EEPROM cannot be locked, and neither does switch-off") {
  Rig<MinimalPort> rig(Custom());
  rig.port.state.chip.eun_write_zero_locks = false; // A6: only a reset locks
  rig.port.state.chip.ForceUnlock();
  const auto r = rig.sensor.EnsureInitialized();
  CHECK_EQ(r.error, Error::EepromVerifyFailed);
  CHECK(!rig.sensor.IsInitialized());
  const auto& m = rig.port.state.chip;
  CHECK_EQ(m.eeprom_words_programmed, 0);
  CHECK_EQ(rig.Writes(reg::kConfiguration), 0);
  CHECK_EQ(rig.Writes(reg::kHighLimit), 0);
  CHECK_EQ(rig.Writes(reg::kLowLimit), 0);
  CHECK_EQ(rig.Writes(reg::kTempOffset), 0);
  // A shutdown word written now would be programmed as a power-on default: refused, nothing sent.
  CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Shutdown).error, Error::EepromVerifyFailed);
  CHECK_EQ(rig.sensor.Deinitialize().error, Error::EepromVerifyFailed);
  CHECK_EQ(rig.Writes(reg::kConfiguration), 0);
  CHECK_EQ(m.eeprom_words_programmed, 0);
  // The power cycle that locks the chip lets everything run again.
  rig.port.state.chip.PowerCycle();
  rig.Wait(3);
  CHECK(rig.sensor.EnsureInitialized().ok());
  CHECK(rig.sensor.Deinitialize().ok());
}

TEST_CASE("A relock that failed leaves the next start-up writing no EEPROM word") {
  Rig<MinimalPort> rig(Custom());
  REQUIRE(rig.Start());
  rig.port.state.chip.eun_write_zero_locks = false;
  CHECK_EQ(rig.sensor.ProgramUserWord(0x1234).error, Error::EepromVerifyFailed);
  CHECK(rig.port.state.chip.EunSet());
  const int programmed = rig.port.state.chip.eeprom_words_programmed;
  const int config_writes = rig.Writes(reg::kConfiguration);
  CHECK_EQ(rig.sensor.EnsureInitialized().error, Error::EepromVerifyFailed);
  CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, programmed);
  CHECK_EQ(rig.Writes(reg::kConfiguration), config_writes);
  CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Shutdown).error, Error::EepromVerifyFailed);
  CHECK_EQ(rig.Writes(reg::kConfiguration), config_writes);
  CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, programmed);
}

TEST_CASE("A one-shot completes whether MOD reads back 01 or stays 11 (A19)") {
  for (const bool stays_11 : {false, true}) {
    Rig<MinimalPort> rig(Custom());
    rig.port.state.chip.oneshot_mod_reads_back_11 = stays_11;
    REQUIRE(rig.Start());
    const auto t = rig.sensor.ReadOneShotMilliC();
    CHECK(t.ok());
    CHECK(rig.sensor.GetConfig().mode == tmp117::Mode::Shutdown);
    // Nothing is Busy, nothing is read as a reset, and the next one-shot runs.
    CHECK(rig.sensor.CheckHealth().ok());
    CHECK(rig.sensor.ReadStatus().ok());
    CHECK(rig.sensor.ReadTemperatureMilliC().ok());
    CHECK(rig.sensor.ReadOneShotMilliC().ok());
    CHECK(rig.sensor.SetAveraging(tmp117::Averaging::X8).ok());
    CHECK(rig.sensor.ReadOneShotMilliC().ok());
    CHECK(rig.sensor.SetLimits(-1000, 50000).ok());
    CHECK(rig.sensor.ProgramUserWord(0x5A5A).ok());
    CHECK(rig.sensor.IsInitialized());
    CHECK(rig.sensor.Deinitialize().ok());
    CHECK_EQ(ModOf(rig), 1U);
  }
}

TEST_CASE("A status read that sees the end of a one-shot completes it under either reading (A19)") {
  for (const bool stays_11 : {false, true}) {
    Rig<MinimalPort> rig(Custom());
    rig.port.state.chip.oneshot_mod_reads_back_11 = stays_11;
    REQUIRE(rig.Start());
    CHECK(rig.sensor.StartOneShot().ok());
    CHECK_EQ(rig.sensor.ReadTemperatureRaw().error, Error::Busy);
    rig.Wait(700); // 32 conversions of 15.5 ms end
    const auto s =
        rig.sensor.ReadStatus(); // reads Configuration: Data_Ready is cleared on the chip
    CHECK(s.ok());
    CHECK(s.value.data_ready);
    CHECK(rig.sensor.ReadTemperatureMilliC().ok());
    CHECK(rig.sensor.SetAveraging(tmp117::Averaging::X8).ok());
  }
}

TEST_CASE("A raw read during a one-shot is not mistaken for a reset") {
  for (const bool stays_11 : {false, true}) {
    // Configuration read raw: the chip clears Data_Ready, the driver latches what it saw.
    Rig<MinimalPort> rig(Custom());
    rig.port.state.chip.oneshot_mod_reads_back_11 = stays_11;
    REQUIRE(rig.Start());
    CHECK(rig.sensor.StartOneShot().ok());
    rig.Wait(700);
    CHECK(rig.sensor.ReadRegister(reg::kConfiguration).ok());
    CHECK(rig.sensor.WaitDataReady(5).ok());
    CHECK(rig.sensor.IsInitialized());
    CHECK(rig.sensor.ReadTemperatureMilliC().ok());
    // Temp_Result read raw would clear Data_Ready unseen: refused while the one-shot is pending.
    CHECK(rig.sensor.StartOneShot().ok());
    rig.Wait(700);
    const std::size_t frames = rig.port.state.frame_count;
    CHECK_EQ(rig.sensor.ReadRegister(reg::kTempResult).error, Error::Busy);
    CHECK_EQ(rig.port.state.frame_count, frames);
    CHECK(rig.sensor.WaitDataReady(5).ok());
    CHECK(rig.sensor.ReadTemperatureMilliC().ok());
  }
}

TEST_CASE("A line stuck at 0 never passes the status reads, whatever the configuration") {
  const tmp117::Config configs[] = {tmp117::Config{}, Custom(), AllZero()};
  for (const tmp117::Config& cfg : configs) {
    for (const Stuck pattern : {Stuck::Zeros, Stuck::Ones, Stuck::RepeatLast}) {
      Rig<MinimalPort> rig(cfg);
      rig.port.state.chip.temperature_counts = tmp117_test::Counts(200); // above 192 degC
      REQUIRE(rig.Start());
      rig.port.state.stuck = pattern;
      rig.port.state.last_reply = 0x0000;
      CHECK(rig.sensor.ReadStatus().error != Error::Ok);
      CHECK(rig.sensor.CheckHealth().error != Error::Ok);
      CHECK(rig.sensor.WaitDataReady(20).error != Error::Ok);
      CHECK(rig.sensor.StartOneShot().error != Error::Ok);
    }
  }
  // The same chip, not stuck, with the all-zero configuration: the flag is reported.
  Rig<MinimalPort> rig(AllZero());
  rig.port.state.chip.temperature_counts = tmp117_test::Counts(200);
  REQUIRE(rig.Start());
  rig.Wait(50);
  const auto s = rig.sensor.ReadStatus();
  CHECK(s.ok());
  CHECK(s.value.high_alert);
}

TEST_CASE("The traceability words of a stuck line are not returned as an identifier") {
  for (const Stuck pattern : {Stuck::Zeros, Stuck::Ones}) {
    Rig<MinimalPort> rig(Custom());
    REQUIRE(rig.Start());
    rig.port.state.stuck = pattern;
    CHECK(rig.sensor.ReadUniqueId().error != Error::Ok);
  }
  Rig<MinimalPort> rig(Custom());
  REQUIRE(rig.Start());
  CHECK(rig.sensor.ReadUniqueId().ok());
}

// Round 2: the switch-off of a driver that lost track of the chip never depends on a read (A21).

namespace {

/// A started rig whose chip was power-cycled behind the driver's back and then seen by a status
/// read: the driver is uninitialised, the chip converts continuously.
void LoseTrack(Rig<MinimalPort>& rig) {
  REQUIRE(rig.Start());
  rig.port.state.chip.PowerCycle();
  rig.Wait(3);
  CHECK_EQ(rig.sensor.ReadStatus().error, Error::UnexpectedReset);
  CHECK(!rig.sensor.IsInitialized());
}

} // namespace

TEST_CASE("regress-r2-1a: SetMode(Shutdown) after reset evidence is sent although reads fail") {
  Rig<MinimalPort> rig(Custom());
  LoseTrack(rig);
  rig.port.state.fail_reads = true;
  const int before = rig.Writes(reg::kConfiguration);
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  CHECK_EQ(rig.Writes(reg::kConfiguration), before + 1);
  CHECK_EQ(ModOf(rig), 1U);
  CHECK(!rig.port.state.chip.EunSet());
}

TEST_CASE("regress-r2-1b: Deinitialize after reset evidence is sent although reads fail") {
  Rig<MinimalPort> rig(Custom());
  LoseTrack(rig);
  rig.port.state.fail_reads = true;
  const int before = rig.Writes(reg::kConfiguration);
  CHECK(rig.sensor.Deinitialize().ok());
  CHECK_EQ(rig.Writes(reg::kConfiguration), before + 1);
  CHECK_EQ(ModOf(rig), 1U);
}

TEST_CASE("regress-r2-1c: switch-off after a Device_ID mismatch is sent when replies are stuck") {
  Rig<MinimalPort> rig(Custom());
  REQUIRE(rig.Start());
  rig.port.state.stuck = Stuck::Ones;
  CHECK(!rig.sensor.CheckHealth().ok());
  const int before = rig.Writes(reg::kConfiguration);
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  CHECK_EQ(rig.Writes(reg::kConfiguration), before + 1);
  CHECK_EQ(ModOf(rig), 1U);
}

TEST_CASE("regress-r2-1d: a failed read still gets the lock written before the switch-off") {
  Rig<MinimalPort> rig(Custom());
  LoseTrack(rig);
  rig.port.state.chip.ForceUnlock(); // EUN set, and the status read before it said nothing
  rig.port.state.fail_reads = true;  // the driver cannot see it
  const int ul_before = rig.Writes(reg::kEepromUnlock);
  CHECK(rig.sensor.Deinitialize().ok());
  CHECK(rig.Writes(reg::kEepromUnlock) > ul_before); // the relock write went out first
  CHECK(!rig.port.state.chip.EunSet());              // and it locked
  CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 0);
  CHECK_EQ(ModOf(rig), 1U);
}

TEST_CASE("A chip that reads EUN = 1 after the lock write is still refused (A21)") {
  Rig<MinimalPort> rig(Custom());
  LoseTrack(rig);
  rig.port.state.chip.eun_write_zero_locks = false;
  rig.port.state.chip.ForceUnlock();
  const int before = rig.Writes(reg::kConfiguration);
  CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Shutdown).error, Error::EepromVerifyFailed);
  CHECK_EQ(rig.sensor.Deinitialize().error, Error::EepromVerifyFailed);
  CHECK_EQ(rig.Writes(reg::kConfiguration), before);
  CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 0);
}

TEST_CASE("regress-r2-2: switch-off with the copy in shutdown and a write to continuous in doubt") {
  tmp117::Config c = Custom();
  c.mode = tmp117::Mode::Shutdown;
  Rig<MinimalPort> rig(c);
  REQUIRE(rig.Start());
  rig.port.state.ack_lost_at = rig.Transfers(); // the write takes effect, the ACK is lost
  CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Continuous).error, Error::Bus);
  rig.port.state.ack_lost_at = -1;
  CHECK_EQ(ModOf(rig), 0U); // the chip converts
  rig.port.state.fail_reads = true;
  const int before = rig.Writes(reg::kConfiguration);
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  CHECK_EQ(rig.Writes(reg::kConfiguration), before + 1); // the force flag: copy equals target
  CHECK_EQ(ModOf(rig), 1U);
}

TEST_CASE("A reserved bit set in the EEPROM_UL reply at start-up stops the start-up (A21)") {
  int ul_read = -1;
  {
    Rig<MinimalPort> probe(Custom());
    REQUIRE(probe.sensor.EnsureInitialized().ok());
    int reads = 0;
    for (std::size_t i = 0; i < probe.port.state.frame_count; ++i) {
      if (probe.port.state.frames[i].is_read) {
        if ((ul_read < 0) && (probe.port.state.frames[i].bytes[0] == reg::kEepromUnlock)) {
          ul_read = reads;
        }
        ++reads;
      }
    }
  }
  REQUIRE(ul_read >= 0);
  Rig<MinimalPort> rig(Custom());
  rig.port.state.flip_at = ul_read;
  rig.port.state.flip_mask = 0x0001U; // a reserved bit, EUN still reads 0
  CHECK_EQ(rig.sensor.EnsureInitialized().error, Error::BadReply);
  CHECK(!rig.sensor.IsInitialized());
  CHECK_EQ(rig.Writes(reg::kConfiguration), 0);
  CHECK_EQ(rig.Writes(reg::kHighLimit), 0);
}

TEST_CASE("A Configuration write that succeeds ends the doubt about a one-shot start") {
  // Lost ACK on the one-shot start: doubt. A shutdown write with the settle read failing is sent
  // and clears it; the next setter then needs no settle read.
  auto next_setter_transfers = [](bool with_doubt) -> int {
    Rig<MinimalPort> rig(Custom());
    CHECK(rig.Start());
    if (with_doubt) {
      rig.port.state.ack_lost_at = rig.Transfers() + 1; // the write after the pre-read
      CHECK_EQ(rig.sensor.StartOneShot().error, Error::Bus);
      rig.port.state.ack_lost_at = -1;
    }
    rig.port.state.fail_reads = true;
    CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
    rig.port.state.fail_reads = false;
    const int before = rig.Transfers();
    CHECK(rig.sensor.SetAveraging(tmp117::Averaging::X8).ok());
    return rig.Transfers() - before;
  };
  CHECK_EQ(next_setter_transfers(true), next_setter_transfers(false));
}

TEST_CASE("regress-b-3: a failed shutdown write while a one-shot runs is not taken for a reset") {
  // The shutdown write is NACKed and did not take effect; the one-shot still runs (MOD 11). The
  // settle must see "not taken", not reset evidence (finder probe T4).
  for (const bool mod_stays_11 : {false, true}) {
    Rig<MinimalPort> rig(Custom());
    rig.port.state.chip.oneshot_mod_reads_back_11 = mod_stays_11;
    REQUIRE(rig.Start());
    REQUIRE(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
    REQUIRE(rig.sensor.StartOneShot().ok());
    rig.port.state.fail_transfer = rig.Transfers(); // the shutdown write
    CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Shutdown).error, Error::Bus);
    rig.port.state.fail_transfer = -1;
    CHECK(rig.sensor.ReadStatus().ok());
    CHECK(rig.sensor.IsInitialized());
    CHECK(rig.sensor.WaitDataReady(2000).ok()); // the one-shot still completes
    CHECK(rig.sensor.IsInitialized());
  }
}

TEST_CASE("regress-r4-3: one flipped EUN bit in the first EEPROM_UL reply costs at most one word") {
  // The residual risk of A21 (ACCEPTED-RISK R4-3): a chip left unlocked and bit 15 of exactly the
  // first EEPROM_UL reply flipped to 0. The bus has no CRC, so the driver cannot see it; the cost
  // is bounded to one EEPROM word, at start-up and at switch-off alike.
  for (const bool at_start : {true, false}) {
    Rig<MinimalPort> rig(Custom());
    if (!at_start) {
      REQUIRE(rig.Start());
      rig.port.state.chip.PowerCycle();
      rig.Wait(3);
      CHECK_EQ(rig.sensor.ReadStatus().error, Error::UnexpectedReset);
    }
    rig.port.state.chip.ForceUnlock();
    int target = rig.port.state.reads;
    if (at_start) {
      Rig<MinimalPort> dry(Custom());
      dry.port.state.chip.ForceUnlock();
      (void)dry.sensor.EnsureInitialized();
      int reads = 0;
      target = -1;
      for (std::size_t i = 0; i < dry.port.state.frame_count; ++i) {
        const auto& f = dry.port.state.frames[i];
        if (f.is_read) {
          if ((target < 0) && (f.bytes[0] == reg::kEepromUnlock)) {
            target = reads;
          }
          ++reads;
        }
      }
    }
    REQUIRE(target >= 0);
    rig.port.state.flip_at = target;
    rig.port.state.flip_mask = 0x8000U;
    const int p0 = rig.port.state.chip.eeprom_words_programmed;
    (void)(at_start ? rig.sensor.EnsureInitialized() : rig.sensor.Deinitialize());
    CHECK(rig.port.state.chip.eeprom_words_programmed - p0 <= 1);
  }
}

TEST_CASE("regress-b-3: a reset while the one-shot runs is still a reset") {
  // The fix of B-3 accepts only MOD 11 with the copy's settings: a power cycle (MOD 00 with the
  // factory settings, here equal to the copy's) is reset evidence, and the switch-off that
  // settles it runs the lost-track rows (I3).
  Rig<MinimalPort> rig(tmp117::Config{});
  REQUIRE(rig.Start());
  REQUIRE(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  REQUIRE(rig.sensor.StartOneShot().ok());
  rig.port.state.fail_transfer = rig.Transfers();
  CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Shutdown).error, Error::Bus);
  rig.port.state.fail_transfer = -1;
  rig.port.state.chip.PowerCycle();
  rig.Wait(3);
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  CHECK(!rig.sensor.IsInitialized());
  CHECK_EQ(ModOf(rig), 1U);
}
