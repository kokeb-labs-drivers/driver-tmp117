/**
 * @file test_eeprom.cpp
 * @brief EEPROM programming (section 7.5.1, p. 18): unlock, per-word programming, the lock on every
 * path, the reset, the read-back, and the words that are never written.
 */
#include "mock_tmp117.hpp"
#include "test_harness.hpp"
#include "test_support.hpp"
#include "tmp117.hpp"

using tmp117::Error;
using tmp117_test::MinimalPort;
using tmp117_test::Rig;
namespace reg = tmp117::reg;
namespace chip = tmp117_test::chip;

namespace {

/// The defaults the tests program: a different limit window, 4 s cycle, therm mode.
tmp117::Config Defaults() {
  tmp117::Config c{};
  c.cycle_time = tmp117::CycleTime::S4;
  c.alert_function = tmp117::AlertFunction::Threshold;
  c.low_limit_mc = 5000;
  c.high_limit_mc = 40000;
  c.offset_mc = -500;
  return c;
}

/// The invariants that hold whatever the reading of the datasheet or the fault.
void CheckSafe(const tmp117_test::Tmp117Model& m, int busy_lock_attempts = 0) {
  CHECK_EQ(m.writes_during_load, 0);
  // Only the lock itself is ever tried on a chip whose programming does not end (at most three
  // attempts, kLockAttempts): everything else waits for EEPROM_Busy.
  CHECK(m.writes_while_busy <= busy_lock_attempts);
  CHECK_EQ(m.traceability_writes, 0);
  CHECK_EQ(m.readonly_writes, 0);
  CHECK_EQ(m.bad_pointer_accesses, 0);
}

} // namespace

TEST_CASE(
    "ProgramPowerOnDefaults stores the words, locks, resets and restores the running config") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const auto defaults = Defaults();
  const auto r = rig.sensor.ProgramPowerOnDefaults(defaults);
  REQUIRE(r.ok());
  const auto& m = rig.port.state.chip;
  CHECK_EQ(m.EepromHigh(), 40000 * 128 / 1000);
  CHECK_EQ(m.EepromLow(), 5000 * 128 / 1000);
  CHECK_EQ(tmp117::detail::AsSigned16(m.EepromOffset()), -64);
  CHECK_EQ(m.EepromConfig(), (uint32_t{chip::kConfigurationReset} & ~uint32_t{chip::kConvMask}) |
                                 0x0280U | uint32_t{chip::kTherm});
  CHECK_EQ(m.eeprom_words_programmed, 4);
  CHECK(!m.EunSet());         // locked again
  CHECK_EQ(m.soft_resets, 1); // the default reset is the soft reset
  CHECK_EQ(m.general_call_resets, 0);
  CHECK_EQ(m.Eeprom1(), 0x1A2B); // traceability words untouched
  CHECK_EQ(m.Eeprom3(), 0x3C4D);
  CHECK(rig.sensor.IsInitialized());
  // The running configuration is back, not the freshly programmed defaults.
  CHECK_EQ(m.RawHigh(), 192000 * 128 / 1000);
  CHECK_EQ(m.RawConfig(), chip::kConfigurationReset);
  CheckSafe(m);
  // A later reset loads what was programmed.
  rig.port.state.chip.PowerCycle();
  CHECK_EQ(rig.port.state.chip.RawHigh(), 40000 * 128 / 1000);
}

TEST_CASE("The programming sequence is shutdown, unlock, words, lock, reset (Figure 7-7)") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  const std::size_t from = rig.port.state.frame_count;
  REQUIRE(rig.sensor.ProgramPowerOnDefaults(Defaults()).ok());
  // Collect the write frames of the operation in order.
  uint8_t order[32]{};
  uint16_t words[32]{};
  std::size_t n = 0;
  for (std::size_t i = from; i < rig.port.state.frame_count && n < 32; ++i) {
    const auto& f = rig.port.state.frames[i];
    if (!f.is_read && f.length == 3) {
      order[n] = f.bytes[0];
      words[n] = static_cast<uint16_t>((static_cast<uint32_t>(f.bytes[1]) << 8U) | f.bytes[2]);
      ++n;
    }
  }
  REQUIRE(n >= 9);
  CHECK_EQ(order[0], reg::kConfiguration); // shutdown first: no conversions while unlocked
  CHECK_EQ(words[0] & chip::kModMask, 0x0400);
  CHECK_EQ(order[1], reg::kEepromUnlock);
  CHECK_EQ(words[1], chip::kEun);
  CHECK_EQ(order[2], reg::kHighLimit);
  CHECK_EQ(order[3], reg::kLowLimit);
  CHECK_EQ(order[4], reg::kTempOffset);
  CHECK_EQ(order[5], reg::kConfiguration);
  CHECK_EQ(order[6], reg::kEepromUnlock); // the lock
  CHECK_EQ(words[6], 0);
  CHECK_EQ(order[7], reg::kConfiguration); // the soft reset, only after EUN read 0
  CHECK_EQ(words[7], chip::kSoftReset);
}

TEST_CASE("ProgramUserWord stores EEPROM2 and nothing else") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.sensor.ProgramUserWord(0xCAFE).ok());
  const auto& m = rig.port.state.chip;
  CHECK_EQ(m.EepromUser(), 0xCAFE);
  CHECK_EQ(m.eeprom_words_programmed, 1);
  CHECK_EQ(m.EepromHigh(), chip::kHighLimitReset);
  CHECK(!m.EunSet());
  const auto id = rig.sensor.ReadUniqueId();
  CHECK_EQ(id.value.eeprom2, 0xCAFE);
  CheckSafe(m);
}

TEST_CASE("Programming never writes the traceability words and never leaves EUN set") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  REQUIRE(rig.sensor.ProgramPowerOnDefaults(Defaults()).ok());
  REQUIRE(rig.sensor.ProgramUserWord(7).ok());
  CHECK_EQ(rig.Writes(reg::kEeprom1), 0);
  CHECK_EQ(rig.Writes(reg::kEeprom3), 0);
  CHECK_EQ(rig.Writes(reg::kDeviceId), 0);
  CHECK_EQ(rig.Writes(reg::kTempResult), 0);
  CHECK(!rig.port.state.chip.EunSet());
}

TEST_CASE("A refusal sends nothing: argument error, one-shot pending, not initialised") {
  Rig<MinimalPort> rig;
  tmp117::Config bad = Defaults();
  bad.low_limit_mc = 50000; // above the high limit
  CHECK_EQ(rig.sensor.ProgramPowerOnDefaults(Defaults()).error, Error::NotInitialized);
  REQUIRE(rig.Start());
  const int before = rig.port.state.transfers;
  CHECK_EQ(rig.sensor.ProgramPowerOnDefaults(bad).error, Error::InvalidArgument);
  CHECK_EQ(rig.port.state.transfers, before);
  CHECK(!rig.port.state.chip.EunSet());
  CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 0);
}

TEST_CASE("The lock is written on every failure path: a NACK on any transfer of the operation") {
  // Count the transfers of a clean run, then fail each one in turn (a NACK that is not applied).
  int total = 0;
  {
    Rig<MinimalPort> clean;
    REQUIRE(clean.Start());
    const int before = clean.port.state.transfers;
    REQUIRE(clean.sensor.ProgramPowerOnDefaults(Defaults()).ok());
    total = clean.port.state.transfers - before;
  }
  CHECK(total > 10);
  int refused = 0;
  for (int i = 0; i < total; ++i) {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.port.state.fail_transfer = rig.port.state.transfers + i;
    const auto r = rig.sensor.ProgramPowerOnDefaults(Defaults());
    // The chip is never left unlocked, whichever transfer failed.
    CHECK(!rig.port.state.chip.EunSet());
    CheckSafe(rig.port.state.chip);
    if (!r.ok()) {
      ++refused;
      if (rig.Writes(reg::kEepromUnlock) > 0) {
        CHECK(!rig.sensor.IsInitialized()); // the application starts the driver again
      }
    }
    // And the soft reset was never written to Configuration while EUN could be set: no EEPROM
    // word holds the soft-reset bit.
    CHECK_EQ(rig.port.state.chip.last_eeprom_cfg_programmed & chip::kSoftReset, 0);
  }
  CHECK(refused > 0);
}

TEST_CASE("A programming that never ends is a Timeout, and the lock is still attempted") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.chip.eeprom_stuck_busy = true;
  const Error e = rig.sensor.ProgramPowerOnDefaults(Defaults()).error;
  CHECK_EQ(e, Error::Timeout);
  CheckSafe(rig.port.state.chip, 3);          // only the lock is tried on a busy chip
  CHECK(rig.Writes(reg::kEepromUnlock) >= 2); // the unlock and at least one lock attempt
  CHECK(!rig.sensor.IsInitialized());
}

TEST_CASE("A slow word within the deadline is waited for (ambiguity A2)") {
  for (const uint32_t us : {2000U, 7000U, 30000U, 55000U}) {
    Rig<MinimalPort> rig;
    rig.port.state.chip.eeprom_program_us = us;
    REQUIRE(rig.Start());
    CHECK(rig.sensor.ProgramPowerOnDefaults(Defaults()).ok());
    CheckSafe(rig.port.state.chip);
  }
  Rig<MinimalPort> slow; // 200 ms against 7 ms + 50 ms
  slow.port.state.chip.eeprom_program_us = 200000;
  REQUIRE(slow.Start());
  CHECK_EQ(slow.sensor.ProgramPowerOnDefaults(Defaults()).error, Error::Timeout);
  CHECK(!slow.port.state.chip.EunSet());
  CheckSafe(slow.port.state.chip, 3);
}

TEST_CASE("Reading EUN back wrong stops the operation and locks") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.stuck = tmp117_test::Stuck::Zeros; // EUN reads 0 right after it was set
  const Error e = rig.sensor.ProgramPowerOnDefaults(Defaults()).error;
  CHECK(e != Error::Ok);
  CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 0);
  CHECK(!rig.port.state.chip.EunSet());
}

TEST_CASE("A reload that differs from what was written is EepromVerifyFailed") {
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.chip.reload_flips_high_limit = true;
  CHECK_EQ(rig.sensor.ProgramPowerOnDefaults(Defaults()).error, Error::EepromVerifyFailed);
  CHECK(!rig.sensor.IsInitialized());
  CHECK(!rig.port.state.chip.EunSet());
}

TEST_CASE("Both readings of the lock: writing EUN = 0 locks, or only a reset does (ambiguity A6)") {
  {
    Rig<MinimalPort>
        rig; // the write of 0 does not lock; no opt-in: refuse and never write the reset
    rig.port.state.chip.eun_write_zero_locks = false;
    REQUIRE(rig.Start());
    const Error e = rig.sensor.ProgramPowerOnDefaults(Defaults()).error;
    CHECK_EQ(e, Error::EepromVerifyFailed);
    CHECK_EQ(rig.port.state.chip.soft_resets, 0);
    CHECK_EQ(rig.port.state.chip.last_eeprom_cfg_programmed & chip::kSoftReset, 0);
    CHECK(rig.port.state.chip.EunSet()); // only a power cycle locks it: the error says so
    CHECK(!rig.sensor.IsInitialized());
  }
  {
    tmp117::Config c{};
    c.allow_general_call = true; // the opt-in allows the general-call reset to lock
    Rig<MinimalPort> rig(c);
    rig.port.state.chip.eun_write_zero_locks = false;
    REQUIRE(rig.Start());
    CHECK(rig.sensor.ProgramPowerOnDefaults(Defaults()).ok());
    CHECK_EQ(rig.port.state.chip.general_call_resets, 1);
    CHECK(!rig.port.state.chip.EunSet());
    CheckSafe(rig.port.state.chip);
  }
}

TEST_CASE("Both readings of the soft reset: it clears EUN and reloads, or it does not (A5)") {
  for (const bool clears : {true, false}) {
    for (const bool reloads : {true, false}) {
      Rig<MinimalPort> rig;
      rig.port.state.chip.soft_reset_clears_eun = clears;
      rig.port.state.chip.soft_reset_reloads = reloads;
      REQUIRE(rig.Start());
      const auto r = rig.sensor.ProgramPowerOnDefaults(Defaults());
      CheckSafe(rig.port.state.chip);
      CHECK(!rig.port.state.chip.EunSet());
      // Without a reload the read-back sees the registers just written, so it cannot prove the
      // EEPROM holds them: the bench step of ambiguity A5 settles which reading is true.
      CHECK(r.ok());
    }
  }
}

TEST_CASE("Programming under every other reading of the datasheet stays safe") {
  struct Knobs {
    uint32_t por_load_us;
    bool por_write_nack;
    bool offset_in_eeprom;
    uint32_t osc;
  };
  for (const Knobs k : {Knobs{500, false, true, 0}, Knobs{9000, true, true, 0},
                        Knobs{1500, false, false, 10}, Knobs{4000, true, false, 5}}) {
    Rig<MinimalPort> rig;
    rig.port.state.chip.por_load_us = k.por_load_us;
    rig.port.state.chip.por_write_nack = k.por_write_nack;
    rig.port.state.chip.offset_in_eeprom = k.offset_in_eeprom;
    rig.port.state.chip.osc_error_pct = k.osc;
    rig.port.state.chip.PowerCycle();
    REQUIRE(rig.sensor.EnsureInitialized().ok());
    const auto p = rig.sensor.ProgramUserWord(0x1234);
    CHECK(p.ok() || p.error == Error::EepromVerifyFailed);
    CheckSafe(rig.port.state.chip);
    CHECK(!rig.port.state.chip.EunSet());
  }
}

TEST_CASE("A failed shutdown or unlock write stops the programming before any word is written") {
  // The transfers up to and including the first write of EUN = 1 are the shutdown and the unlock.
  int until_unlock = 0;
  {
    Rig<MinimalPort> clean;
    REQUIRE(clean.Start());
    const std::size_t from = clean.port.state.frame_count;
    REQUIRE(clean.sensor.ProgramPowerOnDefaults(Defaults()).ok());
    for (std::size_t n = from; n < clean.port.state.frame_count; ++n) {
      const auto& f = clean.port.state.frames[n];
      if (!f.is_read && f.bytes[0] == reg::kEepromUnlock && f.bytes[1] == 0x80) {
        until_unlock = static_cast<int>(n - from) + 1;
        break;
      }
    }
  }
  REQUIRE(until_unlock > 0);
  int refused = 0;
  for (int i = 0; i < until_unlock; ++i) {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.port.state.fail_transfer = rig.port.state.transfers + i;
    const Error e = rig.sensor.ProgramPowerOnDefaults(Defaults()).error;
    CHECK_EQ(e, Error::Bus);
    refused += (e == Error::Bus) ? 1 : 0;
    CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 0);
    CHECK(!rig.port.state.chip.EunSet());
  }
  CHECK_EQ(refused, until_unlock);
}

TEST_CASE("Bits 13:0 of EEPROM_UL reading as 1 are a BadReply, and the chip is locked") {
  int bad_replies = 0;
  for (int i = 0; i < 12; ++i) {
    Rig<MinimalPort> rig;
    REQUIRE(rig.Start());
    rig.port.state.flip_at = rig.port.state.reads + i;
    rig.port.state.flip_mask = 0x0008;
    const Error e = rig.sensor.ProgramPowerOnDefaults(Defaults()).error;
    bad_replies += (e == Error::BadReply) ? 1 : 0;
    CHECK(!rig.port.state.chip.EunSet());
    CheckSafe(rig.port.state.chip);
  }
  CHECK(bad_replies > 0);
}

TEST_CASE("A failed lock write is reported as the bus error, not as a verify failure (A6)") {
  const auto find_relock = []() -> int {
    Rig<MinimalPort> clean;
    if (!clean.Start()) {
      return -1;
    }
    clean.port.state.chip.eun_write_zero_locks = false;
    const std::size_t from = clean.port.state.frame_count;
    (void)clean.sensor.ProgramPowerOnDefaults(Defaults());
    int index = 0;
    for (std::size_t n = from; n < clean.port.state.frame_count; ++n, ++index) {
      const auto& f = clean.port.state.frames[n];
      if (!f.is_read && f.bytes[0] == reg::kEepromUnlock && f.bytes[1] == 0 && f.bytes[2] == 0) {
        return index;
      }
    }
    return -1;
  };
  const int at = find_relock();
  REQUIRE(at >= 0);
  Rig<MinimalPort> rig;
  REQUIRE(rig.Start());
  rig.port.state.chip.eun_write_zero_locks = false;
  rig.port.state.fail_transfer = rig.port.state.transfers + at;
  CHECK_EQ(rig.sensor.ProgramPowerOnDefaults(Defaults()).error, Error::Bus);
}
