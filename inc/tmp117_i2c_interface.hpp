// clang-format off: the table below must keep one row per line.
/**
 * @file tmp117_i2c_interface.hpp
 * @brief The I2C port of the TMP117 driver: the CRTP base, the `I2cPort` concept and the register
 * framing.
 * @copyright Copyright (c) 2026 The driver-tmp117 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. It calls hooks on a class you derive from
 * `tmp117::I2cInterface<Self>`:
 *
 * | Hook | Contract |
 * | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Bring the bus up on the first call; `true` at once afterwards |
 * | `bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | START, `addr` + W, `len` bytes, STOP; `false` on a NACK or bus error |
 * | `bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx, std::size_t rx_len) noexcept` | START, `addr` + W, `tx`, repeated START, `addr` + R, `rx_len` bytes, STOP |
 *
 * `addr` is the 7-bit address. The port moves raw bytes: the pointer travels in the payload, and the
 * port never retries or configures the chip. Optional hooks: `void Log(LogLevel, const char*)` and
 * `bool ReadAlertPin(bool& level)` (the raw level of the ALERT pin; `false` when the pin cannot be
 * read), and `void DelayUs(uint32_t)`, which every wait needs. What the chip needs from the bus is
 * below as `constexpr` values (`kI2cAddress`, `kMaxClockHz`, `kRepeatedStart`); a port checks its
 * settings against them with `static_assert`.
 */
// clang-format on
#ifndef TMP117_I2C_INTERFACE_HPP
#define TMP117_I2C_INTERFACE_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "tmp117_detail.hpp"
#include "tmp117_registers.hpp"
#include "tmp117_types.hpp"

namespace tmp117 {

/**
 * @name Bus requirements
 * What the TMP117 needs from the I2C bus (datasheet sections 6.7 and 7.5.3.1, p. 7 and 20 to 22). A
 * port and a board configuration check their settings against these with `static_assert`
 * (handbook KB-CPP-123).
 * @{
 */
/// 7-bit I2C address with ADD0 on GND; V+, SDA and SCL give 0x49, 0x4A and 0x4B (Table 7-2).
inline constexpr uint8_t kI2cAddress = kAddressGnd;
/// Highest SCL frequency: fast mode, 400 kHz (section 6.7).
inline constexpr uint32_t kMaxClockHz = 400'000;
/// Lowest SCL frequency: below it the 35 ms (typical) bus timeout resets the interface
/// (section 7.5.3.1.7, p. 22).
inline constexpr uint32_t kMinClockHz = 5'000;
/// `true`: a register read is a write of the pointer, a repeated START and a read, in one
/// transaction (Figure 7-10, p. 23).
inline constexpr bool kRepeatedStart = true;
/** @} */

/**
 * @brief CRTP base of the platform I2C port.
 *
 * Derive `class MyPort : public tmp117::I2cInterface<MyPort>` and implement the hooks in this
 * file's description. The base declares no hook itself (a forwarding hook would turn a missing
 * override into infinite recursion) and no data; the driver checks your class with the `I2cPort`
 * concept, so a missing or misdeclared hook is a compile error that names it.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class I2cInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  I2cInterface(const I2cInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  I2cInterface& operator=(const I2cInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  I2cInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~I2cInterface() noexcept = default;
};

/**
 * @brief Satisfied by a class that derives from `I2cInterface<Port>` and implements the required
 * hooks with exactly the types above, `noexcept` and returning `bool`.
 *
 * Each hook's address is matched with `detail::ExactHook`, which takes a member function of exactly
 * the signature and converts no value: a hook declared with a narrower length or without `noexcept`
 * does not satisfy the concept (handbook KB-CPP20-006).
 *
 * @tparam Port The port class.
 */
template <typename Port>
concept I2cPort = std::derived_from<Port, I2cInterface<Port>> && requires {
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  detail::ExactHook<Port, bool(uint8_t, const uint8_t*, std::size_t) noexcept>::Take(&Port::Write);
  detail::ExactHook<Port, bool(uint8_t, const uint8_t*, std::size_t, uint8_t*,
                               std::size_t) noexcept>::Take(&Port::WriteRead);
};

namespace detail {

/// Longest transfer the driver makes: pointer byte and one 16-bit word.
inline constexpr std::size_t kFrameSize = 3;

/// Register access over I2C: the pointer is the first payload byte, words travel most significant
/// byte first, reads use a repeated START. The transfer buffers are members, so they outlive the
/// call a port may complete later (RD-06).
template <I2cPort Port>
class RegisterBus {
public:
  RegisterBus(Port& port, uint8_t address) noexcept : port_(port), address_(address) {}

  Port& GetPort() & noexcept {
    return port_;
  }

  /// Read one 16-bit register. `InvalidArgument` (nothing sent) for a pointer outside the map,
  /// `Bus` on a NACK or bus error.
  Result<uint16_t> ReadWord(uint8_t pointer) noexcept {
    if (!reg::IsDefinedPointer(pointer)) {
      return Result<uint16_t>::Failure(Error::InvalidArgument);
    }
    tx_[0] = pointer;
    rx_[0] = 0U;
    rx_[1] = 0U;
    if (!port_.WriteRead(address_, tx_.data(), 1U, rx_.data(), 2U)) {
      return Result<uint16_t>::Failure(Error::Bus);
    }
    const uint32_t word = (static_cast<uint32_t>(rx_[0]) << 8U) | static_cast<uint32_t>(rx_[1]);
    return Result<uint16_t>::Success(static_cast<uint16_t>(word));
  }

  /// Write one 16-bit register. `InvalidArgument` (nothing sent) for a pointer the driver never
  /// writes (outside the map, read-only, or a traceability word),
  /// `Bus` on a NACK or bus error.
  Result<void> WriteWord(uint8_t pointer, uint16_t value) noexcept {
    if (!reg::IsWritablePointer(pointer)) {
      return Result<void>::Failure(Error::InvalidArgument);
    }
    tx_[0] = pointer;
    tx_[1] = static_cast<uint8_t>(static_cast<uint32_t>(value) >> 8U);
    tx_[2] = static_cast<uint8_t>(static_cast<uint32_t>(value) & 0xFFU);
    return port_.Write(address_, tx_.data(), kFrameSize) ? Result<void>::Success()
                                                         : Result<void>::Failure(Error::Bus);
  }

  /// The general-call reset, address 00h and one byte 06h (section 7.5.3.1.6, p. 22; Figure 7-12).
  Result<void> GeneralCallReset() noexcept {
    tx_[0] = reg::kGeneralCallReset;
    return port_.Write(reg::kGeneralCallAddress, tx_.data(), 1U)
               ? Result<void>::Success()
               : Result<void>::Failure(Error::Bus);
  }

private:
  Port& port_;
  uint8_t address_;
  std::array<uint8_t, kFrameSize> tx_{};
  std::array<uint8_t, 2> rx_{};
};

} // namespace detail

} // namespace tmp117

#endif // TMP117_I2C_INTERFACE_HPP
