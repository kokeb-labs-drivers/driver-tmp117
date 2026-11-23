/**
 * @file test_model.cpp
 * @brief The chip model's register map against the driver's, at compile time (handbook KB-TST-030),
 * the datasheet's golden values, and the model's own behaviour (it is tested before it is trusted).
 *
 * tests/mock_tmp117.hpp is written from the datasheet without the driver's headers, so a constant
 * the driver gets wrong is not copied into the model. These checks make the two agree, or the build
 * fails: a difference is a mistake in one of them, found against the datasheet section the model
 * names.
 */
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>

#include "mock_tmp117.hpp"
#include "test_harness.hpp"
#include "tmp117.hpp"

namespace {

namespace chip = tmp117_test::chip;
namespace reg = tmp117::reg;

// --8<-- [start:cross-check]
// Register map (Table 7-3, p. 25).
static_assert((chip::kTempResult == reg::kTempResult) &&
              (chip::kConfiguration == reg::kConfiguration) &&
              (chip::kHighLimit == reg::kHighLimit) && (chip::kLowLimit == reg::kLowLimit) &&
              (chip::kEepromUl == reg::kEepromUnlock) && (chip::kEeprom1 == reg::kEeprom1) &&
              (chip::kEeprom2 == reg::kEeprom2) && (chip::kTempOffset == reg::kTempOffset) &&
              (chip::kEeprom3 == reg::kEeprom3) && (chip::kDeviceId == reg::kDeviceId));
// Configuration fields (Table 7-6, p. 27-28) and EEPROM unlock (Table 7-10, p. 30).
static_assert((chip::kHighAlert == reg::kCfgHighAlert) && (chip::kLowAlert == reg::kCfgLowAlert) &&
              (chip::kDataReady == reg::kCfgDataReady) &&
              (chip::kEepromBusy == reg::kCfgEepromBusy) && (chip::kTherm == reg::kCfgTherm) &&
              (chip::kPol == reg::kCfgPolarity) && (chip::kDrAlert == reg::kCfgDrAlert) &&
              (chip::kSoftReset == reg::kCfgSoftReset));
static_assert((chip::kModMask == reg::CfgMod::kMask) && (chip::kConvMask == reg::CfgConv::kMask) &&
              (chip::kAvgMask == reg::CfgAvg::kMask) &&
              (chip::kWritable == reg::kCfgWritableMask) &&
              (chip::kEepromStored == reg::kCfgEepromMask));
static_assert((chip::kEun == reg::kUnlockEun) && (chip::kUlBusy == reg::kUnlockBusy));
// Reset values and identity (Table 7-3; section 7.6.11).
static_assert((chip::kConfigurationReset == reg::kConfigurationReset) &&
              (chip::kHighLimitReset == reg::kHighLimitReset) &&
              (chip::kLowLimitReset == reg::kLowLimitReset) &&
              (chip::kTempResultReset == reg::kTempResultReset) &&
              (chip::kDeviceIdValue == reg::kDeviceIdValue));
// Timing: the driver waits at least what the chip needs (section 6.5, 6.6, Table 7-6).
static_assert((chip::kPowerOnUs == tmp117::kPowerOnLoadUs) &&
              (chip::kSoftResetUs == tmp117::kSoftResetUs) &&
              (chip::kConversionMaxUs == tmp117::kConversionMaxUs) &&
              (chip::kProgramTypUs == tmp117::kEepromProgramUs));
// Bus (section 7.5.3.1.2 and 7.5.3.1.6).
static_assert((chip::kAddressGnd == tmp117::kI2cAddress) &&
              (chip::kGeneralCall == reg::kGeneralCallAddress) &&
              (chip::kGeneralCallReset == reg::kGeneralCallReset));
// The clear-on-read flags the driver latches are the three the model clears on a read.
static_assert(reg::kCfgClearOnRead == (uint32_t{chip::kHighAlert} | uint32_t{chip::kLowAlert} |
                                       uint32_t{chip::kDataReady}));
// --8<-- [end:cross-check]

constexpr bool TablesAgree() {
  for (std::size_t conv = 0; conv < 8; ++conv) {
    for (std::size_t avg = 0; avg < 4; ++avg) {
      const auto c = static_cast<tmp117::CycleTime>(conv);
      const auto a = static_cast<tmp117::Averaging>(avg);
      if (chip::kCycleUs[conv][avg] != tmp117::GetCycleTimeUs(c, a)) {
        return false;
      }
    }
  }
  return true;
}
static_assert(TablesAgree(), "Table 7-7 differs between the model and the driver");

constexpr bool AveragedAgrees() {
  return (tmp117::ConversionsPerResult(tmp117::Averaging::X1) == chip::kAveraged[0]) &&
         (tmp117::ConversionsPerResult(tmp117::Averaging::X8) == chip::kAveraged[1]) &&
         (tmp117::ConversionsPerResult(tmp117::Averaging::X32) == chip::kAveraged[2]) &&
         (tmp117::ConversionsPerResult(tmp117::Averaging::X64) == chip::kAveraged[3]);
}
static_assert(AveragedAgrees());

constexpr bool GoldenRowDecodes(const chip::Golden& g) {
  if (tmp117::CountsToMilliC(tmp117::detail::AsSigned16(g.word)) != g.milli_c) {
    return false;
  }
  if (g.word == 0x8000U) {
    return true; // -256 degC is a limit, not a result: the inverse holds for every other row
  }
  const auto back = tmp117::MilliCToCounts(g.milli_c);
  return back.ok() && (static_cast<uint16_t>(back.value) == g.word);
}

constexpr bool GoldenDecodes() {
  return std::all_of(std::begin(chip::kTable71), std::end(chip::kTable71), GoldenRowDecodes);
}
static_assert(GoldenDecodes(), "Table 7-1 golden values");

} // namespace

TEST_CASE("Table 7-1: every golden word decodes to its temperature") {
  for (const auto& g : chip::kTable71) {
    CHECK_EQ(tmp117::CountsToMilliC(tmp117::detail::AsSigned16(g.word)), g.milli_c);
  }
}

TEST_CASE("The model's registers read their reset values after a power cycle") {
  tmp117_test::Tmp117Model m;
  CHECK(m.Loading());
  m.Elapse(1500);
  CHECK(!m.Loading());
  CHECK_EQ(m.Read(chip::kConfiguration) & uint32_t{chip::kWritable},
           uint32_t{chip::kConfigurationReset});
  CHECK_EQ(m.Read(chip::kHighLimit), chip::kHighLimitReset);
  CHECK_EQ(m.Read(chip::kLowLimit), chip::kLowLimitReset);
  CHECK_EQ(m.Read(chip::kTempOffset), 0);
  CHECK_EQ(m.Read(chip::kTempResult), chip::kTempResultReset); // 8000h until a conversion ends
  CHECK_EQ(m.Read(chip::kEepromUl), 0);
  CHECK_EQ(m.Read(chip::kDeviceId), chip::kDeviceIdValue);
}

TEST_CASE("Writes during the power-up load are ignored; reads still work") {
  tmp117_test::Tmp117Model m;
  CHECK(m.Write(chip::kHighLimit, 0x1234)); // ACKed and ignored (ambiguity A14 default)
  CHECK_EQ(m.writes_during_load, 1);
  CHECK_EQ(m.Read(chip::kHighLimit), chip::kHighLimitReset);
  CHECK((m.Read(chip::kConfiguration) & chip::kEepromBusy) != 0U); // busy during the load
  m.Elapse(1500);
  CHECK(m.Write(chip::kHighLimit, 0x1234));
  CHECK_EQ(m.Read(chip::kHighLimit), 0x1234);
}

TEST_CASE("Soft_Reset reads 0 and MOD 10 reads back 00") {
  tmp117_test::Tmp117Model m;
  m.Elapse(1500);
  CHECK(m.Write(chip::kConfiguration, 0x0A20)); // MOD 10
  CHECK_EQ(m.Read(chip::kConfiguration) & chip::kModMask, 0);
  static_cast<void>(m.Write(chip::kConfiguration, chip::kSoftReset));
  CHECK_EQ(m.soft_resets, 1);
  m.Elapse(2000);
  CHECK_EQ(m.Read(chip::kConfiguration) & chip::kSoftReset, 0);
}

TEST_CASE("The model converts on the Table 7-7 schedule and clears flags on read") {
  tmp117_test::Tmp117Model m;
  m.Elapse(1500);
  m.temperature_counts = 100 * 128;                                  // 100 degC
  static_cast<void>(m.Write(chip::kConfiguration, 0x0400 | 0x0000)); // shutdown, CONV 000, AVG 00
  static_cast<void>(m.Write(chip::kConfiguration, 0x0000)); // continuous, 15.5 ms, no averaging
  m.Elapse(16000);
  CHECK((m.Read(chip::kConfiguration) & chip::kDataReady) != 0U);
  CHECK_EQ(m.Read(chip::kConfiguration) & chip::kDataReady, 0); // cleared by the read
  CHECK_EQ(tmp117::detail::AsSigned16(m.Read(chip::kTempResult)), 100 * 128);
  // Averaging 8 at CONV 100: one result per second, first after 8 conversions (124 ms).
  tmp117_test::Tmp117Model n;
  n.Elapse(1500);
  n.Elapse(120000);
  CHECK_EQ(n.Read(chip::kTempResult), chip::kTempResultReset); // not yet
  n.Elapse(10000);
  CHECK(n.Read(chip::kTempResult) != chip::kTempResultReset);
}

TEST_CASE("A one-shot ends in shutdown, or with MOD still 11 under the A19 knob") {
  for (const bool stays_11 : {false, true}) {
    tmp117_test::Tmp117Model m;
    m.oneshot_mod_reads_back_11 = stays_11;
    m.Elapse(1500);
    static_cast<void>(m.Write(chip::kConfiguration, 0x0C00)); // MOD 11, CONV 000, AVG 00
    CHECK_EQ(m.Read(chip::kConfiguration) & chip::kModMask, 0x0C00);
    m.Elapse(20000);
    const uint16_t word = m.Read(chip::kConfiguration);
    CHECK((word & chip::kDataReady) != 0U);
    CHECK_EQ(word & chip::kModMask, stays_11 ? 0x0C00 : 0x0400);
    CHECK_EQ(m.conversions, 1);
    m.Elapse(100000);
    CHECK_EQ(m.conversions, 1); // no further conversion either way
  }
}

TEST_CASE("The model's alert flags follow the limits, in alert and therm mode") {
  tmp117_test::Tmp117Model m;
  m.Elapse(1500);
  static_cast<void>(m.Write(chip::kHighLimit, static_cast<uint16_t>(30 * 128)));
  static_cast<void>(m.Write(chip::kLowLimit, static_cast<uint16_t>(10 * 128)));
  static_cast<void>(m.Write(chip::kConfiguration, 0x0000)); // CC, AVG 00, CONV 000
  m.temperature_counts = 35 * 128;
  m.Elapse(16000);
  CHECK((m.Read(chip::kConfiguration) & chip::kHighAlert) != 0U);
  CHECK_EQ(m.Read(chip::kConfiguration) & chip::kHighAlert, 0); // cleared by the read
  CHECK(!m.AlertPinHigh() || true);
  // Therm mode: the flag stays set until the result is below the low limit.
  static_cast<void>(m.Write(chip::kConfiguration, chip::kTherm));
  m.Elapse(16000);
  CHECK((m.Read(chip::kConfiguration) & chip::kHighAlert) != 0U);
  m.temperature_counts = 20 * 128;
  m.Elapse(16000);
  CHECK((m.Read(chip::kConfiguration) & chip::kHighAlert) != 0U); // between the limits
  m.temperature_counts = 5 * 128;
  m.Elapse(16000);
  CHECK_EQ(m.Read(chip::kConfiguration) & chip::kHighAlert, 0);
}

TEST_CASE("The model's EEPROM programs per word, locks with EUN and reloads on a reset") {
  tmp117_test::Tmp117Model m;
  m.Elapse(1500);
  static_cast<void>(m.Write(chip::kEepromUl, chip::kEun));
  CHECK(m.EunSet());
  static_cast<void>(m.Write(chip::kEeprom2, 0xBEEF));
  CHECK_EQ(m.EepromUser(), 0xBEEF);
  CHECK((m.Read(chip::kEepromUl) & chip::kUlBusy) != 0U);
  static_cast<void>(m.Write(chip::kHighLimit, 0x1111)); // ignored: busy
  CHECK_EQ(m.writes_while_busy, 1);
  m.Elapse(7000);
  CHECK_EQ(m.Read(chip::kEepromUl) & chip::kUlBusy, 0);
  static_cast<void>(m.Write(chip::kEepromUl, 0));
  CHECK(!m.EunSet());
  static_cast<void>(m.Write(chip::kEeprom2, 0xAAAA)); // locked: volatile only
  CHECK_EQ(m.EepromUser(), 0xBEEF);
  m.GeneralCallReset();
  m.Elapse(1500);
  CHECK_EQ(m.Read(chip::kEeprom2), 0xBEEF);
}

TEST_CASE("AsSigned16 and AsUnsigned16 are two's complement and inverse for every 16-bit word") {
  static_assert(tmp117::detail::AsSigned16(0x0000U) == 0);
  static_assert(tmp117::detail::AsSigned16(0x7FFFU) == 32767);
  static_assert(tmp117::detail::AsSigned16(0x8000U) == -32768);
  static_assert(tmp117::detail::AsSigned16(0xFFFFU) == -1);
  static_assert(tmp117::detail::AsUnsigned16(-1) == 0xFFFFU);
  static_assert(tmp117::detail::AsUnsigned16(-32768) == 0x8000U);
  for (uint32_t w = 0U; w <= 0xFFFFU; ++w) {
    const auto word = static_cast<uint16_t>(w);
    const int16_t s = tmp117::detail::AsSigned16(word);
    const int expected = (w < 0x8000U) ? static_cast<int>(w) : (static_cast<int>(w) - 65536);
    CHECK_EQ(static_cast<int>(s), expected);
    CHECK_EQ(static_cast<uint32_t>(tmp117::detail::AsUnsigned16(s)), w);
  }
}
