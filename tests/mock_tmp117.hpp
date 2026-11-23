/**
 * @file mock_tmp117.hpp
 * @brief A register model of the TMP117 for the host tests, plus the mock ports in
 * mock_tmp117_bus.hpp.
 *
 * The model is written from SNOSD82D (the TMP117 datasheet, Rev. D) on its own (handbook
 * KB-TST-030): it does not include the driver's headers, but carries its own register map, reset
 * values, timing and Table 7-7 below (namespace `chip`, each with its datasheet section), so a
 * wrong constant in the driver cannot hide behind the same wrong constant in the model.
 * tests/test_model.cpp checks the two maps against each other at compile time.
 *
 * What it models: a virtual clock in microseconds that only the port's `DelayUs()` advances; the
 * power-up load with writes ignored during it; continuous, shutdown and one-shot conversions with
 * averaging and the Table 7-7 cycle time; clear-on-read alert and data-ready flags in alert and
 * therm mode; the ALERT pin; the offset; EEPROM with unlock, per-word programming time, busy flag,
 * soft reset, general-call reset and a power cycle; Device_ID. Every ambiguity of
 * docs/ambiguities.md is a knob below; the tests run the safety checks under each reading.
 */
#ifndef TMP117_MOCK_TMP117_HPP
#define TMP117_MOCK_TMP117_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tmp117_test {

/// A 16-bit register word as the signed two's-complement value (the model's own copy: it includes
/// no driver header).
constexpr int16_t Signed16(uint16_t word) noexcept {
  return static_cast<int16_t>(word);
}

/// The address of an object as an integer (no cast: memcpy of the pointer value).
inline std::uintptr_t AddressOf(const void* p) noexcept {
  std::uintptr_t value = 0;
  static_assert(sizeof(value) == sizeof(p));
  std::memcpy(&value, static_cast<const void*>(&p), sizeof(value));
  return value;
}

/// An enumerator no case names: the byte `v` stored in an enumeration of one byte.
template <typename E>
inline E RawEnum(uint8_t v) noexcept {
  static_assert(sizeof(E) == sizeof(v));
  E e{};
  std::memcpy(&e, &v, sizeof(e));
  return e;
}

/// The chip as the datasheet describes it, independent of the driver's headers.
namespace chip {
// Register map (Table 7-3, p. 25): 16-bit registers, pointer byte.
inline constexpr uint8_t kTempResult = 0x00;
inline constexpr uint8_t kConfiguration = 0x01;
inline constexpr uint8_t kHighLimit = 0x02;
inline constexpr uint8_t kLowLimit = 0x03;
inline constexpr uint8_t kEepromUl = 0x04;
inline constexpr uint8_t kEeprom1 = 0x05;
inline constexpr uint8_t kEeprom2 = 0x06;
inline constexpr uint8_t kTempOffset = 0x07;
inline constexpr uint8_t kEeprom3 = 0x08;
inline constexpr uint8_t kDeviceId = 0x0F;
// Configuration bits (Table 7-6, p. 27-28).
inline constexpr uint16_t kHighAlert = 0x8000;
inline constexpr uint16_t kLowAlert = 0x4000;
inline constexpr uint16_t kDataReady = 0x2000;
inline constexpr uint16_t kEepromBusy = 0x1000;
inline constexpr uint16_t kModMask = 0x0C00; // 00 CC, 01 SD, 10 CC (reads 00), 11 OS
inline constexpr uint16_t kConvMask = 0x0380;
inline constexpr uint16_t kAvgMask = 0x0060;
inline constexpr uint16_t kTherm = 0x0010;
inline constexpr uint16_t kPol = 0x0008;
inline constexpr uint16_t kDrAlert = 0x0004;
inline constexpr uint16_t kSoftReset = 0x0002;
inline constexpr uint16_t kWritable = 0x0FFC;
inline constexpr uint16_t kEepromStored = 0x07FC; // MOD1 cannot be stored (note 2)
// EEPROM unlock register (Table 7-10, p. 30).
inline constexpr uint16_t kEun = 0x8000;
inline constexpr uint16_t kUlBusy = 0x4000;
// Reset values (Table 7-3).
inline constexpr uint16_t kConfigurationReset = 0x0220;
inline constexpr uint16_t kHighLimitReset = 0x6000;
inline constexpr uint16_t kLowLimitReset = 0x8000;
inline constexpr uint16_t kTempResultReset = 0x8000;
inline constexpr uint16_t kDeviceIdValue = 0x0117;
// Timing: 6.5 tRESET 1.5 ms typical; Table 7-6 soft reset 2 ms; 6.5 conversion 15.5 ms typical,
// 17.5 ms maximum; 6.6 programming 7 ms typical.
inline constexpr uint32_t kPowerOnUs = 1500;
inline constexpr uint32_t kSoftResetUs = 2000;
inline constexpr uint32_t kConversionTypUs = 15500;
inline constexpr uint32_t kConversionMaxUs = 17500;
inline constexpr uint32_t kProgramTypUs = 7000;
// Bus addresses (section 7.5.3.1, Table 7-2, Figure 7-12).
inline constexpr uint8_t kAddressGnd = 0x48;
inline constexpr uint8_t kGeneralCall = 0x00;
inline constexpr uint8_t kGeneralCallReset = 0x06;
/// Table 7-7 (p. 28): cycle time in microseconds, row CONV 000 to 111, column AVG 00 to 11.
inline constexpr std::array<std::array<uint32_t, 4>, 8> kCycleUs = {{
    {{15500, 125000, 500000, 1000000}},
    {{125000, 125000, 500000, 1000000}},
    {{250000, 250000, 500000, 1000000}},
    {{500000, 500000, 500000, 1000000}},
    {{1000000, 1000000, 1000000, 1000000}},
    {{4000000, 4000000, 4000000, 4000000}},
    {{8000000, 8000000, 8000000, 8000000}},
    {{16000000, 16000000, 16000000, 16000000}},
}};
/// AVG 00 to 11: 1, 8, 32 and 64 conversions (section 7.3.2, p. 12).
inline constexpr std::array<uint32_t, 4> kAveraged = {1, 8, 32, 64};

/// Datasheet Table 7-1 (p. 13), typed from the table: result word and degrees in 1/128 steps.
struct Golden {
  uint16_t word;
  int32_t milli_c; ///< Rounded to the millidegree.
};
inline constexpr std::array<Golden, 11> kTable71 = {{
    {0x8000, -256000}, // -256 degC
    {0xF380, -25000},  // -25 degC
    {0xFFF0, -125},    // -0.125 degC
    {0xFFFF, -8},      // -0.0078125 degC rounds to -8 mdegC
    {0x0000, 0},
    {0x0001, 8},    // +0.0078125 degC
    {0x0010, 125},  // +0.125 degC
    {0x0080, 1000}, // +1 degC
    {0x0C80, 25000},
    {0x3200, 100000},
    {0x7FFF, 255992},
}};
} // namespace chip

class Tmp117Model {
public:
  // --- What the chip is
  // ------------------------------------------------------------------------------------
  uint16_t device_id = chip::kDeviceIdValue; ///< Device_ID; change it to model a wrong part.
  int32_t temperature_counts = 25 * 128;     ///< The die temperature the sensor sees, 1/128 degC.

  // --- Ambiguity knobs (docs/ambiguities.md): the default is the reading the driver assumes
  // ------------------------------------------------------------------------------------
  uint32_t por_load_us = chip::kPowerOnUs;          ///< A1: the power-up load time.
  uint32_t eeprom_program_us = chip::kProgramTypUs; ///< A2: one word's programming time.
  uint16_t eeprom2_reset = 0x0000;                  ///< A3: EEPROM2 as shipped.
  bool offset_in_eeprom = true;                     ///< A4: Temp_Offset reloads from EEPROM.
  bool soft_reset_clears_eun = true;                ///< A5: a soft reset clears EUN.
  bool soft_reset_reloads = true;                   ///< A5: a soft reset reloads the EEPROM.
  bool eun_write_zero_locks = true;                 ///< A6: writing EUN = 0 locks without a reset.
  bool saturate_min_8001 = false;                   ///< A7: offset saturation to 8001h, not 8000h.
  bool limit_compare_inclusive = false;             ///< A8: "exceeds" is >=, "lower than" is <=.
  bool oneshot_sets_alert = false;        ///< A11: one-shot completion sets the alert flag.
  bool therm_read_clears_dr = true;       ///< A12: a Configuration read clears DR in therm mode.
  uint8_t device_rev = 0;                 ///< A13: Rev field of Device_ID.
  bool por_write_nack = false;            ///< A14: writes during the load are NACKed.
  uint32_t osc_error_pct = 0;             ///< A16: conversion time error, percent slower.
  bool oneshot_mod_reads_back_11 = false; ///< A19: MOD keeps reading 11 after a one-shot, not 01.

  // --- Faults to inject
  // ------------------------------------------------------------------------------------
  bool conversion_stuck = false;        ///< A conversion never finishes.
  bool eeprom_stuck_busy = false;       ///< EEPROM_Busy never clears after a programming.
  bool reload_flips_high_limit = false; ///< A reset reloads THigh_Limit with a bit flipped.

  Tmp117Model() noexcept {
    PowerCycle();
  }

  /// The chip as an interrupted programming leaves it: EUN set (Table 7-10), nothing written.
  void ForceUnlock() noexcept {
    eun_ = true;
  }

  /// A power cycle: RAM from EEPROM, locked, load in progress, 8000h result, no flags.
  void PowerCycle() noexcept {
    reload();
    eun_ = false;
    por_until_us_ = now_us_ + por_load_us;
    eeprom_until_us_ = 0;
    power_cycles++;
  }

  /// Time passes: the port's `DelayUs()`.
  void Elapse(uint32_t us) noexcept {
    // Step to the next event so a long delay runs every conversion it spans, in order.
    uint64_t remaining = us;
    while (remaining > 0) {
      const uint64_t step = remaining > 1000 ? 1000 : remaining;
      now_us_ += step;
      remaining -= step;
      advance();
    }
  }

  uint16_t Read(uint8_t pointer) noexcept {
    advance();
    if ((pointer & 0xF0U) != 0U) {
      bad_pointer_accesses++;
    }
    switch (pointer) {
    case chip::kTempResult:
      dr_ = false;
      return temp_result_;
    case chip::kConfiguration:
      return readConfiguration();
    case chip::kHighLimit:
      return high_;
    case chip::kLowLimit:
      return low_;
    case chip::kEepromUl:
      return static_cast<uint16_t>((eun_ ? chip::kEun : 0U) | (eepromBusy() ? chip::kUlBusy : 0U));
    case chip::kEeprom1:
      return ee1_;
    case chip::kEeprom2:
      return user_;
    case chip::kTempOffset:
      return offset_;
    case chip::kEeprom3:
      return ee3_;
    case chip::kDeviceId:
      return static_cast<uint16_t>((static_cast<uint32_t>(device_rev) << 12U) |
                                   (static_cast<uint32_t>(device_id) & 0x0FFFU));
    default:
      bad_pointer_accesses++;
      return 0;
    }
  }

  /// A write transfer; false is a NACK (a write during the load when `por_write_nack`).
  bool Write(uint8_t pointer, uint16_t word) noexcept {
    advance();
    writes++;
    if ((pointer & 0xF0U) != 0U) {
      bad_pointer_accesses++;
    }
    if (loading()) {
      writes_during_load++;
      return !por_write_nack; // "ignored" (section 7.5.1.1)
    }
    if (eepromBusy()) {
      writes_while_busy++; // the datasheet says do not write until programming is complete
      return true;
    }
    switch (pointer) {
    case chip::kConfiguration:
      writeConfiguration(word);
      break;
    case chip::kHighLimit:
      high_ = word;
      programIfUnlocked(pointer, word);
      break;
    case chip::kLowLimit:
      low_ = word;
      programIfUnlocked(pointer, word);
      break;
    case chip::kEepromUl:
      if ((word & chip::kEun) != 0U) {
        eun_ = true;
      } else if (eun_write_zero_locks) {
        eun_ = false;
      } else {
        // The write of 0 does not lock (A6): only a reset does.
      }
      break;
    case chip::kEeprom1:
    case chip::kEeprom3:
      traceability_writes++;
      if (pointer == chip::kEeprom1) {
        ee1_ = word;
      } else {
        ee3_ = word;
      }
      break;
    case chip::kEeprom2:
      user_ = word;
      programIfUnlocked(pointer, word);
      break;
    case chip::kTempOffset:
      offset_ = word;
      programIfUnlocked(pointer, word);
      break;
    case chip::kTempResult:
    case chip::kDeviceId:
      readonly_writes++;
      break;
    default:
      bad_pointer_accesses++;
      break;
    }
    return true;
  }

  /// The general-call reset (Figure 7-12): every register to its power-up value, EUN cleared.
  void GeneralCallReset() noexcept {
    advance();
    reload();
    eun_ = false;
    por_until_us_ = now_us_ + por_load_us;
    general_call_resets++;
  }

  /// The ALERT pin: true is a high level (section 5, p. 4; Configuration POL and DR/Alert).
  [[nodiscard]] bool AlertPinHigh() noexcept {
    advance();
    const bool shows_dr = (cfg_ & chip::kDrAlert) != 0U;
    const bool active = shows_dr ? dr_ : (hi_ || lo_);
    const bool active_high = (cfg_ & chip::kPol) != 0U;
    return active == active_high;
  }

  [[nodiscard]] uint64_t Now() const noexcept {
    return now_us_;
  }
  [[nodiscard]] bool EunSet() const noexcept {
    return eun_;
  }
  [[nodiscard]] bool Loading() noexcept {
    advance();
    return loading();
  }
  /// The raw EEPROM words, for checks: limits, offset, configuration, EEPROM1/2/3.
  [[nodiscard]] uint16_t EepromHigh() const noexcept {
    return ee_high_;
  }
  [[nodiscard]] uint16_t EepromLow() const noexcept {
    return ee_low_;
  }
  [[nodiscard]] uint16_t EepromOffset() const noexcept {
    return ee_offset_;
  }
  [[nodiscard]] uint16_t EepromConfig() const noexcept {
    return ee_cfg_;
  }
  [[nodiscard]] uint16_t EepromUser() const noexcept {
    return ee_user_;
  }
  [[nodiscard]] uint16_t Eeprom1() const noexcept {
    return ee1_;
  }
  [[nodiscard]] uint16_t Eeprom3() const noexcept {
    return ee3_;
  }
  /// The register file as the chip holds it, for checks (no side effects).
  [[nodiscard]] uint16_t RawConfig() const noexcept {
    return cfg_;
  }
  [[nodiscard]] uint16_t RawHigh() const noexcept {
    return high_;
  }
  [[nodiscard]] uint16_t RawLow() const noexcept {
    return low_;
  }
  [[nodiscard]] uint16_t RawOffset() const noexcept {
    return offset_;
  }
  [[nodiscard]] uint16_t RawResult() const noexcept {
    return temp_result_;
  }
  /// Set EEPROM contents as if programmed earlier (no busy time).
  void SetEeprom(uint16_t high, uint16_t low, uint16_t offset, uint16_t config) noexcept {
    ee_high_ = high;
    ee_low_ = low;
    ee_offset_ = offset;
    ee_cfg_ = static_cast<uint16_t>(config & chip::kEepromStored);
  }

  // Counters the tests assert on.
  int writes = 0;
  int writes_during_load = 0;
  int writes_while_busy = 0;
  int traceability_writes = 0;
  int readonly_writes = 0;
  int bad_pointer_accesses = 0;
  int power_cycles = 0;
  int soft_resets = 0;
  int general_call_resets = 0;
  int eeprom_words_programmed = 0;
  int conversions = 0;
  uint16_t last_eeprom_cfg_programmed = 0;

private:
  [[nodiscard]] bool loading() const noexcept {
    return now_us_ < por_until_us_;
  }
  [[nodiscard]] bool eepromBusy() const noexcept {
    return loading() || eeprom_stuck_busy_active_ || (now_us_ < eeprom_until_us_);
  }

  /// RAM registers from EEPROM (power-up and reset).
  void reload() noexcept {
    high_ = reload_flips_high_limit ? static_cast<uint16_t>(ee_high_ ^ 0x0100U) : ee_high_;
    low_ = ee_low_;
    offset_ = offset_in_eeprom ? ee_offset_ : uint16_t{0};
    cfg_ = static_cast<uint16_t>(ee_cfg_ & chip::kWritable);
    user_ = ee_user_;
    temp_result_ = chip::kTempResultReset;
    hi_ = false;
    lo_ = false;
    dr_ = false;
    os_active_ = false;
    cc_scheduled_ = false;
    eeprom_stuck_busy_active_ = false;
  }

  [[nodiscard]] uint32_t convTimeUs() const noexcept {
    return static_cast<uint32_t>((uint64_t{chip::kConversionTypUs} * (100U + osc_error_pct)) /
                                 100U);
  }
  [[nodiscard]] uint32_t activeUs() const noexcept {
    const uint32_t avg = (static_cast<uint32_t>(cfg_) & chip::kAvgMask) >> 5U;
    return chip::kAveraged[avg] * convTimeUs();
  }
  [[nodiscard]] uint32_t periodUs() const noexcept {
    const uint32_t conv = (static_cast<uint32_t>(cfg_) & chip::kConvMask) >> 7U;
    const uint32_t avg = (static_cast<uint32_t>(cfg_) & chip::kAvgMask) >> 5U;
    const uint32_t table = chip::kCycleUs[conv][avg];
    const uint32_t active = activeUs();
    return table > active ? table : active; // no standby when averaging is longer (Table 7-7 note)
  }
  [[nodiscard]] uint32_t mod() const noexcept {
    return (static_cast<uint32_t>(cfg_) & chip::kModMask) >> 10U;
  }

  void startContinuous() noexcept {
    cc_scheduled_ = true;
    next_conv_us_ = now_us_ + activeUs();
  }

  void advance() noexcept {
    if (loading() || conversion_stuck) {
      return;
    }
    if (os_active_) {
      if (now_us_ >= os_done_us_) {
        finishConversion(true);
        os_active_ = false;
        if (!oneshot_mod_reads_back_11) {
          cfg_ = static_cast<uint16_t>((cfg_ & ~static_cast<uint32_t>(chip::kModMask)) |
                                       (1U << 10U)); // back to shutdown
        }
      }
      return;
    }
    const uint32_t m = mod();
    if ((m == 0U) || (m == 2U)) {
      if (!cc_scheduled_) {
        // Starts when the load ends or the mode changes to continuous.
        next_conv_us_ = (por_until_us_ > now_us_ ? por_until_us_ : now_us_) + activeUs();
        cc_scheduled_ = true;
        if (next_conv_us_ < por_until_us_) {
          next_conv_us_ = por_until_us_ + activeUs();
        }
      }
      while (next_conv_us_ <= now_us_) {
        finishConversion(false);
        next_conv_us_ += periodUs();
      }
    } else {
      cc_scheduled_ = false;
    }
  }

  void finishConversion(bool one_shot) noexcept {
    conversions++;
    int32_t sum = temperature_counts + Signed16(offset_);
    sum = std::min<int32_t>(sum, 32767);
    if (sum < -32768) {
      sum = saturate_min_8001 ? -32767 : -32768;
    }
    temp_result_ = static_cast<uint16_t>(sum);
    dr_ = true;
    const int32_t result = Signed16(temp_result_);
    const int32_t high = Signed16(high_);
    const int32_t low = Signed16(low_);
    const bool above = limit_compare_inclusive ? (result >= high) : (result > high);
    const bool below = limit_compare_inclusive ? (result <= low) : (result < low);
    if ((cfg_ & chip::kTherm) != 0U) {
      if (above) {
        hi_ = true;
      } else if (result < low) {
        hi_ = false; // cleared when the result is lower than the hysteresis (section 7.4.4.2)
      } else {
        // Between the limits the flag keeps its value.
      }
      lo_ = false; // always 0 in therm mode
    } else {
      if (above) {
        hi_ = true;
      }
      if (below) {
        lo_ = true;
      }
      if (one_shot && oneshot_sets_alert) {
        hi_ = true;
        lo_ = true;
      }
    }
  }

  uint16_t readConfiguration() noexcept {
    uint32_t m = mod();
    if (m == 2U) {
      m = 0U; // MOD 10 reads back 00
    }
    uint32_t word = (cfg_ & ~static_cast<uint32_t>(chip::kModMask)) | (m << 10U);
    word &= chip::kWritable;
    if (hi_) {
      word |= chip::kHighAlert;
    }
    if (lo_) {
      word |= chip::kLowAlert;
    }
    if (dr_) {
      word |= chip::kDataReady;
    }
    if (eepromBusy()) {
      word |= chip::kEepromBusy;
    }
    const bool therm = (cfg_ & chip::kTherm) != 0U;
    if (!therm) {
      hi_ = false; // cleared on read in alert mode
      lo_ = false;
    }
    if (!therm || therm_read_clears_dr) {
      dr_ = false;
    }
    return static_cast<uint16_t>(word);
  }

  void writeConfiguration(uint16_t word) noexcept {
    if ((word & chip::kSoftReset) != 0U) {
      if (eun_) {
        // A write to a programmable register while unlocked programs it, soft reset bit included.
        programIfUnlocked(chip::kConfiguration, word);
      }
      softReset();
      return;
    }
    const uint32_t old_mod = mod();
    cfg_ = static_cast<uint16_t>(word & chip::kWritable);
    programIfUnlocked(chip::kConfiguration, word);
    const uint32_t new_mod = mod();
    if (new_mod == 3U) {
      os_active_ = true;
      os_done_us_ = now_us_ + activeUs();
      cc_scheduled_ = false;
    } else {
      if (os_active_) {
        os_active_ = false; // aborted by a write of another MOD
      }
      if (((new_mod == 0U) || (new_mod == 2U)) && (old_mod != new_mod)) {
        startContinuous();
      } else if ((new_mod == 0U) || (new_mod == 2U)) {
        startContinuous(); // CONV or AVG changed: the schedule restarts
      } else {
        cc_scheduled_ = false;
      }
    }
  }

  void softReset() noexcept {
    soft_resets++;
    if (soft_reset_reloads) {
      reload();
    }
    if (soft_reset_clears_eun) {
      eun_ = false;
    }
    por_until_us_ = now_us_ + chip::kSoftResetUs;
  }

  void programIfUnlocked(uint8_t pointer, uint16_t word) noexcept {
    if (!eun_) {
      return;
    }
    switch (pointer) {
    case chip::kHighLimit:
      ee_high_ = word;
      break;
    case chip::kLowLimit:
      ee_low_ = word;
      break;
    case chip::kTempOffset:
      ee_offset_ = word;
      break;
    case chip::kConfiguration:
      ee_cfg_ = static_cast<uint16_t>(word & chip::kEepromStored);
      last_eeprom_cfg_programmed = word;
      break;
    case chip::kEeprom2:
      ee_user_ = word;
      break;
    default:
      return;
    }
    eeprom_words_programmed++;
    eeprom_until_us_ = now_us_ + eeprom_program_us;
    eeprom_stuck_busy_active_ = eeprom_stuck_busy;
  }

  uint64_t now_us_ = 0;
  uint64_t por_until_us_ = 0;
  uint64_t eeprom_until_us_ = 0;
  uint64_t next_conv_us_ = 0;
  uint64_t os_done_us_ = 0;
  bool cc_scheduled_ = false;
  bool os_active_ = false;
  bool eun_ = false;
  bool eeprom_stuck_busy_active_ = false;
  bool hi_ = false;
  bool lo_ = false;
  bool dr_ = false;
  uint16_t cfg_ = chip::kConfigurationReset;
  uint16_t high_ = chip::kHighLimitReset;
  uint16_t low_ = chip::kLowLimitReset;
  uint16_t offset_ = 0;
  uint16_t user_ = 0;
  uint16_t temp_result_ = chip::kTempResultReset;
  // EEPROM, as shipped (Table 7-3): the factory values and two traceability words.
  uint16_t ee_high_ = chip::kHighLimitReset;
  uint16_t ee_low_ = chip::kLowLimitReset;
  uint16_t ee_offset_ = 0;
  uint16_t ee_cfg_ = chip::kConfigurationReset;
  uint16_t ee_user_ = 0;
  uint16_t ee1_ = 0x1A2B; // traceability words: arbitrary values for the test
  uint16_t ee3_ = 0x3C4D;
};

} // namespace tmp117_test

#include "mock_tmp117_bus.hpp"

#endif // TMP117_MOCK_TMP117_HPP
