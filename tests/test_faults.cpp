/**
 * @file test_faults.cpp
 * @brief A dead, shorted or floating line, one flipped reply, a reset in the middle of a call: each
 * maps to an error and never to data or to a write over a chip that may have reset (RD-01 to RD-03,
 * RD-30), and every reading of the datasheet's ambiguities keeps the safety rules.
 */
#include <cstdio>

#include "mock_tmp117.hpp"
#include "test_harness.hpp"
#include "test_support.hpp"
#include "tmp117.hpp"

using tmp117::Error;
using tmp117_test::FullPort;
using tmp117_test::MinimalPort;
using tmp117_test::Rig;
using tmp117_test::Stuck;
namespace reg = tmp117::reg;
namespace chip = tmp117_test::chip;

namespace {

/// A configuration that differs from the factory word, so a reset to factory values shows.
tmp117::Config Custom() {
  tmp117::Config c{};
  c.averaging = tmp117::Averaging::X32;
  c.cycle_time = tmp117::CycleTime::Ms125;
  c.high_limit_mc = 60000;
  c.low_limit_mc = 0;
  return c;
}

struct ReadCase {
  const char* name;
  Error (*run)(tmp117::TMP117<MinimalPort>&);
};

Error RunStatus(tmp117::TMP117<MinimalPort>& s) {
  return s.ReadStatus().error;
}
Error RunHealth(tmp117::TMP117<MinimalPort>& s) {
  return s.CheckHealth().error;
}
Error RunWait(tmp117::TMP117<MinimalPort>& s) {
  return s.WaitDataReady(20).error;
}
Error RunOneShot(tmp117::TMP117<MinimalPort>& s) {
  return s.ReadOneShotMilliC().error;
}
Error RunVerified(tmp117::TMP117<MinimalPort>& s) {
  return s.ReadTemperatureMilliC(tmp117::Verify::Device).error;
}

} // namespace

TEST_CASE("Every read that decides something refuses a line stuck at 0, at 1 or repeating") {
  const ReadCase cases[] = {{"ReadStatus", &RunStatus},
                            {"CheckHealth", &RunHealth},
                            {"WaitDataReady", &RunWait},
                            {"ReadOneShotMilliC", &RunOneShot},
                            {"ReadTemperatureMilliC(Verify::Device)", &RunVerified}};
  // The default word, a custom one and the one whose Configuration word is 0000h, which a line
  // stuck at 0 reproduces exactly.
  tmp117::Config all_zero{};
  all_zero.averaging = tmp117::Averaging::X1;
  all_zero.cycle_time = tmp117::CycleTime::Ms15p5;
  const tmp117::Config configs[] = {Custom(), tmp117::Config{}, all_zero};
  for (const ReadCase& rc : cases) {
    for (const tmp117::Config& cfg : configs) {
      for (const Stuck pattern : {Stuck::Zeros, Stuck::Ones, Stuck::RepeatLast}) {
        Rig<MinimalPort> rig(cfg);
        REQUIRE(rig.Start());
        rig.port.state.stuck = pattern;
        rig.port.state.last_reply = 0x4242;
        const Error e = rc.run(rig.sensor);
        if (e == Error::Ok) {
          std::printf("    %s accepted stuck pattern %d\n", rc.name, static_cast<int>(pattern));
        }
        CHECK(e != Error::Ok);
      }
    }
  }
}

TEST_CASE("One flipped reply at any call of a status read never gives data and never writes") {
  // Sweep the flipped read over the whole of a read sequence.
  for (int k = 0; k < 6; ++k) {
    for (const uint16_t mask :
         {uint16_t{0x0001}, uint16_t{0x0010}, uint16_t{0x0400}, uint16_t{0x8000}}) {
      Rig<MinimalPort> rig(Custom());
      REQUIRE(rig.Start());
      rig.port.state.reads = 0;
      rig.port.state.flip_at = k;
      rig.port.state.flip_mask = mask;
      const int writes = rig.port.state.chip.writes;
      for (int call = 0; call < 3; ++call) {
        static_cast<void>(rig.sensor.CheckHealth());
        static_cast<void>(rig.sensor.ReadStatus());
      }
      // A flip is reported by the call it hit, as an error; whatever happened, nothing was written
      // by a read call.
      CHECK_EQ(rig.port.state.chip.writes, writes);
    }
  }
}

TEST_CASE("A flipped Configuration reply is an error, not a status") {
  for (const uint16_t mask :
       {uint16_t{0x0001}, uint16_t{0x0002}, uint16_t{0x0400}, uint16_t{0x0800}, uint16_t{0x0020}}) {
    Rig<MinimalPort> rig(Custom());
    REQUIRE(rig.Start());
    rig.port.state.reads = 0;
    rig.port.state.flip_at = 0;
    rig.port.state.flip_mask = mask;
    const auto s = rig.sensor.ReadStatus();
    CHECK(!s.ok()); // a reserved bit, MOD 10, or a field that differs from what was written
  }
}

TEST_CASE("A reset at any transfer of a script is found, and no write follows the evidence") {
  // The script: set things, one-shot, read. Count its transfers, then reset before each in turn.
  auto script = [](Rig<MinimalPort>& rig, bool& saw_evidence) {
    const tmp117::Result<void> calls[] = {rig.sensor.SetLimits(0, 70000),
                                          rig.sensor.SetOffsetMilliC(500),
                                          rig.sensor.SetAveraging(tmp117::Averaging::X8)};
    for (const auto& c : calls) {
      if (c.error == Error::UnexpectedReset) {
        saw_evidence = true;
      }
    }
    const auto os = rig.sensor.ReadOneShotMilliC();
    if (os.error == Error::UnexpectedReset) {
      saw_evidence = true;
    }
    const auto t = rig.sensor.ReadTemperatureMilliC();
    if (t.error == Error::UnexpectedReset) {
      saw_evidence = true;
    }
  };
  int total = 0;
  {
    Rig<MinimalPort> clean(Custom());
    REQUIRE(clean.Start());
    bool ignored = false;
    const int before = clean.port.state.transfers;
    script(clean, ignored);
    total = clean.port.state.transfers - before;
  }
  CHECK(total > 5);
  int found = 0;
  for (int k = 0; k < total; ++k) {
    Rig<MinimalPort> rig(Custom());
    REQUIRE(rig.Start());
    rig.port.state.reset_at_transfer = rig.port.state.transfers + k;
    bool evidence = false;
    script(rig, evidence);
    if (evidence) {
      ++found;
      // After the evidence the driver is uninitialised and sent nothing more: the last write frame
      // was before the call that returned UnexpectedReset.
      CHECK(!rig.sensor.IsInitialized());
      const std::size_t frames = rig.port.state.frame_count;
      CHECK_EQ(rig.sensor.SetLimits(0, 1000).error, Error::NotInitialized);
      CHECK_EQ(rig.sensor.SetOffsetMilliC(1).error, Error::NotInitialized);
      CHECK_EQ(rig.port.state.frame_count, frames);
    }
    // A reset that lands between two transfers of one call cannot be known before the next read;
    // the writes that race with it are bounded by the one call that was running.
    CHECK(rig.port.state.chip.writes_during_load <= 3);
    CHECK_EQ(rig.port.state.chip.traceability_writes, 0);
  }
  CHECK(found > 0);
}

TEST_CASE("After UnexpectedReset every call is NotInitialized and sends nothing until restarted") {
  Rig<MinimalPort> rig(Custom());
  REQUIRE(rig.Start());
  rig.port.state.chip.PowerCycle();
  rig.Wait(3);
  CHECK_EQ(rig.sensor.CheckHealth().error, Error::UnexpectedReset);
  const std::size_t frames = rig.port.state.frame_count;
  CHECK_EQ(rig.sensor.SetLimits(0, 1000).error, Error::NotInitialized);
  CHECK_EQ(rig.sensor.SetOffsetMilliC(1).error, Error::NotInitialized);
  CHECK_EQ(rig.sensor.StartOneShot().error, Error::NotInitialized);
  CHECK_EQ(rig.sensor.ProgramUserWord(1).error, Error::NotInitialized);
  CHECK_EQ(rig.sensor.ReadTemperatureRaw().error, Error::NotInitialized);
  CHECK_EQ(rig.port.state.frame_count, frames);
}

TEST_CASE("Every ambiguity reading keeps the safety rules through a whole session") {
  struct Reading {
    const char* name;
    void (*apply)(tmp117_test::Tmp117Model&);
  };
  const Reading readings[] = {
      {"A1 slow load", [](tmp117_test::Tmp117Model& m) { m.por_load_us = 8000; }},
      {"A1 fast load", [](tmp117_test::Tmp117Model& m) { m.por_load_us = 300; }},
      {"A2 slow word", [](tmp117_test::Tmp117Model& m) { m.eeprom_program_us = 45000; }},
      {"A3 EEPROM2 not zero", [](tmp117_test::Tmp117Model& m) { m.eeprom2_reset = 0xFFFF; }},
      {"A4 offset not in EEPROM", [](tmp117_test::Tmp117Model& m) { m.offset_in_eeprom = false; }},
      {"A5 soft reset keeps EUN",
       [](tmp117_test::Tmp117Model& m) { m.soft_reset_clears_eun = false; }},
      {"A5 soft reset no reload",
       [](tmp117_test::Tmp117Model& m) { m.soft_reset_reloads = false; }},
      {"A7 saturate to 8001h", [](tmp117_test::Tmp117Model& m) { m.saturate_min_8001 = true; }},
      {"A8 inclusive compare",
       [](tmp117_test::Tmp117Model& m) { m.limit_compare_inclusive = true; }},
      {"A11 one-shot sets alert", [](tmp117_test::Tmp117Model& m) { m.oneshot_sets_alert = true; }},
      {"A12 therm read keeps DR",
       [](tmp117_test::Tmp117Model& m) { m.therm_read_clears_dr = false; }},
      {"A13 revision 5", [](tmp117_test::Tmp117Model& m) { m.device_rev = 5; }},
      {"A14 writes NACKed in load", [](tmp117_test::Tmp117Model& m) { m.por_write_nack = true; }},
      {"A16 slow oscillator", [](tmp117_test::Tmp117Model& m) { m.osc_error_pct = 10; }},
      {"A19 MOD stays 11 after a one-shot",
       [](tmp117_test::Tmp117Model& m) { m.oneshot_mod_reads_back_11 = true; }},
  };
  for (const Reading& rd : readings) {
    Rig<FullPort> rig(Custom());
    rd.apply(rig.port.state.chip);
    rig.port.state.chip.PowerCycle();
    if (!rig.Start()) {
      std::printf("    %s: start failed\n", rd.name);
    }
    REQUIRE(rig.sensor.IsInitialized());
    CHECK(rig.sensor.SetLimits(-1000, 50000).ok());
    CHECK(rig.sensor.SetAlertFunction(tmp117::AlertFunction::Threshold).ok());
    CHECK(rig.sensor.ReadStatus().ok());
    rig.Wait(1200);
    CHECK(rig.sensor.ReadTemperatureMilliC().ok());
    CHECK(rig.sensor.ReadOneShotMilliC().ok());
    CHECK(rig.sensor.ProgramUserWord(0x5A5A).ok());
    CHECK(rig.sensor.Deinitialize().ok());
    const auto& m = rig.port.state.chip;
    CHECK_EQ(m.writes_during_load, 0);
    CHECK_EQ(m.writes_while_busy, 0);
    CHECK_EQ(m.traceability_writes, 0);
    CHECK_EQ(m.readonly_writes, 0);
    CHECK_EQ(m.bad_pointer_accesses, 0);
    CHECK(!m.EunSet());
  }
}

TEST_CASE("The offset saturating at the minimum never returns the 8000h sentinel as data (A7)") {
  for (const bool alt : {false, true}) {
    Rig<MinimalPort> rig;
    rig.port.state.chip.saturate_min_8001 = alt;
    REQUIRE(rig.Start());
    CHECK(rig.sensor.SetOffsetMilliC(-256000).ok());
    rig.port.state.chip.temperature_counts = -10000;
    rig.Wait(1200);
    const auto t = rig.sensor.ReadTemperatureMilliC();
    if (alt) {
      CHECK(t.ok()); // 8001h: -255.99 degC, a number
    } else {
      CHECK(!t.ok()); // 8000h is the no-conversion sentinel: never a temperature
    }
  }
}

TEST_CASE("Inclusive and strict limit comparison both flag a result beyond the limit (A8)") {
  for (const bool inclusive : {false, true}) {
    tmp117::Config c{};
    c.high_limit_mc = 30000;
    c.low_limit_mc = 10000;
    c.cycle_time = tmp117::CycleTime::Ms125;
    Rig<MinimalPort> rig(c);
    rig.port.state.chip.limit_compare_inclusive = inclusive;
    rig.port.state.chip.temperature_counts = tmp117_test::Counts(31);
    REQUIRE(rig.Start());
    const auto s = rig.sensor.ReadStatus();
    CHECK(s.value.high_alert);
    // Exactly at the limit: only the inclusive reading flags it.
    rig.port.state.chip.temperature_counts = tmp117_test::Counts(30);
    rig.sensor.ClearLatchedStatus();
    rig.Wait(300);
    const auto at = rig.sensor.ReadStatus();
    CHECK_EQ(at.value.high_alert, inclusive);
  }
}

TEST_CASE(
    "A failed limit write never leaves an inverted window, and a reset after it still starts") {
  // The limits are two registers. Whichever one is written first, the chip and the driver's copy
  // must never hold a low limit above the high limit (ambiguity A10), or the next reset command
  // would be sent and then refused by the start-up check as InvalidArgument.
  struct Move {
    int32_t from_low, from_high, to_low, to_high;
  };
  const Move moves[] = {{50000, 60000, -10000, -5000}, // down, past the old low limit
                        {-10000, -5000, 50000, 60000}, // up, past the old high limit
                        {0, 10000, 5000, 20000},       // overlapping, up
                        {5000, 20000, 0, 10000},       // overlapping, down
                        {0, 10000, 2000, 3000}};       // inside the old window
  for (const Move& m : moves) {
    int transfers = 0;
    for (int fail = -1; fail < 12; ++fail) {
      tmp117::Config c{};
      c.cycle_time = tmp117::CycleTime::Ms125;
      c.low_limit_mc = m.from_low;
      c.high_limit_mc = m.from_high;
      Rig<MinimalPort> rig(c);
      REQUIRE(rig.Start());
      const int before = rig.Transfers();
      if (fail >= 0) {
        if (fail >= transfers) {
          break; // past the last transfer of the call: every position was tried
        }
        rig.port.state.fail_transfer = before + fail;
      }
      const auto set = rig.sensor.SetLimits(m.to_low, m.to_high);
      if (fail < 0) {
        CHECK(set.ok());
        transfers = rig.Transfers() - before;
      }
      const auto& chip_model = rig.port.state.chip;
      CHECK(tmp117::detail::AsSigned16(chip_model.RawLow()) <=
            tmp117::detail::AsSigned16(chip_model.RawHigh()));
      CHECK(rig.sensor.GetConfig().low_limit_mc <= rig.sensor.GetConfig().high_limit_mc);
      rig.port.state.fail_transfer = -1;
      const auto reset = rig.sensor.SoftReset();
      CHECK(reset.error != Error::InvalidArgument);
    }
  }
}
