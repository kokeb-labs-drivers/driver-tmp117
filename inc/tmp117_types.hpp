/**
 * @file tmp117_types.hpp
 * @brief Public types of the TMP117 driver: errors, `Result<T>`, configuration and presets.
 * @copyright Copyright (c) 2026 The driver-tmp117 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is plain data with no platform dependency, so handlers, tests and tools can
 * include it without the driver template. The configuration defaults are the chip's factory reset
 * values (SNOSD82D Table 7-3, p. 25), so `Config{}` describes a chip that was just reset.
 */
#ifndef TMP117_TYPES_HPP
#define TMP117_TYPES_HPP

#include <array>
#include <cstdint>

#include "tmp117_detail.hpp"
#include "tmp117_version.h"

/** @brief The TMP117 driver: everything it declares lives here; internals in `detail`. */
namespace tmp117 {

/**
 * @defgroup tmp117_types Driver types
 * @brief Errors, results, configuration and presets.
 * @{
 */

/**
 * @brief Why a call failed. Every call of the driver returns one of these in its `Result`.
 *
 * Values 0 to 9 are the shared base set; chip-specific values start at 16. `[[nodiscard]]` on
 * the type: a function that returns a bare `Error` cannot have it ignored either (KB-CPP20-037).
 * `Crc` and `DeviceFault` are part of the base set but never returned: the bus has no checksum and
 * the chip reports no sensing fault (docs/errors).
 */
enum class [[nodiscard]] Error : uint8_t {
  // Base set: the same names and values in every driver of the org (standards/driver.md).
  Ok = 0,               ///< The call succeeded.
  NotInitialized = 1,   ///< `EnsureInitialized()` has not succeeded, or a reset was seen since.
  InvalidArgument = 2,  ///< An argument or a `Config` field is out of range; nothing was sent.
  Bus = 3,              ///< A port hook returned `false`.
  Timeout = 4,          ///< The chip did not finish power-up, a conversion or EEPROM programming.
  DeviceIdMismatch = 5, ///< Device_ID did not read DID 117h: wrong chip, no chip or a wiring fault.
  Crc = 6,              ///< Never returned: the TMP117 bus carries no checksum.
  Busy = 7,             ///< A one-shot conversion is pending.
  DeviceFault = 8,      ///< Never returned: the TMP117 reports no sensing fault.
  Unsupported = 9,      ///< The hook, the opt-in or the feature this call needs is absent.
  // Chip-specific values start at 16; each one is returned by some call (tests/test_errors.cpp).
  UnexpectedReset = 16, ///< Evidence that the chip reset since the start; nothing was written.
  NoMeasurement = 17,   ///< Temp_Result reads 8000h: no conversion has finished since a reset.
  EepromVerifyFailed =
      18,        ///< Read-back after EEPROM programming and reset differs, or EUN stays 1.
  BadReply = 19, ///< A reply broke a fixed bit of the register (reserved bits, MOD 10).
};

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string such as `"Timeout"`; `"Unknown"` for a value that is not an enumerator.
 */
constexpr const char* ToString(Error error) noexcept {
  switch (error) {
  case Error::Ok:
    return "Ok";
  case Error::NotInitialized:
    return "NotInitialized";
  case Error::InvalidArgument:
    return "InvalidArgument";
  case Error::Bus:
    return "Bus";
  case Error::Timeout:
    return "Timeout";
  case Error::DeviceIdMismatch:
    return "DeviceIdMismatch";
  case Error::Crc:
    return "Crc";
  case Error::Busy:
    return "Busy";
  case Error::DeviceFault:
    return "DeviceFault";
  case Error::Unsupported:
    return "Unsupported";
  case Error::UnexpectedReset:
    return "UnexpectedReset";
  case Error::NoMeasurement:
    return "NoMeasurement";
  case Error::EepromVerifyFailed:
    return "EepromVerifyFailed";
  case Error::BadReply:
    return "BadReply";
  default:
    break;
  }
  return "Unknown";
}

/**
 * @brief The value of a call, or why there is none.
 *
 * Check `ok()` (or the `bool` conversion) before reading `value`; on failure `value` is `T{}`.
 *
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {T{}, e};
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A call failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events: initialised, reset.
  Debug,     ///< Detail for bring-up.
};

/**
 * @brief Conversion mode kept between calls (Configuration MOD, section 7.4, p. 14-15).
 *
 * One-shot is not a mode you keep: `StartOneShot()` runs one conversion and the chip returns to
 * `Shutdown` by itself. MOD 10 is a duplicate of continuous and is never written.
 */
enum class Mode : uint8_t {
  Continuous = 0, ///< MOD 00: conversions back to back, one per cycle time. Reset value.
  Shutdown = 1,   ///< MOD 01: no conversions; lowest supply current.
};

/** @brief Conversions averaged into one result (Configuration AVG, section 7.3.2, p. 12). */
enum class Averaging : uint8_t {
  X1 = 0,  ///< AVG 00: no averaging.
  X8 = 1,  ///< AVG 01: 8 conversions. Reset value.
  X32 = 2, ///< AVG 10: 32 conversions.
  X64 = 3, ///< AVG 11: 64 conversions.
};

/** @brief Cycle time in continuous mode (Configuration CONV, Table 7-7, p. 28). */
enum class CycleTime : uint8_t {
  Ms15p5 = 0, ///< CONV 000: 15.5 ms with AVG 00; longer averaging sets the floor.
  Ms125 = 1,  ///< CONV 001: 125 ms.
  Ms250 = 2,  ///< CONV 010: 250 ms.
  Ms500 = 3,  ///< CONV 011: 500 ms.
  S1 = 4,     ///< CONV 100: 1 s. Reset value.
  S4 = 5,     ///< CONV 101: 4 s.
  S8 = 6,     ///< CONV 110: 8 s.
  S16 = 7,    ///< CONV 111: 16 s.
};

/** @brief Alert or therm function of the limit registers (Configuration T/nA, section 7.4.4). */
enum class AlertFunction : uint8_t {
  Window = 0,    ///< T/nA 0, alert mode: flags above the high or below the low limit. Reset value.
  Threshold = 1, ///< T/nA 1, therm mode: set above the high limit, cleared below the low limit.
};

/** @brief Polarity of the ALERT pin (Configuration POL). */
enum class AlertPolarity : uint8_t {
  ActiveLow = 0,  ///< The pin pulls low when active. Reset value.
  ActiveHigh = 1, ///< The pin drives high when active.
};

/** @brief What the ALERT pin shows (Configuration DR/Alert). */
enum class AlertSource : uint8_t {
  Flags = 0,     ///< The alert or therm flags. Reset value.
  DataReady = 1, ///< The Data_Ready flag.
};

/** @brief Extra traffic a read may ask for: a second look at the chip, never implicit. */
enum class Verify : uint8_t {
  None = 0,   ///< One transfer only.
  Device = 1, ///< Also read Device_ID and require DID 117h: one more transfer. A line stuck at 0 or
              ///< 1 cannot pass it.
};

/// 7-bit address with ADD0 on GND (Table 7-2, p. 21).
inline constexpr uint8_t kAddressGnd = 0x48;
/// 7-bit address with ADD0 on V+.
inline constexpr uint8_t kAddressVplus = 0x49;
/// 7-bit address with ADD0 on SDA.
inline constexpr uint8_t kAddressSda = 0x4A;
/// 7-bit address with ADD0 on SCL.
inline constexpr uint8_t kAddressScl = 0x4B;

/**
 * @brief The Configuration register flags, decoded from one read.
 *
 * Reading the register clears the three flag bits on the chip, so the driver keeps them in a latch
 * (`GetLatchedStatus()`): no internal read hides a flag from the application (RD-31).
 */
struct Status {
  bool high_alert{false};  ///< HIGH_Alert (bit 15).
  bool low_alert{false};   ///< LOW_Alert (bit 14).
  bool data_ready{false};  ///< Data_Ready (bit 13).
  bool eeprom_busy{false}; ///< EEPROM_Busy (bit 12): power-up load or programming in progress.
};

/** @brief The three traceability and user words (EEPROM1, EEPROM2, EEPROM3; section 7.6.7
 * to 7.6.10). */
struct UniqueId {
  uint16_t eeprom1{0}; ///< EEPROM1 (05h): traceability word.
  uint16_t eeprom2{0}; ///< EEPROM2 (06h): the user word.
  uint16_t eeprom3{0}; ///< EEPROM3 (08h): traceability word.
};

/// Temperature resolution: 128 counts per degree Celsius (1 LSB = 7.8125 millidegrees;
/// section 7.3.3).
inline constexpr int32_t kCountsPerDegreeC = 128;
/// Lowest temperature, limit or offset the registers hold, in millidegrees Celsius (8000h).
inline constexpr int32_t kMinMilliC = -256000;
/// Highest temperature, limit or offset the registers hold, in millidegrees (7FFFh, rounded down).
inline constexpr int32_t kMaxMilliC = 255992;

/// Power-on load: 1.5 ms typical, no maximum printed (section 6.5 tRESET, p. 7; ambiguity A1).
inline constexpr uint32_t kPowerOnLoadUs = 1500;
/// Poll step while waiting for EEPROM_Busy to clear after power-up or reset.
inline constexpr uint32_t kBusyPollUs = 500;
/// Software reset: 2 ms (Table 7-6, Soft_Reset, p. 28).
inline constexpr uint32_t kSoftResetUs = 2000;
/// One conversion, maximum: 17.5 ms (section 6.5, p. 6).
inline constexpr uint32_t kConversionMaxUs = 17500;
/// EEPROM programming of one word: 7 ms typical, no maximum printed (section 6.6, p. 7; A2).
inline constexpr uint32_t kEepromProgramUs = 7000;
/// Poll step while EEPROM_Busy is set.
inline constexpr uint32_t kEepromPollUs = 1000;
/// Poll step of `WaitDataReady()`.
inline constexpr uint32_t kDataReadyPollUs = 1000;
/// Longest `Config::reset_timeout_ms` and `Config::eeprom_timeout_ms` the driver accepts.
inline constexpr uint32_t kMaxTimeoutMs = 1000;
/// Longest argument of `WaitDataReady()`: one 16 s cycle and a margin (KB-CPP20-058).
inline constexpr uint32_t kMaxWaitMs = 20000;

/**
 * @brief Everything `EnsureInitialized()` and `Configure()` write to the chip.
 *
 * The defaults are the factory reset values (section 7.3.1 and Table 7-3), so `Config{}` changes
 * nothing on a chip that was just reset except the driver-side timeouts and opt-ins. Fields with a
 * unit carry it as a suffix. Every field has a default (KB-CPP20-031).
 */
struct Config {
  Mode mode{Mode::Continuous};                            ///< MOD. Reset value: continuous.
  Averaging averaging{Averaging::X8};                     ///< AVG. Reset value: 8 conversions.
  CycleTime cycle_time{CycleTime::S1};                    ///< CONV. Reset value: 1 s.
  AlertFunction alert_function{AlertFunction::Window};    ///< T/nA. Reset value: alert mode.
  AlertPolarity alert_polarity{AlertPolarity::ActiveLow}; ///< POL. Reset value: active low.
  AlertSource alert_source{AlertSource::Flags};           ///< DR/Alert. Reset value: flags.
  int32_t low_limit_mc{kMinMilliC};                       ///< TLow_Limit. Reset value: -256 degC.
  int32_t high_limit_mc{192000};                          ///< THigh_Limit. Reset value: 192 degC.
  int32_t offset_mc{0};                                   ///< Temp_Offset. Reset value: 0.
  uint32_t reset_timeout_ms{10};  ///< Driver side: longest wait for EEPROM_Busy to clear (A1).
  uint32_t eeprom_timeout_ms{50}; ///< Driver side: longest wait for one EEPROM word (A2).
  bool allow_general_call{false}; ///< Opt in to the bus-wide general-call reset (A5, Q1).
};

/**
 * @brief Number of conversions a setting averages.
 * @param averaging The setting.
 * @return 1, 8, 32 or 64.
 */
constexpr uint32_t ConversionsPerResult(Averaging averaging) noexcept {
  switch (averaging) {
  case Averaging::X1:
    return 1U;
  case Averaging::X8:
    return 8U;
  case Averaging::X32:
    return 32U;
  case Averaging::X64:
    return 64U;
  default:
    break;
  }
  return 64U; // an out-of-range value takes the longest wait
}

namespace detail {
/// Table 7-7 (p. 28), nominal cycle time in microseconds: row CONV 000 to 111, column AVG 00 to 11.
inline constexpr std::array<std::array<uint32_t, 4>, 8> kCycleTableUs = {{
    {{15500U, 125000U, 500000U, 1000000U}},
    {{125000U, 125000U, 500000U, 1000000U}},
    {{250000U, 250000U, 500000U, 1000000U}},
    {{500000U, 500000U, 500000U, 1000000U}},
    {{1000000U, 1000000U, 1000000U, 1000000U}},
    {{4000000U, 4000000U, 4000000U, 4000000U}},
    {{8000000U, 8000000U, 8000000U, 8000000U}},
    {{16000000U, 16000000U, 16000000U, 16000000U}},
}};
} // namespace detail

/**
 * @brief Nominal cycle time in continuous mode, from Table 7-7 (a table, not a formula).
 * @param cycle The CONV setting.
 * @param averaging The AVG setting.
 * @return The cycle in microseconds: the CONV time, or the averaging time when that is longer.
 */
constexpr uint32_t GetCycleTimeUs(CycleTime cycle, Averaging averaging) noexcept {
  // The masks keep both indices inside the table for any value the enumerations can hold.
  const uint32_t row = static_cast<uint32_t>(detail::ToUnderlying(cycle)) & 0x07U;
  const uint32_t column = static_cast<uint32_t>(detail::ToUnderlying(averaging)) & 0x03U;
  return detail::kCycleTableUs[row][column];
}

/**
 * @brief The longest a one-shot conversion takes: every conversion at the maximum plus one more as
 * margin for the unspecified oscillator accuracy (section 7.4.3, p. 15; ambiguity A16).
 * @param averaging The AVG setting.
 * @return Microseconds.
 */
constexpr uint32_t GetOneShotWaitUs(Averaging averaging) noexcept {
  return (ConversionsPerResult(averaging) + 1U) * kConversionMaxUs;
}

/**
 * @brief Register counts (1/128 degC) in millidegrees Celsius, rounded half away from zero.
 * @param counts The two's complement register value.
 * @return Millidegrees, -256000 to 255992.
 */
constexpr int32_t CountsToMilliC(int16_t counts) noexcept {
  const int32_t scaled = static_cast<int32_t>(counts) * 1000; // at most 32 768 000
  return (scaled >= 0) ? ((scaled + 64) / 128) : (-((-scaled + 64) / 128));
}

/**
 * @brief Millidegrees Celsius in register counts (1/128 degC), rounded half away from zero.
 * @param milli_c The value; -256000 to 255992.
 * @return The counts, or `InvalidArgument` outside that range (never clamped).
 */
constexpr Result<int16_t> MilliCToCounts(int32_t milli_c) noexcept {
  if ((milli_c < kMinMilliC) || (milli_c > kMaxMilliC)) {
    return Result<int16_t>::Failure(Error::InvalidArgument);
  }
  const int32_t scaled = milli_c * kCountsPerDegreeC; // at most 32 768 000 in magnitude
  const int32_t counts = (scaled >= 0) ? ((scaled + 500) / 1000) : (-((-scaled + 500) / 1000));
  return Result<int16_t>::Success(static_cast<int16_t>(counts)); // in range: checked above
}

namespace detail {
/// Last enumerator of `Mode` (detail::IsEnumerator).
template <>
struct EnumLast<Mode> {
  static constexpr Mode kValue = Mode::Shutdown; ///< The last enumerator.
};
/// Last enumerator of `Averaging`.
template <>
struct EnumLast<Averaging> {
  static constexpr Averaging kValue = Averaging::X64; ///< The last enumerator.
};
/// Last enumerator of `CycleTime`.
template <>
struct EnumLast<CycleTime> {
  static constexpr CycleTime kValue = CycleTime::S16; ///< The last enumerator.
};
/// Last enumerator of `AlertFunction`.
template <>
struct EnumLast<AlertFunction> {
  static constexpr AlertFunction kValue = AlertFunction::Threshold; ///< The last enumerator.
};
/// Last enumerator of `AlertPolarity`.
template <>
struct EnumLast<AlertPolarity> {
  static constexpr AlertPolarity kValue = AlertPolarity::ActiveHigh; ///< The last enumerator.
};
/// Last enumerator of `AlertSource`.
template <>
struct EnumLast<AlertSource> {
  static constexpr AlertSource kValue = AlertSource::DataReady; ///< The last enumerator.
};
/// Last enumerator of `Verify`.
template <>
struct EnumLast<Verify> {
  static constexpr Verify kValue = Verify::Device; ///< The last enumerator.
};
} // namespace detail

/**
 * @brief Check a configuration without touching the chip.
 *
 * Fails when an enumeration holds a value that is not an enumerator, a limit or the offset is
 * outside -256000 to 255992 millidegrees, the low limit is above the high limit (ambiguity A10: the
 * datasheet does not describe an inverted window), or a timeout is zero or above `kMaxTimeoutMs`.
 *
 * @param config The configuration.
 * @return `Error::Ok` or `Error::InvalidArgument`.
 */
constexpr Error Validate(const Config& config) noexcept {
  if (!detail::IsEnumerator(config.mode) || !detail::IsEnumerator(config.averaging) ||
      !detail::IsEnumerator(config.cycle_time) || !detail::IsEnumerator(config.alert_function) ||
      !detail::IsEnumerator(config.alert_polarity) || !detail::IsEnumerator(config.alert_source)) {
    return Error::InvalidArgument;
  }
  if (!MilliCToCounts(config.low_limit_mc).ok() || !MilliCToCounts(config.high_limit_mc).ok() ||
      !MilliCToCounts(config.offset_mc).ok()) {
    return Error::InvalidArgument;
  }
  if (config.low_limit_mc > config.high_limit_mc) {
    return Error::InvalidArgument;
  }
  if ((config.reset_timeout_ms == 0U) || (config.reset_timeout_ms > kMaxTimeoutMs) ||
      (config.eeprom_timeout_ms == 0U) || (config.eeprom_timeout_ms > kMaxTimeoutMs)) {
    return Error::InvalidArgument;
  }
  return Error::Ok;
}

// The factory configuration is valid and equals the datasheet's reset values, proved at build time
// (KB-CPP20-012): 6000h is 192 degC, 8000h is -256 degC (Table 7-1, p. 13).
static_assert(Validate(Config{}) == Error::Ok);
static_assert(MilliCToCounts(Config{}.high_limit_mc).value == 0x6000);
static_assert(MilliCToCounts(Config{}.low_limit_mc).value == -32768);
static_assert(CountsToMilliC(0x6000) == 192000);
static_assert(CountsToMilliC(-32768) == -256000);
static_assert(CountsToMilliC(0x7FFF) == kMaxMilliC);
static_assert(!MilliCToCounts(kMaxMilliC + 1).ok() && !MilliCToCounts(kMinMilliC - 1).ok());
static_assert(GetCycleTimeUs(CycleTime::S1, Averaging::X8) == 1000000U);
static_assert(GetCycleTimeUs(CycleTime::Ms15p5, Averaging::X8) == 125000U);
static_assert(GetOneShotWaitUs(Averaging::X1) == 35000U);

/**
 * @brief Reached only when a `CheckedConfig` is built from an invalid configuration: it is never
 * defined, so the build fails and names it (KB-CPP20-013).
 */
void ConfigIsInvalid() noexcept;

/**
 * @brief A configuration proven valid at build time.
 *
 * `constexpr CheckedConfig kBoard{Config{.averaging = Averaging::X32}};` fails to compile, naming
 * `ConfigIsInvalid`, when `Validate()` rejects the configuration. Pass `kBoard.Get()` to the
 * driver; the driver still validates at start-up, for configurations built at run time.
 */
class CheckedConfig {
public:
  /**
   * @brief Check `config` at build time.
   * @param config The configuration; it must pass `Validate()`.
   */
  consteval explicit CheckedConfig(const Config& config) noexcept : config_(config) {
    if (Validate(config) != Error::Ok) {
      ConfigIsInvalid();
    }
  }
  /** @return The checked configuration. */
  [[nodiscard]] constexpr const Config& Get() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary: the reference would dangle (KB-CPP20-044). */
  void Get() const&& = delete;

private:
  Config config_;
};

/** @return The driver version, e.g. `"0.1.0"`, from the generated `tmp117_version.h`. No I/O. */
constexpr const char* GetDriverVersion() noexcept {
  return TMP117_VERSION_STRING;
}
/** @return The major version. No I/O. */
constexpr uint32_t GetDriverVersionMajor() noexcept {
  return TMP117_VERSION_MAJOR;
}
/** @return The minor version. No I/O. */
constexpr uint32_t GetDriverVersionMinor() noexcept {
  return TMP117_VERSION_MINOR;
}
/** @return The patch version. No I/O. */
constexpr uint32_t GetDriverVersionPatch() noexcept {
  return TMP117_VERSION_PATCH;
}

/** @} */

} // namespace tmp117

#endif // TMP117_TYPES_HPP
