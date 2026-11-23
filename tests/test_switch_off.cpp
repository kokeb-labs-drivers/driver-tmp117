/**
 * @file test_switch_off.cpp
 * @brief The switch-off decision table of docs/contract.md, one test per row, and the probes of
 * the third to fifth review rounds as regression tests.
 *
 * Rows: N1 and N2 (never reached the chip, another device), I1 to I3 (initialised), U1 to U6 (the
 * driver lost track of the chip), W1 and W2 (a busy EEPROM), R1 (a reset command that failed).
 * Each test asserts the exact transfers of the call and its status. The sweep at the end fails
 * every transfer of the call in turn on five chips (locked, unlocked, unlocked and unlockable, a
 * word in flight, the power-up load) and checks the table's promises and bounds on each.
 */
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "mock_tmp117.hpp"
#include "test_harness.hpp"
#include "test_support.hpp"
#include "tmp117.hpp"

using tmp117::Error;
using tmp117_test::MinimalPort;
using tmp117_test::MockBusState;
using tmp117_test::MockPortBase;
using tmp117_test::Rig;
using tmp117_test::Stuck;
namespace reg = tmp117::reg;
namespace chip = tmp117_test::chip;

namespace {

/// A port whose reads all fail from transfer index `fail_reads_from` on (-1: never), and which
/// fails transfer `fail_base + i` for every bit i set in `fail_mask`.
class SwitchPort : public MockPortBase<SwitchPort> {
public:
  uint64_t delayed_us = 0;
  int fail_reads_from = -1;
  int fail_base = 0;
  uint32_t fail_mask = 0U;

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    pick();
    return state.DoWrite(addr, data, len);
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    if ((fail_reads_from >= 0) && (state.transfers >= fail_reads_from)) {
      state.fail_reads = true;
    }
    pick();
    return state.DoWriteRead(addr, tx, tx_len, rx, rx_len);
  }

private:
  void pick() noexcept {
    const int i = state.transfers - fail_base;
    if ((fail_mask != 0U) && (i >= 0) && (i < 32) &&
        (((fail_mask >> static_cast<uint32_t>(i)) & 1U) != 0U)) {
      state.fail_transfer = state.transfers;
    }
  }
};

/// One expected transfer: a read or a write, and the pointer.
struct Xfer {
  bool read;
  uint8_t pointer;
};
constexpr Xfer Rd(uint8_t pointer) {
  return Xfer{true, pointer};
}
constexpr Xfer Wr(uint8_t pointer) {
  return Xfer{false, pointer};
}
constexpr uint8_t kUl = reg::kEepromUnlock;
constexpr uint8_t kCfg = reg::kConfiguration;
constexpr uint8_t kId = reg::kDeviceId;

/// The transfers recorded from frame `from` on are exactly `want`.
bool Transfers(const MockBusState& s, std::size_t from, std::initializer_list<Xfer> want) {
  if ((s.frame_count - from) != want.size()) {
    return false;
  }
  std::size_t i = from;
  for (const Xfer& x : want) {
    const auto& f = s.frames[i];
    if ((f.is_read != x.read) || (f.bytes[0] != x.pointer)) {
      return false;
    }
    ++i;
  }
  return true;
}

/// The last frame is a Configuration write with MOD = 01 (shutdown).
bool LastIsShutdown(const MockBusState& s) {
  if (s.frame_count == 0U) {
    return false;
  }
  const auto& f = s.frames[s.frame_count - 1U];
  const uint32_t word =
      (static_cast<uint32_t>(f.bytes[1]) << 8U) | static_cast<uint32_t>(f.bytes[2]);
  return !f.is_read && (f.length == 3U) && (f.bytes[0] == kCfg) && (((word >> 10U) & 3U) == 1U);
}

tmp117::Config Custom() {
  tmp117::Config c{};
  c.averaging = tmp117::Averaging::X32;
  c.cycle_time = tmp117::CycleTime::Ms125;
  c.high_limit_mc = 60000;
  c.low_limit_mc = 0;
  return c;
}

template <typename R>
uint32_t ModOf(const R& rig) {
  return (static_cast<uint32_t>(rig.port.state.chip.RawConfig()) & chip::kModMask) >> 10U;
}

/// Start, then a power cycle the driver sees: uninitialised, with the chip touched.
template <typename R>
void LoseTrack(R& rig) {
  REQUIRE(rig.Start());
  rig.port.state.chip.PowerCycle();
  rig.Wait(3);
  CHECK_EQ(rig.sensor.ReadStatus().error, Error::UnexpectedReset);
  CHECK(!rig.sensor.IsInitialized());
}

/// Start, then a power cycle the driver sees at once, inside the power-up load (no time passes).
template <typename R>
void LoseTrackInLoad(R& rig) {
  REQUIRE(rig.Start());
  rig.port.state.chip.PowerCycle();
  CHECK_EQ(rig.sensor.ReadStatus().error, Error::UnexpectedReset); // EEPROM_Busy set
  CHECK(!rig.sensor.IsInitialized());
  CHECK(rig.port.state.chip.Loading());
}

/// The chip unlocked, and (when `lockable` is false) deaf to a write of EUN = 0 (A6).
template <typename R>
void Unlock(R& rig, bool lockable) {
  rig.port.state.chip.eun_write_zero_locks = lockable;
  rig.port.state.chip.ForceUnlock();
}

/// Switch-off by SetMode (true) or Deinitialize (false).
template <typename R>
tmp117::Result<void> SwitchOff(R& rig, bool by_set_mode) {
  return by_set_mode ? rig.sensor.SetMode(tmp117::Mode::Shutdown) : rig.sensor.Deinitialize();
}

} // namespace

// -------------------------------------------------------------------------------------------------
// Rows N1, I1, I2
// -------------------------------------------------------------------------------------------------

TEST_CASE("Switch-off row N1: a driver that never started sends nothing") {
  Rig<MinimalPort> rig(Custom());
  CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Shutdown).error, Error::NotInitialized);
  CHECK(rig.sensor.Deinitialize().ok());
  CHECK_EQ(rig.Transfers(), 0);
}

TEST_CASE("Switch-off row I1: SetMode on an initialised driver sends with the settle failing") {
  Rig<MinimalPort> rig(Custom());
  REQUIRE(rig.Start());
  rig.port.state.fail_transfer = rig.Transfers(); // a limit write in doubt
  CHECK_EQ(rig.sensor.SetLimits(1000, 50000).error, Error::Bus);
  rig.port.state.fail_transfer = -1;
  rig.port.state.fail_reads = true;
  const std::size_t from = rig.port.state.frame_count;
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  CHECK(Transfers(rig.port.state, from, {Rd(reg::kHighLimit), Wr(kCfg)}));
  CHECK(LastIsShutdown(rig.port.state));
  CHECK(rig.sensor.IsInitialized());
  CHECK(rig.sensor.GetConfig().mode == tmp117::Mode::Shutdown);
}

TEST_CASE("Switch-off row I2: Deinitialize on an initialised driver is one write, no read") {
  Rig<MinimalPort> rig(Custom());
  REQUIRE(rig.Start());
  const std::size_t from = rig.port.state.frame_count;
  CHECK(rig.sensor.Deinitialize().ok());
  CHECK(Transfers(rig.port.state, from, {Wr(kCfg)}));
  CHECK(LastIsShutdown(rig.port.state));
  CHECK(!rig.sensor.IsInitialized());
}

TEST_CASE("Switch-off row I3: a settle that finds a reset hands over to the lost-track rows") {
  // A brown-out NACKs a limit write; SetMode(Shutdown) follows at once. The settle reads the limit,
  // sees the EEPROM default (reset evidence) and the driver lost track: W1 waits out the load.
  for (const bool nack : {false, true}) {
    Rig<SwitchPort> rig(Custom());
    rig.port.state.chip.por_write_nack = nack;
    REQUIRE(rig.Start());
    rig.port.state.fail_transfer = rig.Transfers();
    CHECK_EQ(rig.sensor.SetLimits(1000, 50000).error, Error::Bus);
    rig.port.state.fail_transfer = -1;
    rig.port.state.chip.PowerCycle();
    const int during_load = rig.port.state.chip.writes_during_load;
    const std::size_t from = rig.port.state.frame_count;
    CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
    CHECK(Transfers(rig.port.state, from, {Rd(reg::kHighLimit), Rd(kUl), Rd(kUl), Wr(kCfg)}));
    CHECK(!rig.sensor.IsInitialized());
    CHECK_EQ(rig.port.state.chip.writes_during_load, during_load);
    rig.Wait(30);
    CHECK_EQ(ModOf(rig), 1U);
  }
}

namespace {

/// Which write is in doubt when the brown-out comes (all on the factory defaults, which equal the
/// chip's reset values, so a settle that compares only the setting bits sees "nothing happened").
enum class Doubt : uint8_t { SetModeShutdown, SetAveragingX32, OneShotStart };

/// The write of `doubt` fails, the chip resets inside its power-up load, and the application
/// switches off at once (the retry the API guide recommends after `Bus`).
void SettleSeesLoad(bool nack, Doubt doubt) {
  Rig<SwitchPort> rig{tmp117::Config{}};
  rig.port.state.chip.por_write_nack = nack;
  REQUIRE(rig.Start());
  rig.port.state.fail_transfer = rig.Transfers();
  if (doubt == Doubt::OneShotStart) {
    // StartOneShot reads Configuration first; the failing transfer is its write.
    rig.port.state.fail_transfer = rig.Transfers() + 1;
  }
  tmp117::Result<void> first = tmp117::Result<void>::Success();
  if (doubt == Doubt::SetModeShutdown) {
    first = rig.sensor.SetMode(tmp117::Mode::Shutdown);
  } else if (doubt == Doubt::SetAveragingX32) {
    first = rig.sensor.SetAveraging(tmp117::Averaging::X32);
  } else {
    first = rig.sensor.StartOneShot();
  }
  CHECK_EQ(first.error, Error::Bus);
  rig.port.state.fail_transfer = -1;
  rig.port.state.chip.PowerCycle();
  CHECK(rig.port.state.chip.Loading());
  const int during_load = rig.port.state.chip.writes_during_load;
  const std::size_t from = rig.port.state.frame_count;
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  // The settle read shows EEPROM_Busy: reset evidence, row I3, then W1 waits out the load.
  CHECK(Transfers(rig.port.state, from, {Rd(kCfg), Rd(kUl), Rd(kUl), Wr(kCfg)}));
  CHECK(!rig.sensor.IsInitialized());
  CHECK_EQ(rig.port.state.chip.writes_during_load, during_load);
  rig.Wait(30);
  CHECK_EQ(ModOf(rig), 1U);
}

} // namespace

TEST_CASE(
    "Switch-off row I3: a settle read that shows EEPROM_Busy is reset evidence (PROBE-R5-1)") {
  // Both A14 readings (a write during the load ignored with an ACK, or NACKed), a failed
  // SetMode(Shutdown), a failed SetAveraging(X32), a one-shot start in doubt. Only SetMode settles
  // (row I2: Deinitialize sends one write, no read).
  for (const bool nack : {false, true}) {
    SettleSeesLoad(nack, Doubt::SetModeShutdown);
    SettleSeesLoad(nack, Doubt::SetAveragingX32);
    SettleSeesLoad(nack, Doubt::OneShotStart);
  }
}

TEST_CASE("Switch-off row I3: a lost ACK, a power cycle, then SetMode(Shutdown) (D-2)") {
  // SetAveraging(X1) on the defaults: the write took effect and its ACK was lost. The reset then
  // brings back the values of the old copy, so the setting bits agree with it; only EEPROM_Busy
  // shows the reset.
  int doubtful = 0;
  for (int k = 0; k < 4; ++k) {
    Rig<SwitchPort> rig{tmp117::Config{}};
    REQUIRE(rig.Start());
    rig.port.state.ack_lost_at = rig.Transfers() + k;
    const auto r = rig.sensor.SetAveraging(tmp117::Averaging::X1);
    rig.port.state.ack_lost_at = -1;
    if (r.ok() || (r.error != Error::Bus)) {
      continue;
    }
    ++doubtful;
    rig.port.state.chip.PowerCycle();
    const int during_load = rig.port.state.chip.writes_during_load;
    CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
    CHECK_EQ(rig.port.state.chip.writes_during_load, during_load);
    CHECK(!rig.sensor.IsInitialized());
    rig.Wait(30);
    CHECK_EQ(ModOf(rig), 1U);
  }
  CHECK(doubtful >= 1);
}

TEST_CASE("A settle that reads MOD 11 with no one-shot pending is reset evidence (R5-3, B-3)") {
  // A Configuration write is in doubt and no one-shot was started. The chip reads the copy's
  // settings with MOD = 11: neither the old value, the attempted one, nor a one-shot of this
  // driver (only a reset or another master can do that), so the settle must not call it "the
  // write did not take effect".
  Rig<SwitchPort> rig{tmp117::Config{}};
  REQUIRE(rig.Start());
  rig.port.state.fail_transfer = rig.Transfers();
  CHECK_EQ(rig.sensor.SetAveraging(tmp117::Averaging::X32).error, Error::Bus);
  rig.port.state.fail_transfer = -1;
  const uint16_t copy = rig.port.state.chip.RawConfig();
  rig.port.state.chip.Write(chip::kConfiguration,
                            static_cast<uint16_t>(copy | static_cast<uint16_t>(chip::kModMask)));
  CHECK_EQ((static_cast<uint32_t>(rig.port.state.chip.RawConfig()) & chip::kModMask) >> 10U, 3U);
  CHECK_EQ(rig.sensor.SetCycleTime(tmp117::CycleTime::S4).error, Error::UnexpectedReset);
  CHECK(!rig.sensor.IsInitialized());
}

TEST_CASE("Switch-off row N2: another device at the address gets no write") {
  // A wrong part or a moved address strap, found at start-up or by CheckHealth (finder probe C6).
  for (const bool at_start : {true, false}) {
    for (const bool by_set_mode : {true, false}) {
      Rig<MinimalPort> rig(Custom());
      if (at_start) {
        rig.port.state.chip.device_id = 0x0116U;
        CHECK_EQ(rig.sensor.EnsureInitialized().error, Error::DeviceIdMismatch);
      } else {
        REQUIRE(rig.Start());
        rig.port.state.chip.device_id = 0x0118U;
        CHECK_EQ(rig.sensor.CheckHealth().error, Error::DeviceIdMismatch);
      }
      const std::size_t from = rig.port.state.frame_count;
      CHECK_EQ(SwitchOff(rig, by_set_mode).error, Error::DeviceIdMismatch);
      CHECK(Transfers(rig.port.state, from, {Rd(kId)}));
      CHECK(!rig.sensor.IsInitialized());
    }
  }
}

TEST_CASE(
    "Switch-off row N2: the chip answering again, or a failed Device_ID read, is switched off") {
  // The mismatch was a passing fault: Device_ID reads 117h again, or the read fails (no
  // evidence); the lost-track rows follow.
  for (const bool read_fails : {false, true}) {
    Rig<MinimalPort> rig(Custom());
    REQUIRE(rig.Start());
    rig.port.state.chip.device_id = 0x0118U;
    CHECK_EQ(rig.sensor.CheckHealth().error, Error::DeviceIdMismatch);
    rig.port.state.chip.device_id = chip::kDeviceIdValue;
    if (read_fails) {
      rig.port.state.fail_transfer = rig.Transfers();
    }
    const std::size_t from = rig.port.state.frame_count;
    CHECK(rig.sensor.Deinitialize().ok());
    CHECK(Transfers(rig.port.state, from, {Rd(kId), Rd(kUl), Wr(kCfg)}));
    CHECK_EQ(ModOf(rig), 1U);
  }
}

TEST_CASE("Switch-off row R1: a reset command that failed but took effect (finder B-1)") {
  // The reset write is applied and its ACK lost; the application switches off at once. The chip
  // is in its power-up load: the first EEPROM_UL read shows it and W1 waits it out.
  for (const bool general_call : {false, true}) {
    for (const bool by_set_mode : {true, false}) {
      tmp117::Config c = Custom();
      c.allow_general_call = true;
      Rig<SwitchPort> rig(c);
      REQUIRE(rig.Start());
      rig.port.state.ack_lost_at = rig.Transfers(); // the reset command
      const auto reset = general_call ? rig.sensor.GeneralCallReset() : rig.sensor.SoftReset();
      CHECK_EQ(reset.error, Error::Bus);
      CHECK(rig.port.state.chip.Loading());
      const int during_load = rig.port.state.chip.writes_during_load;
      CHECK(SwitchOff(rig, by_set_mode).ok());
      CHECK_EQ(rig.port.state.chip.writes_during_load, during_load);
      rig.Wait(30);
      CHECK_EQ(ModOf(rig), 1U);
    }
  }
  // ProgramUserWord with the ACK of transfer k lost, then Deinitialize at once.
  int reset_cases = 0;
  for (int k = 0; k < 40; ++k) {
    Rig<SwitchPort> rig(Custom());
    REQUIRE(rig.Start());
    rig.port.state.ack_lost_at = rig.Transfers() + k;
    const auto p = rig.sensor.ProgramUserWord(0x55AAU);
    rig.port.state.ack_lost_at = -1;
    reset_cases += (!p.ok() && rig.port.state.chip.Loading()) ? 1 : 0;
    const int during_load = rig.port.state.chip.writes_during_load;
    const auto off = rig.sensor.Deinitialize();
    CHECK_EQ(rig.port.state.chip.writes_during_load, during_load);
    if (off.ok()) {
      rig.Wait(30);
      CHECK_EQ(ModOf(rig), 1U);
    }
  }
  CHECK(reset_cases >= 1); // the relock's reset command was among them
}

// -------------------------------------------------------------------------------------------------
// Rows U1 to U6: the driver lost track of the chip
// -------------------------------------------------------------------------------------------------

TEST_CASE("Switch-off row U1: a clean EUN = 0 read is followed by the shutdown write only") {
  for (const bool by_set_mode : {true, false}) {
    Rig<MinimalPort> rig(Custom());
    LoseTrack(rig);
    const std::size_t from = rig.port.state.frame_count;
    CHECK(SwitchOff(rig, by_set_mode).ok());
    CHECK(Transfers(rig.port.state, from, {Rd(kUl), Wr(kCfg)}));
    CHECK_EQ(ModOf(rig), 1U);
  }
}

TEST_CASE("Switch-off row U2: no answer to the read and the lock write costs two transfers") {
  for (const bool by_set_mode : {true, false}) {
    Rig<SwitchPort> rig(Custom());
    rig.port.state.address = 0x49; // nothing answers the driver's address
    CHECK(!rig.sensor.EnsureInitialized().ok());
    const std::size_t from = rig.port.state.frame_count;
    const uint64_t d0 = rig.port.delayed_us;
    CHECK_EQ(SwitchOff(rig, by_set_mode).error, Error::Bus);
    CHECK(Transfers(rig.port.state, from, {Rd(kUl), Wr(kUl)}));
    CHECK_EQ(rig.port.delayed_us, d0);
  }
}

TEST_CASE("Switch-off row U3: a lock confirmed by a clean EUN = 0 read-back is followed by "
          "the shutdown write") {
  // From a clean EUN = 1, from a failed read, and from a reply with a bit of 13:0 set that shows
  // EUN = 0 (it is no evidence of a lock: the lock is written first).
  for (int first = 0; first < 3; ++first) {
    Rig<MinimalPort> rig(Custom());
    LoseTrack(rig);
    Unlock(rig, true);
    if (first == 1) {
      rig.port.state.fail_transfer = rig.Transfers();
    } else if (first == 2) {
      rig.port.state.flip_at = rig.port.state.reads;
      rig.port.state.flip_mask = 0x8001U; // EUN reads 0, bit 0 reads 1
    } else {
      // A clean EUN = 1.
    }
    const int programmed = rig.port.state.chip.eeprom_words_programmed;
    const std::size_t from = rig.port.state.frame_count;
    CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
    CHECK(Transfers(rig.port.state, from, {Rd(kUl), Wr(kUl), Rd(kUl), Wr(kCfg)}));
    CHECK(!rig.port.state.chip.EunSet());
    CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, programmed);
    CHECK_EQ(ModOf(rig), 1U);
  }
}

TEST_CASE("Switch-off row W2: a word being programmed is waited for and read again before the "
          "lock write (regress-r4-2)") {
  // 7 ms programming: one wait; 10 ms: two waits; neither sends a write while EEPROM_Busy is set.
  for (const uint32_t program_us : {7000U, 10000U}) {
    Rig<SwitchPort> rig(Custom());
    LoseTrack(rig);
    Unlock(rig, true);
    rig.port.state.chip.eeprom_program_us = program_us;
    (void)rig.port.state.chip.Write(chip::kHighLimit, 0x1234U); // a word in flight: busy, EUN 1
    const int programmed = rig.port.state.chip.eeprom_words_programmed;
    const int ignored = rig.port.state.chip.writes_while_busy;
    const uint64_t d0 = rig.port.delayed_us;
    const std::size_t from = rig.port.state.frame_count;
    CHECK(rig.sensor.Deinitialize().ok());
    if (program_us == 7000U) {
      CHECK(Transfers(rig.port.state, from, {Rd(kUl), Rd(kUl), Wr(kUl), Rd(kUl), Wr(kCfg)}));
      CHECK_EQ(rig.port.delayed_us - d0, uint64_t{tmp117::kEepromProgramUs});
    } else {
      CHECK(
          Transfers(rig.port.state, from, {Rd(kUl), Rd(kUl), Rd(kUl), Wr(kUl), Rd(kUl), Wr(kCfg)}));
      CHECK_EQ(rig.port.delayed_us - d0, 2U * uint64_t{tmp117::kEepromProgramUs});
    }
    CHECK_EQ(rig.port.state.chip.writes_while_busy, ignored); // no write was ignored
    CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, programmed);
    CHECK(!rig.port.state.chip.EunSet());
    CHECK_EQ(ModOf(rig), 1U);
  }
}

TEST_CASE("Switch-off row U6: an EEPROM still busy after two 7 ms waits gets no write") {
  // Programming slower than twice the typical time (A2): Timeout, nothing sent, the caller calls
  // again (regress-r4-2 with 16 ms).
  for (const bool by_set_mode : {true, false}) {
    Rig<SwitchPort> rig(Custom());
    LoseTrack(rig);
    Unlock(rig, true);
    rig.port.state.chip.eeprom_program_us = 16000U;
    (void)rig.port.state.chip.Write(chip::kHighLimit, 0x1234U);
    const int ignored = rig.port.state.chip.writes_while_busy;
    const uint64_t d0 = rig.port.delayed_us;
    const std::size_t from = rig.port.state.frame_count;
    CHECK_EQ(SwitchOff(rig, by_set_mode).error, Error::Timeout);
    CHECK(Transfers(rig.port.state, from, {Rd(kUl), Rd(kUl), Rd(kUl)}));
    CHECK_EQ(rig.port.delayed_us - d0, 2U * uint64_t{tmp117::kEepromProgramUs});
    CHECK_EQ(rig.port.state.chip.writes_while_busy, ignored);
    // Called again once the word is done: sent.
    CHECK(SwitchOff(rig, by_set_mode).ok());
    CHECK_EQ(rig.port.state.chip.writes_while_busy, ignored);
    CHECK_EQ(ModOf(rig), 1U);
  }
}

TEST_CASE("Switch-off row U6: the two waits are a budget for the whole call") {
  // A 10 ms word in flight takes both waits before the lock write; the lock write then meets a
  // reset (the load starts again): the read-back is busy and no wait is left.
  Rig<SwitchPort> rig(Custom());
  LoseTrack(rig);
  Unlock(rig, true);
  rig.port.state.chip.eeprom_program_us = 10000U;
  (void)rig.port.state.chip.Write(chip::kHighLimit, 0x1234U);
  rig.port.state.reset_at_transfer = rig.Transfers() + 3; // the lock write
  const uint64_t d0 = rig.port.delayed_us;
  const std::size_t from = rig.port.state.frame_count;
  CHECK_EQ(rig.sensor.Deinitialize().error, Error::Timeout);
  CHECK(Transfers(rig.port.state, from, {Rd(kUl), Rd(kUl), Rd(kUl), Wr(kUl), Rd(kUl)}));
  CHECK_EQ(rig.port.delayed_us - d0, 2U * uint64_t{tmp117::kEepromProgramUs});
}

TEST_CASE("Switch-off row W1: a switch-off inside the power-up load waits for it "
          "(regress-r4-1)") {
  // The reset the driver detected (EEPROM_Busy set) and the switch-off right after it, under both
  // readings of A14 (a write during the load ignored with an ACK, or NACKed).
  for (const bool nack : {false, true}) {
    for (const bool by_set_mode : {true, false}) {
      Rig<SwitchPort> rig(Custom());
      rig.port.state.chip.por_write_nack = nack;
      LoseTrackInLoad(rig);
      const int during_load = rig.port.state.chip.writes_during_load;
      const uint64_t d0 = rig.port.delayed_us;
      const std::size_t from = rig.port.state.frame_count;
      CHECK(SwitchOff(rig, by_set_mode).ok());
      CHECK(Transfers(rig.port.state, from, {Rd(kUl), Rd(kUl), Wr(kCfg)}));
      CHECK_EQ(rig.port.delayed_us - d0, uint64_t{tmp117::kEepromProgramUs});
      CHECK_EQ(rig.port.state.chip.writes_during_load, during_load);
      rig.Wait(30);
      CHECK_EQ(ModOf(rig), 1U); // the chip took the write and stays in shutdown
    }
  }
}

TEST_CASE("Switch-off row W1: a lock read-back that shows the power-up load is waited for") {
  // U3 with a busy EUN = 0 read-back: the chip resets at the lock write (which the load ignores,
  // the driver cannot know); the read-back shows the load, so the shutdown write waits for it.
  Rig<SwitchPort> rig(Custom());
  LoseTrack(rig);
  Unlock(rig, true);
  rig.port.state.reset_at_transfer = rig.Transfers() + 1; // the lock write
  const int during_load = rig.port.state.chip.writes_during_load;
  const std::size_t from = rig.port.state.frame_count;
  CHECK(rig.sensor.Deinitialize().ok());
  CHECK(Transfers(rig.port.state, from, {Rd(kUl), Wr(kUl), Rd(kUl), Rd(kUl), Wr(kCfg)}));
  CHECK_EQ(rig.port.state.chip.writes_during_load, during_load + 1); // the lock write only
  rig.Wait(30);
  CHECK_EQ(ModOf(rig), 1U);
}

TEST_CASE("Switch-off row U3: a NACKed first lock write is written again and confirmed") {
  Rig<MinimalPort> rig(Custom());
  LoseTrack(rig);
  Unlock(rig, true);
  rig.port.state.fail_transfer = rig.Transfers() + 1; // the first lock write
  const std::size_t from = rig.port.state.frame_count;
  CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
  CHECK(Transfers(rig.port.state, from, {Rd(kUl), Wr(kUl), Rd(kUl), Wr(kUl), Rd(kUl), Wr(kCfg)}));
  CHECK(!rig.port.state.chip.EunSet());
  CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 0);
}

TEST_CASE("Switch-off row U4: EUN reading 1 after the last lock write refuses (A21)") {
  for (const bool by_set_mode : {true, false}) {
    Rig<MinimalPort> rig(Custom());
    LoseTrack(rig);
    Unlock(rig, false);
    const std::size_t from = rig.port.state.frame_count;
    CHECK_EQ(SwitchOff(rig, by_set_mode).error, Error::EepromVerifyFailed);
    CHECK(Transfers(rig.port.state, from, {Rd(kUl), Wr(kUl), Rd(kUl), Wr(kUl), Rd(kUl)}));
    CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 0);
  }
}

TEST_CASE("Switch-off row U4: a failed first read-back and a clean EUN = 1 after the last lock "
          "write refuses") {
  Rig<MinimalPort> rig(Custom());
  LoseTrack(rig);
  Unlock(rig, false);
  rig.port.state.fail_transfer = rig.Transfers() + 2; // the first read-back
  const std::size_t from = rig.port.state.frame_count;
  CHECK_EQ(rig.sensor.SetMode(tmp117::Mode::Shutdown).error, Error::EepromVerifyFailed);
  CHECK(Transfers(rig.port.state, from, {Rd(kUl), Wr(kUl), Rd(kUl), Wr(kUl), Rd(kUl)}));
  CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 0);
}

TEST_CASE("Switch-off row U5: EUN = 1 before a lock write that took, read-back failed: sent "
          "(regress-r3-1)") {
  // The scenario of the third review: attempt 1 lock write NACKed, read-back clean EUN = 1;
  // attempt 2 lock write delivered (it locks), read-back fails. Only the read after the last lock
  // write counts, and it says nothing: the shutdown write goes out.
  for (const bool garbled : {false, true}) {
    Rig<SwitchPort> rig(Custom());
    LoseTrack(rig);
    Unlock(rig, true);
    const int t0 = rig.Transfers();
    rig.port.state.fail_transfer = t0 + 1; // first lock write
    if (garbled) {
      rig.port.state.flip_at = rig.port.state.reads + 2; // the read after the second lock write
      rig.port.state.flip_mask = 0x0001U;
    } else {
      rig.port.fail_reads_from = t0 + 4;
    }
    const std::size_t from = rig.port.state.frame_count;
    CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
    CHECK(Transfers(rig.port.state, from, {Rd(kUl), Wr(kUl), Rd(kUl), Wr(kUl), Rd(kUl), Wr(kCfg)}));
    CHECK(!rig.port.state.chip.EunSet());
    CHECK_EQ(ModOf(rig), 1U);
  }
}

TEST_CASE("Switch-off row U5: every read failing, writes going through: lock twice, then sent") {
  for (const bool by_set_mode : {true, false}) {
    Rig<MinimalPort> rig(Custom());
    LoseTrack(rig);
    Unlock(rig, true);
    rig.port.state.fail_reads = true;
    const std::size_t from = rig.port.state.frame_count;
    CHECK(SwitchOff(rig, by_set_mode).ok());
    CHECK(Transfers(rig.port.state, from, {Rd(kUl), Wr(kUl), Rd(kUl), Wr(kUl), Rd(kUl), Wr(kCfg)}));
    CHECK(!rig.port.state.chip.EunSet());
    CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 0);
  }
}

TEST_CASE("Switch-off row U5: the residual risk of A21 is a programmed shutdown word") {
  // A lock write that does not take (A6) and no read-back: the driver cannot know, and sends.
  Rig<MinimalPort> rig(Custom());
  LoseTrack(rig);
  Unlock(rig, false);
  rig.port.state.fail_reads = true;
  CHECK(rig.sensor.Deinitialize().ok());
  CHECK(LastIsShutdown(rig.port.state));
  CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 1);
}

TEST_CASE("Switch-off sweep: every pattern of failed transfers keeps the table's promises") {
  // Chips: 0 locked, 1 unlocked and lockable, 2 unlocked and not lockable (A6), 3 unlocked with a
  // word of 10 ms in flight, 4 inside the power-up load. Every subset of the first eight transfers
  // of the call fails.
  for (int kind = 0; kind < 5; ++kind) {
    for (uint32_t mask = 0U; mask < 256U; ++mask) {
      for (const bool by_set_mode : {true, false}) {
        Rig<SwitchPort> rig(Custom());
        if (kind == 4) {
          LoseTrackInLoad(rig);
        } else {
          LoseTrack(rig);
        }
        if ((kind > 0) && (kind < 4)) {
          Unlock(rig, kind != 2);
        }
        if (kind == 3) {
          rig.port.state.chip.eeprom_program_us = 10000U;
          (void)rig.port.state.chip.Write(chip::kHighLimit, 0x1234U);
        }
        const int t0 = rig.Transfers();
        rig.port.fail_base = t0;
        rig.port.fail_mask = mask;
        const uint64_t d0 = rig.port.delayed_us;
        const int programmed0 = rig.port.state.chip.eeprom_words_programmed;
        const std::size_t from = rig.port.state.frame_count;
        const auto r = SwitchOff(rig, by_set_mode);
        rig.port.fail_mask = 0U;
        const int made = rig.Transfers() - t0;
        CHECK(made <= 8);
        CHECK(rig.port.delayed_us - d0 <= 2U * uint64_t{tmp117::kEepromProgramUs});
        // The read after the last lock write: clean with EUN = 1 is the only evidence that refuses.
        // A failed read is recorded with reply 0, which is no evidence.
        bool lock_written = false;
        bool refusal_evidence = false;
        bool confirmed = false;   // the latest EEPROM_UL read is clean with EUN = 0, not busy
        bool busy = false;        // the latest EEPROM_UL read is clean with EEPROM_Busy set
        bool read_failed = false; // the latest EEPROM_UL read failed
        bool no_answer = false;   // U2: that read and the first lock write after it failed
        for (std::size_t i = from; i < rig.port.state.frame_count; ++i) {
          const auto& f = rig.port.state.frames[i];
          const bool failed = (static_cast<int>(i - from) < 32) &&
                              (((mask >> static_cast<uint32_t>(i - from)) & 1U) != 0U);
          if (!f.is_read) {
            CHECK(!busy); // W1, W2, U6: no write follows a clean busy reply
          }
          if (!f.is_read && (f.bytes[0] == kUl)) {
            if (!lock_written) {
              no_answer = read_failed && failed;
            }
            lock_written = true;
            refusal_evidence = false;
            confirmed = false;
          }
          if (f.is_read && (f.bytes[0] == kUl)) {
            const uint32_t reply = f.reply;
            const bool clean = !failed && ((reply & 0x3FFFU) == 0U);
            read_failed = failed;
            busy = clean && ((reply & 0x4000U) != 0U);
            refusal_evidence = lock_written && clean && !busy && ((reply & 0x8000U) != 0U);
            confirmed = clean && !busy && ((reply & 0x8000U) == 0U);
          }
        }
        if (LastIsShutdown(rig.port.state)) {
          CHECK(!refusal_evidence && !no_answer && !busy);
        } else {
          CHECK(refusal_evidence || no_answer || busy);
          CHECK(!r.ok());
          if (busy) {
            CHECK_EQ(r.error, Error::Timeout); // U6
          }
        }
        // The effect: sent after a confirmed lock and acknowledged, the chip is in shutdown.
        if (r.ok() && confirmed) {
          CHECK_EQ(ModOf(rig), 1U);
        }
        if (no_answer) {
          // Nothing more after that lock write: two transfers, or four after a busy reply.
          const auto& last = rig.port.state.frames[rig.port.state.frame_count - 1U];
          CHECK(!last.is_read && (last.bytes[0] == kUl));
          CHECK(made <= 4);
          CHECK_EQ(r.error, Error::Bus);
        }
        // A word is programmed only by a shutdown write sent without a confirmed lock: U5, the
        // residual risk of A21 (every lock write failed or did not take, and no clean read-back).
        if ((kind == 0) || !LastIsShutdown(rig.port.state) || confirmed) {
          CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, programmed0);
        }
      }
    }
  }
}

// -------------------------------------------------------------------------------------------------
// The probes of the third review round
// -------------------------------------------------------------------------------------------------

TEST_CASE("regress-r3-1: stale EUN evidence never refuses a switch-off after a later lock write") {
  // The reviewer's PROBE-R3-1 as written: transfer t0 + 2 fails, every read from t0 + 4 fails.
  Rig<SwitchPort> rig(Custom());
  LoseTrack(rig);
  rig.port.state.chip.ForceUnlock();
  const int t0 = rig.Transfers();
  rig.port.state.fail_transfer = t0 + 2;
  rig.port.fail_reads_from = t0 + 4;
  const int before = rig.Writes(reg::kConfiguration);
  const auto r = rig.sensor.SetMode(tmp117::Mode::Shutdown);
  CHECK(r.ok());
  CHECK_EQ(rig.Writes(reg::kConfiguration), before + 1);
  CHECK_EQ(ModOf(rig), 1U);
  CHECK(!rig.port.state.chip.EunSet());
}

TEST_CASE("regress-r3-2: switch-off with replies stuck at 1 is bounded in transfers and time") {
  for (const uint32_t t : {50U, 1000U}) {
    tmp117::Config c = Custom();
    c.eeprom_timeout_ms = t;
    Rig<SwitchPort> rig(c);
    REQUIRE(rig.Start());
    rig.port.state.stuck = Stuck::Ones;
    CHECK(!rig.sensor.CheckHealth().ok());
    const uint64_t d0 = rig.port.delayed_us;
    const std::size_t from = rig.port.state.frame_count;
    // The Device_ID read of N2 (CheckHealth saw FFFFh), which a stuck word never stops; then U5.
    CHECK(rig.sensor.SetMode(tmp117::Mode::Shutdown).ok());
    CHECK(Transfers(rig.port.state, from,
                    {Rd(kId), Rd(kUl), Wr(kUl), Rd(kUl), Wr(kUl), Rd(kUl), Wr(kCfg)}));
    CHECK_EQ(rig.port.delayed_us, d0);
  }
}

TEST_CASE("regress-r3-2b: Deinitialize after a start-up on an absent chip is two transfers") {
  Rig<SwitchPort> rig(Custom());
  rig.port.state.address = 0x49; // nothing answers the driver's address
  CHECK(!rig.sensor.EnsureInitialized().ok());
  const uint64_t d0 = rig.port.delayed_us;
  const int x0 = rig.Transfers();
  CHECK_EQ(rig.sensor.Deinitialize().error, Error::Bus);
  CHECK_EQ(rig.Transfers() - x0, 2);
  CHECK_EQ(rig.port.delayed_us, d0);
}

TEST_CASE("regress-r3-3: a garbled lock read-back at start-up is not a confirmed lock") {
  // The start-up read of EEPROM_UL after the lock write on an unlocked chip whose lock write does
  // not take, with bits 15 and 0 flipped: EUN reads 0, a reserved bit reads 1.
  int target = -1;
  {
    Rig<MinimalPort> p(Custom());
    Unlock(p, false);
    (void)p.sensor.EnsureInitialized();
    int reads = 0;
    bool lock_written = false;
    for (std::size_t i = 0; i < p.port.state.frame_count; ++i) {
      const auto& f = p.port.state.frames[i];
      if (!f.is_read && (f.length == 3U) && (f.bytes[0] == kUl)) {
        lock_written = true;
      }
      if (f.is_read) {
        if (lock_written && (target < 0) && (f.bytes[0] == kUl)) {
          target = reads;
        }
        ++reads;
      }
    }
  }
  REQUIRE(target >= 0);
  Rig<MinimalPort> rig(Custom());
  Unlock(rig, false);
  rig.port.state.flip_at = target;
  rig.port.state.flip_mask = 0x8001U;
  CHECK_EQ(rig.sensor.EnsureInitialized().error, Error::EepromVerifyFailed);
  CHECK_EQ(rig.port.state.chip.eeprom_words_programmed, 0);
  CHECK_EQ(rig.Writes(reg::kConfiguration), 0);
}
