/**
 * @file tmp117.hpp
 * @brief TMP117 driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-tmp117 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * `tmp117::TMP117<Port>` drives one TMP117 digital temperature sensor through a platform port you
 * write (see `tmp117_i2c_interface.hpp`). The driver has no platform code, allocates nothing,
 * throws nothing, takes no lock and is not thread-safe: give each object one owner, or guard it
 * with one mutex. Section and page numbers are those of SNOSD82D, the TMP117 datasheet, Rev. D.
 * Nothing here is hardware-verified.
 */
#ifndef TMP117_HPP
#define TMP117_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "tmp117_detail.hpp"
#include "tmp117_i2c_interface.hpp"
#include "tmp117_registers.hpp"
#include "tmp117_types.hpp"
#include "tmp117_version.h"

namespace tmp117 {

namespace detail {

// Optional hooks: a `Declares*` concept only tests the name, the `Has*` concept the exact
// signature, `noexcept` and the result type; the driver rejects a hook that is declared but does
// not match (KB-CPP20-007).

/// The port declares something called `ReadAlertPin`.
template <typename Port>
concept DeclaresReadAlertPin = requires(Port& port, bool& level) { port.ReadAlertPin(level); };

/// The port reads the ALERT pin: `bool ReadAlertPin(bool& level) noexcept`.
template <typename Port>
concept HasReadAlertPin =
    requires { ExactHook<Port, bool(bool&) noexcept>::Take(&Port::ReadAlertPin); };

/// The port declares something called `DelayUs`.
template <typename Port>
concept DeclaresDelayUs = requires(Port& port) { port.DelayUs(1_u32); };

/// The port waits: `void DelayUs(uint32_t us) noexcept`. A narrower parameter would truncate long
/// waits.
template <typename Port>
concept HasDelayUs = requires { ExactHook<Port, void(uint32_t) noexcept>::Take(&Port::DelayUs); };

/// The port declares something called `Log`.
template <typename Port>
concept DeclaresLog = requires(Port& port) { port.Log(LogLevel::Info, ""); };

/// The port takes log lines: `void Log(LogLevel, const char* message) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };

/// Wait through the port. A port without `DelayUs()` fails to compile at the first call that needs
/// a wait: the chip's power-up, conversion and programming times are requirements, never skipped.
template <ClassType Port>
void DelayUs(Port& port, uint32_t us) noexcept {
  if constexpr (HasDelayUs<Port>) {
    port.DelayUs(us);
  } else {
    static_assert(kAlwaysFalse<Port>,
                  "tmp117: the port must implement `void DelayUs(uint32_t us) noexcept`");
  }
}

/// Log through the port, or nothing when the port has no `Log()`. The driver never formats text.
template <ClassType Port>
void Log([[maybe_unused]] Port& port, [[maybe_unused]] LogLevel level,
         [[maybe_unused]] const char* message) noexcept {
  if constexpr (HasLog<Port>) {
    port.Log(level, message);
  }
}

// Register encoders and decoders: free constexpr functions, emitted once whatever the port type,
// and proved below with static_assert (KB-CPP20-009, -012).

/// The MOD field for a mode (Table 7-6): continuous 00, shutdown 01.
[[nodiscard]] constexpr uint32_t ModBits(Mode mode) noexcept {
  return (mode == Mode::Shutdown) ? reg::kModShutdown : reg::kModContinuous;
}

/// The Configuration word for `config`: MOD, CONV, AVG, T/nA, POL and DR/Alert; every other bit 0,
/// so the Soft_Reset bit is never set by accident (Table 7-6, p. 27-28).
[[nodiscard]] constexpr uint16_t ConfigWord(const Config& config) noexcept {
  uint16_t word = reg::CfgMod::Insert(0_u16, ModBits(config.mode));
  word = reg::CfgConv::Insert(word, static_cast<uint32_t>(ToUnderlying(config.cycle_time)));
  word = reg::CfgAvg::Insert(word, static_cast<uint32_t>(ToUnderlying(config.averaging)));
  uint32_t bits = word;
  if (config.alert_function == AlertFunction::Threshold) {
    bits |= reg::kCfgTherm;
  }
  if (config.alert_polarity == AlertPolarity::ActiveHigh) {
    bits |= reg::kCfgPolarity;
  }
  if (config.alert_source == AlertSource::DataReady) {
    bits |= reg::kCfgDrAlert;
  }
  return static_cast<uint16_t>(bits);
}

/// A limit or offset register word for millidegrees; `0` for a value outside the range (callers
/// validate first).
[[nodiscard]] constexpr uint16_t CountsWord(int32_t milli_c) noexcept {
  return AsUnsigned16(MilliCToCounts(milli_c).value);
}

/// The Configuration word, decoded (Table 7-6).
[[nodiscard]] constexpr Status DecodeStatus(uint16_t word) noexcept {
  Status s{};
  const uint32_t bits = word;
  s.high_alert = (bits & reg::kCfgHighAlert) != 0U;
  s.low_alert = (bits & reg::kCfgLowAlert) != 0U;
  s.data_ready = (bits & reg::kCfgDataReady) != 0U;
  s.eeprom_busy = (bits & reg::kCfgEepromBusy) != 0U;
  return s;
}

// The factory configuration encodes to the reset values of Table 7-3, p. 25.
static_assert(ConfigWord(Config{}) == reg::kConfigurationReset);
static_assert(CountsWord(Config{}.high_limit_mc) == reg::kHighLimitReset);
static_assert(CountsWord(Config{}.low_limit_mc) == reg::kLowLimitReset);
static_assert(CountsWord(Config{}.offset_mc) == reg::kTempOffsetReset);
// Each field lands where the register map puts it.
static_assert(ConfigWord(Config{.mode = Mode::Shutdown}) == 0x0620U);
static_assert(ConfigWord(Config{.averaging = Averaging::X64, .cycle_time = CycleTime::S16}) ==
              0x03E0U);
static_assert(ConfigWord(Config{.alert_function = AlertFunction::Threshold,
                                .alert_polarity = AlertPolarity::ActiveHigh,
                                .alert_source = AlertSource::DataReady}) == 0x023CU);
static_assert((ConfigWord(Config{}) & reg::kCfgMustBeZero) == 0U);
static_assert(DecodeStatus(0xA000U).high_alert && DecodeStatus(0xA000U).data_ready &&
              !DecodeStatus(0xA000U).low_alert && !DecodeStatus(0xA000U).eeprom_busy);
static_assert(DecodeStatus(0x5000U).low_alert && DecodeStatus(0x5000U).eeprom_busy);

} // namespace detail

/**
 * @defgroup tmp117_driver TMP117 driver
 * @brief The driver class.
 * @{
 */

// clang-format off: the table below must keep one row per line.
/**
 * @brief Driver for one TMP117 on a platform port.
 *
 * Lifecycle: construct with the configuration (no I/O), then `EnsureInitialized()`, the one way to
 * start (bus up, wait for the power-up load, identity, configuration, read-back), use, optionally
 * `Deinitialize()` (shutdown). Every call that touches the chip returns `Error::NotInitialized`
 * until `EnsureInitialized()` has succeeded, except `Deinitialize()` (row N1 of docs/contract.md:
 * `Ok` and no I/O on a driver that never reached the chip, so a converting chip keeps converting);
 * no call starts the chip as a side effect.
 *
 * Port hooks:
 *
 * | Hook | Used for | Without it |
 * | --- | --- | --- |
 * | `void DelayUs(uint32_t us) noexcept` | Power-up load, conversions, EEPROM programming | Compile error: the waits are required |
 * | `bool ReadAlertPin(bool& level) noexcept` | The raw level of the ALERT pin, for `ReadAlertPin()` | `ReadAlertPin()` returns `Unsupported`; poll `ReadStatus()` instead |
 * | `void Log(LogLevel, const char* message) noexcept` | One line per lifecycle event or failure | Nothing is logged |
 *
 * A hook declared with the wrong signature is a compile error.
 *
 * @tparam Port Your class derived from `I2cInterface<Port>`.
 */
// clang-format on
template <I2cPort Port>
class TMP117 {
  static_assert(!detail::DeclaresReadAlertPin<Port> || detail::HasReadAlertPin<Port>,
                "tmp117: the optional ReadAlertPin hook must be `bool ReadAlertPin(bool& level) "
                "noexcept`, so that a failed pin read is reported");
  static_assert(!detail::DeclaresDelayUs<Port> || detail::HasDelayUs<Port>,
                "tmp117: the DelayUs hook must be `void DelayUs(uint32_t us) noexcept`, so that no "
                "wait is truncated");
  static_assert(!detail::DeclaresLog<Port> || detail::HasLog<Port>,
                "tmp117: the optional Log hook must be `void Log(LogLevel, const char* message) "
                "noexcept`");

public:
  /**
   * @brief Bind the driver to a port and a 7-bit I2C address, and store the configuration. No I/O,
   * cannot fail.
   * @param port The port; it must outlive the driver.
   * @param address 7-bit I2C address: `kAddressGnd` (0x48), `kAddressVplus`, `kAddressSda` or
   *        `kAddressScl`; checked by `EnsureInitialized()`, before any I/O.
   * @param config What `EnsureInitialized()` will apply; checked there, before any I/O.
   */
  explicit TMP117(Port& port, uint8_t address = kI2cAddress, const Config& config = {}) noexcept
      : bus_(port, address), address_(address), config_(config) {}

  /**
   * @brief Bind the driver to a port at `kI2cAddress` and store the configuration. No I/O, cannot
   * fail.
   * @param port The port; it must outlive the driver.
   * @param config What `EnsureInitialized()` will apply; checked there, before any I/O.
   */
  explicit TMP117(Port& port, const Config& config) noexcept : TMP117(port, kI2cAddress, config) {}

  /** @brief One driver object per chip. */
  TMP117(const TMP117&) = delete;
  /** @brief One driver object per chip. */
  TMP117& operator=(const TMP117&) = delete;

  /// @name Lifecycle
  /// @{

  /**
   * @brief Start the chip, once: the only way to initialise the driver.
   *
   * Brings the bus up, waits for the power-up load (section 7.5.1.1, p. 18: 1.5 ms typical, no
   * maximum printed, so a bounded busy poll follows, `Config::reset_timeout_ms`), checks Device_ID,
   * reads EEPROM_UL and locks the EEPROM first when EUN reads 1 (a write would otherwise be
   * programmed as a power-on default, section 7.5.1.2, p. 18), writes the limits, the offset and
   * the configuration and reads them back. After it succeeds,
   * later calls return `Ok` at once with no I/O. After it fails, the driver stays uninitialised and
   * the next call starts again from the beginning. It does not reset the chip: the chip is in the
   * state its EEPROM gives it, and the configuration is written over it.
   *
   * @return `Ok`, `InvalidArgument` (the address or the stored configuration is out of range; no
   *         I/O), `Bus`, `Timeout` (EEPROM_Busy never cleared), `DeviceIdMismatch`, `BadReply` (a
   *         reserved bit set, or a register that does not read back what was written) or
   *         `EepromVerifyFailed` (EUN is set and could not be cleared; nothing else was written).
   */
  Result<void> EnsureInitialized() noexcept;

  /** @return true after a successful `EnsureInitialized()` and before a reset is seen or
   * `Deinitialize()` runs. No I/O. */
  [[nodiscard]] bool IsInitialized() const noexcept {
    return initialized_;
  }

  /**
   * @brief Put the chip in shutdown, its low-power safe state, and mark the driver uninitialised.
   *
   * Follows the switch-off decision table of docs/contract.md. An initialised driver sends one
   * Configuration write with MOD = shutdown, whatever doubt it holds about the chip (never as the
   * tail of another write). Does nothing, with no I/O, when the driver never reached the chip
   * (also after a start-up refused for an argument or a port that did not come up). The driver
   * is uninitialised afterwards even if the write failed. A driver that lost track of the chip
   * reads EEPROM_UL first. While a clean reply shows EEPROM_Busy (the power-up load, or a word
   * being programmed) it sends nothing: it waits 7 ms and reads again, at most twice per call.
   * Then it writes the lock (EUN = 0, a write that programs no EEPROM) and reads it back, at most
   * twice, and sends the shutdown write unless the read that follows the last lock write is clean
   * and shows EUN = 1 (ambiguity A21). A failed or garbled read is no evidence and never refuses.
   * After a Device_ID mismatch it reads Device_ID first and sends nothing to another device. At
   * most 8 transfers (9 after a mismatch) and 14 ms of delay.
   *
   * @return `Ok`, `Bus` (the shutdown write failed, or no answer to the read and the lock write),
   *         `Timeout` (EEPROM_Busy still set after two waits; nothing sent, call again),
   *         `DeviceIdMismatch` (another device answers; nothing sent) or `EepromVerifyFailed`
   *         (EUN reads 1 after the last lock write; nothing sent). `Ok` means the write was
   *         acknowledged; the residual risks of docs/contract.md still apply.
   */
  Result<void> Deinitialize() noexcept;

  /**
   * @brief Software reset (Configuration bit 1), then start-up again with the stored configuration.
   *
   * The chip reloads its EEPROM and takes 2 ms (Table 7-6, p. 28). Only this chip is reset.
   *
   * @return `Ok`, `NotInitialized`, `Bus`, `Timeout`, `DeviceIdMismatch` or `BadReply`.
   */
  Result<void> SoftReset() noexcept;

  /**
   * @brief General-call reset: address 00h, byte 06h (section 7.5.3.1.6, p. 22), then start-up
   * again.
   *
   * Every device on the bus that answers the general call resets, not only this chip, so it needs
   * `Config::allow_general_call`; without it nothing is sent.
   *
   * @return `Ok`, `NotInitialized`, `Unsupported` (no opt-in), `Bus`, `Timeout`, `DeviceIdMismatch`
   *         or `BadReply`.
   */
  Result<void> GeneralCallReset() noexcept;

  /**
   * @brief The SMBus alert response is not part of this release: it is a bus-wide, address-
   * arbitrating transfer (section 7.5.3.1.5, p. 21).
   * @return Always `Unsupported`; nothing is sent. Read `ReadStatus()` or `ReadAlertPin()` instead.
   */
  Result<void> SmbusAlertResponse() noexcept;

  /// @}
  /// @name Configuration
  /// @{

  /**
   * @brief Change the whole configuration of the running chip.
   *
   * Writes the limits, the offset and the configuration (the order of start-up) from the validated
   * copy, never from a read-modify-write.
   *
   * @param config Checked with `Validate()` before any I/O.
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (nothing sent), `Busy` (a one-shot is
   *         pending), `Bus` or `UnexpectedReset`.
   */
  Result<void> Configure(const Config& config) noexcept;

  /** @return The configuration the driver believes the chip holds. No I/O. */
  [[nodiscard]] const Config& GetConfig() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (KB-CPP20-044). */
  void GetConfig() const&& = delete;

  /**
   * @brief Change the conversion mode only. `Shutdown` is accepted in every state once the driver
   * reached the chip (`NotInitialized` before) and also ends a pending one-shot: the write is
   * sent even when settling a write in doubt failed. When the settle finds reset evidence, or the
   * driver is uninitialised, it follows the lost-track rows of the switch-off decision table of
   * docs/contract.md, as `Deinitialize()` does: no write while EEPROM_UL reads busy (two 7 ms
   * waits at most, then `Timeout`), a refusal only when the EEPROM_UL read that follows its last
   * lock write is clean and shows EUN = 1 (`EepromVerifyFailed`, nothing sent; the write would be
   * programmed as the power-on default, ambiguity A21), `DeviceIdMismatch` with nothing sent when
   * another device answers, and `Bus` when the chip answers neither the read nor the lock write.
   * At most 8 transfers (9 after a mismatch) and 14 ms of delay after the settle reads.
   * @param mode The mode.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, `Busy` (continuous while a one-shot runs),
   *         `Bus`, `Timeout`, `DeviceIdMismatch`, `UnexpectedReset`, `BadReply`,
   *         `EepromVerifyFailed` (shutdown refused as above, or a mismatch found by a settle).
   */
  Result<void> SetMode(Mode mode) noexcept;

  /** @brief Change the averaging only (`SetMode()` errors apply). @param averaging The setting.
   * @return The error of the write. */
  Result<void> SetAveraging(Averaging averaging) noexcept;
  /** @brief Change the cycle time only. @param cycle_time The setting. @return As `SetAveraging()`.
   */
  Result<void> SetCycleTime(CycleTime cycle_time) noexcept;
  /** @brief Change alert or therm mode only. @param function The setting. @return As
   * `SetAveraging()`. */
  Result<void> SetAlertFunction(AlertFunction function) noexcept;
  /** @brief Change the ALERT polarity only. @param polarity The setting. @return As
   * `SetAveraging()`. */
  Result<void> SetAlertPolarity(AlertPolarity polarity) noexcept;
  /** @brief Change what ALERT shows only. @param source The setting. @return As `SetAveraging()`.
   */
  Result<void> SetAlertSource(AlertSource source) noexcept;

  /**
   * @brief Set both limits, checked and never clamped.
   * @param low_mc Low limit in millidegrees, -256000 to 255992.
   * @param high_mc High limit in millidegrees; not below `low_mc` (ambiguity A10).
   * @return As `SetAveraging()`; `InvalidArgument` sends nothing.
   */
  Result<void> SetLimits(int32_t low_mc, int32_t high_mc) noexcept;

  /**
   * @brief Set the offset added to every result (Temp_Offset).
   * @param offset_mc Millidegrees, -256000 to 255992. The chip saturates its sum (ambiguity A7).
   * @return As `SetAveraging()`.
   */
  Result<void> SetOffsetMilliC(int32_t offset_mc) noexcept;

  /// @}
  /// @name Measurement
  /// @{

  /**
   * @brief Read Temp_Result in counts of 1/128 degC.
   *
   * @param verify `Verify::Device` also reads Device_ID first (one more transfer), which a line
   * stuck at 0 or 1 cannot fake. The default is one transfer, except that a result of 0000h or
   * FFFFh (the words a stuck line returns, and 0 degC or -1 count on a real chip) is confirmed by
   * a Device_ID read before it is returned.
   * @return The counts, or `NotInitialized`, `Busy` (a one-shot is pending), `Bus`, `NoMeasurement`
   *         (8000h before the first conversion), `UnexpectedReset` (8000h after a result was seen)
   * or `DeviceIdMismatch` (with `Verify::Device`, or when the result is 0000h or FFFFh).
   */
  Result<int16_t> ReadTemperatureRaw(Verify verify = Verify::None) noexcept;

  /**
   * @brief Read Temp_Result in millidegrees Celsius, rounded half away from zero.
   * @param verify As `ReadTemperatureRaw()`.
   * @return The millidegrees, or the errors of `ReadTemperatureRaw()`.
   */
  Result<int32_t> ReadTemperatureMilliC(Verify verify = Verify::None) noexcept;

  /**
   * @brief Start one conversion (MOD 11). The chip returns to shutdown by itself when it is done;
   * the driver counts the one-shot complete when Data_Ready was seen or MOD reads 01 (A19).
   *
   * Reads Configuration first (clears a stale Data_Ready and checks for a reset), then writes.
   *
   * @return `Ok`, `NotInitialized`, `Busy` (one is pending), `Bus`, `BadReply`,
   *         `DeviceIdMismatch` (a Configuration reply of 0000h that Device_ID does not confirm) or
   *         `UnexpectedReset`.
   */
  Result<void> StartOneShot() noexcept;

  /**
   * @brief Start one conversion, wait for it from the actual averaging setting (section 7.4.3,
   * p. 15) and read it.
   * @return The millidegrees, or the errors of `StartOneShot()` and `WaitDataReady()` (`Timeout`
   *         sends shutdown first), or `NoMeasurement`.
   */
  Result<int32_t> ReadOneShotMilliC() noexcept;

  /**
   * @brief Wait for Data_Ready, polling Configuration once per millisecond.
   * @param timeout_ms 1 to `kMaxWaitMs`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, `Timeout`, `Bus`, `UnexpectedReset`,
   *         `BadReply` or `DeviceIdMismatch` (a Configuration reply of 0000h that Device_ID does
   *         not confirm).
   */
  Result<void> WaitDataReady(uint32_t timeout_ms) noexcept;

  /**
   * @brief Read the Configuration flags in one transfer (high alert, low alert, data ready, EEPROM
   * busy). Clears them on the chip; the driver latches them.
   * @return The flags of this read, or `NotInitialized`, `Bus`, `BadReply`, `UnexpectedReset`. A
   *         reply of 0000h (the word of a line stuck at 0, and of the all-zero configuration) is
   *         confirmed by a Device_ID read afterwards and returns `DeviceIdMismatch` when that
   * fails. In alert mode the read also releases the ALERT pin; see `GetLatchedStatus()`.
   */
  Result<Status> ReadStatus() noexcept;

  /**
   * @brief The flags seen by any Configuration read since the last `ClearLatchedStatus()`, the
   * driver's own reads included. No I/O.
   *
   * Every read of Configuration the driver makes ORs the three clear-on-read bits into this latch,
   * so no internal read hides an alert or a data-ready from the caller (handbook KB-CPP-119). In
   * therm mode the alert flags are a level that follows the temperature (section 7.4.4.2, p. 17):
   * the latch says "seen since the last clear"; `ReadStatus()` gives the present state.
   *
   * @return The latched flags (`eeprom_busy` is never latched).
   */
  [[nodiscard]] Status GetLatchedStatus() const noexcept {
    return detail::DecodeStatus(latched_flags_);
  }

  /** @brief Empty the latch of `GetLatchedStatus()`. No I/O. */
  void ClearLatchedStatus() noexcept {
    latched_flags_ = 0U;
  }

  /**
   * @brief The level of the ALERT pin after polarity: true while the pin is active.
   * @return The level, or `NotInitialized`, `Unsupported` (the port has no `ReadAlertPin()` hook)
   *         or `Bus` (the hook failed).
   */
  Result<bool> ReadAlertPin() noexcept;

  /// @}
  /// @name EEPROM
  /// @{

  /**
   * @brief Program the power-on defaults (limits, offset, configuration) into EEPROM.
   *
   * One guarded operation, never implicit (7.5.1.2, p. 18; endurance 1000 writes, section 6.6):
   * shut the chip down, unlock, write each word (the Configuration word last: with a continuous
   * MOD the chip converts from that write on, ambiguity A20), wait and poll EEPROM_Busy with a
   * deadline, then lock again **on every path**: write EUN = 0 (ambiguity A6), reset (a soft reset;
   * the general-call reset only with `Config::allow_general_call`, ambiguity A5), check that EUN
   * reads 0, read the words back and compare, then start the running configuration again. EEPROM1
   * and EEPROM3 are never written. Nothing else may use the bus while it runs.
   *
   * @param defaults What the chip should load at power-up; `Validate()` applies.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, `Busy` (a one-shot is pending), `Bus`,
   *         `Timeout`, `BadReply`, `EepromVerifyFailed`. After any error the driver is
   * uninitialised.
   */
  Result<void> ProgramPowerOnDefaults(const Config& defaults) noexcept;

  /**
   * @brief Program the user word EEPROM2 (06h), with the same lock and verify sequence.
   * @param value The word.
   * @return As `ProgramPowerOnDefaults()`.
   */
  Result<void> ProgramUserWord(uint16_t value) noexcept;

  /**
   * @brief Read EEPROM1, EEPROM2 and EEPROM3 (05h, 06h, 08h). EEPROM1 and EEPROM3 hold the
   * traceability identifier (sections 7.5.1.1 and 7.6.7).
   * @return The words, or `NotInitialized` or `Bus`; three identical words of 0000h or FFFFh are
   *         confirmed by a Device_ID read first (`DeviceIdMismatch` when that fails).
   */
  Result<UniqueId> ReadUniqueId() noexcept;

  /// @}
  /// @name Diagnostics
  /// @{

  /**
   * @brief Check the chip is still the one the driver started: Device_ID and the Configuration
   * fields against the driver's copy. Alert flags it reads go to the latch.
   * @return `Ok`, `NotInitialized`, `Bus`, `DeviceIdMismatch`, `BadReply` or `UnexpectedReset`.
   */
  Result<void> CheckHealth() noexcept;

  /**
   * @brief Read one register once: one transfer, no retry, no decoding (handbook KB-CPP-129).
   *
   * For bring-up. Side effects are the chip's own: reading Configuration clears its three flags
   * (the driver latches them) and reading Temp_Result clears Data_Ready.
   *
   * @param pointer A pointer of the map: 00h to 08h or 0Fh.
   * @return The register, or `NotInitialized`, `InvalidArgument` (undefined pointer; nothing sent),
   *         `Busy` (Temp_Result while a one-shot is pending; nothing sent, ambiguity A19) or
   *         `Bus`.
   */
  Result<uint16_t> ReadRegister(uint8_t pointer) noexcept;

  /** @return The last Configuration word read by any call; 0 before the first. No I/O. */
  [[nodiscard]] uint16_t GetLastConfigWord() const noexcept {
    return last_config_word_;
  }

  /** @return The Rev field of Device_ID read at start-up; 0 before. No I/O. */
  [[nodiscard]] uint8_t GetRevision() const noexcept {
    return revision_;
  }

  /// @}

private:
  /// Writes of EUN = 0 the relock tries before it gives up (the frame is idempotent).
  static constexpr uint32_t kLockAttempts = 3U;
  /// Writes of EUN = 0 an uninitialised switch-off tries (docs/contract.md, decision table).
  static constexpr uint32_t kSwitchOffLockAttempts = 2U;
  /// 7 ms waits (`kEepromProgramUs`, the typical programming time, over four times the 1.5 ms
  /// power-up load) an uninitialised switch-off spends on a busy EEPROM, for the whole call (rows
  /// W1, W2 and U6 of the decision table).
  static constexpr uint32_t kSwitchOffBusyWaits = 2U;
  /// What one EEPROM_UL read says about the lock.
  enum class UnlockSeen : uint8_t {
    Nothing,  ///< The read failed, or a bit of 13:0 read 1: no evidence.
    Locked,   ///< Clean, EUN = 0.
    Unlocked, ///< Clean, EUN = 1.
    Busy      ///< Clean, EEPROM_Busy = 1 (switch-off only): no write may follow.
  };
  /// Which registers are in doubt after a write failed (bit mask).
  static constexpr uint8_t kDoubtConfig = 1U;
  static constexpr uint8_t kDoubtHigh = 2U;
  static constexpr uint8_t kDoubtLow = 4U;
  static constexpr uint8_t kDoubtOffset = 8U;

  Result<void> startUp() noexcept;
  Result<void> lockForWrite() noexcept;
  Result<void> switchOff(bool from_set_mode) noexcept;
  Result<void> lockBeforeSwitchOff() noexcept;
  Result<uint16_t> readUnlockWhenIdle(uint32_t& waits) noexcept;
  static UnlockSeen classifyUnlock(const Result<uint16_t>& word) noexcept;
  static UnlockSeen classifyForSwitchOff(const Result<uint16_t>& word) noexcept;
  bool tryLock(Error& first) noexcept;
  void noteDataReady(uint16_t word) noexcept;
  Result<void> waitPowerUp(uint32_t timeout_ms) noexcept;
  Result<void> writeAll(const Config& config) noexcept;
  Result<void> readBackAll(const Config& config) noexcept;
  Result<void> resetAndStart(bool general_call) noexcept;
  Result<void> require() const noexcept;
  Result<void> requireIdle() const noexcept;
  Result<void> prepare() noexcept;
  Result<void> settle() noexcept;
  Result<void> settleLimit(uint8_t doubt_bit, uint8_t pointer, int32_t old_mc,
                           int32_t new_mc) noexcept;
  Result<void> settleConfig() noexcept;
  Result<void> settleOneShot() noexcept;
  Result<void> setConfig(const Config& next, bool force_config) noexcept;
  Result<void> writeConfig(const Config& next, bool force_config) noexcept;
  Result<void> writeCommitted(uint8_t pointer, uint16_t word, uint8_t doubt_bit,
                              const Config& next) noexcept;
  void adoptFields(uint8_t doubt_bit, const Config& from) noexcept;
  Result<uint16_t> readConfigWord() noexcept;
  Result<uint16_t> pollConfig() noexcept;
  Result<void> checkEvidence(uint16_t word) noexcept;
  Result<void> unexpectedReset() noexcept;
  Result<uint16_t> readIdentity() noexcept;
  Result<void> verifyWords(const std::array<uint8_t, 4>& pointers,
                           const std::array<uint16_t, 4>& words, uint32_t count) noexcept;
  Result<void> verifyTraceability(uint16_t id1, uint16_t id3) noexcept;
  Result<void> program(const std::array<uint8_t, 4>& pointers, const std::array<uint16_t, 4>& words,
                       uint32_t count) noexcept;
  Result<void> unlockAndWrite(const std::array<uint8_t, 4>& pointers,
                              const std::array<uint16_t, 4>& words, uint32_t count) noexcept;
  Result<void> waitEepromIdle() noexcept;
  void waitEepromBusyClear() noexcept;
  Result<void> relock() noexcept;
  Result<uint16_t> readUnlock(bool expect_unlocked) noexcept;
  Result<void> fail(Error error, const char* message) noexcept;

  detail::RegisterBus<Port> bus_;
  uint8_t address_;
  Config config_{};
  Config attempted_{}; ///< What a failed write tried to set (`doubt_mask_`).
  uint16_t last_config_word_{0};
  uint16_t latched_flags_{0};
  uint8_t revision_{0};
  uint8_t doubt_mask_{0};
  bool initialized_{false};
  bool touched_{false};           ///< A start-up reached the chip: it may hold our configuration.
  bool identity_mismatch_{false}; ///< The last clean Device_ID read was not DID 117h (row N2).
  bool one_shot_pending_{false};
  bool one_shot_doubt_{false};     ///< A failed one-shot start may have taken effect.
  bool have_result_{false};        ///< A real temperature was read since the start.
  bool one_shot_dr_seen_{false};   ///< Data_Ready was read since the pending one-shot started.
  bool one_shot_mod_stuck_{false}; ///< The last one-shot ended with MOD still reading 11 (A19).
};

/** @} */

} // namespace tmp117

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/tmp117.ipp" // IWYU pragma: export

#endif // TMP117_HPP
