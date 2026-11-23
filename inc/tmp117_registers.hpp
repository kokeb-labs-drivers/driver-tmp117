/**
 * @file tmp117_registers.hpp
 * @brief TMP117 register map: pointers, bit fields and reset values (datasheet section 7.6).
 * @copyright Copyright (c) 2026 The driver-tmp117 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Every register is 16 bits wide, sent most significant byte first (SNOSD82D section 7.5.3.1,
 * p. 20). The pointer register is one byte and its upper four bits must be 0 (Figure 7-9, p. 22).
 * Section and page numbers are those of SNOSD82D, the TMP117 datasheet, Rev. D.
 */
#ifndef TMP117_REGISTERS_HPP
#define TMP117_REGISTERS_HPP

#include <cstdint>

#include "tmp117_detail.hpp"

/** @brief The TMP117 register map. */
namespace tmp117::reg {

using detail::operator""_u8;
using detail::operator""_u16;

/**
 * @defgroup tmp117_registers Register map
 * @brief Pointers, fields and reset values (Table 7-3, section 7.6, p. 25).
 * @{
 */

inline constexpr uint8_t kTempResult = 0x00;    ///< Temp_Result (R): 1/128 degC per count.
inline constexpr uint8_t kConfiguration = 0x01; ///< Configuration (R/W, flags R, clear on read).
inline constexpr uint8_t kHighLimit = 0x02;     ///< THigh_Limit (R/W), EEPROM backed.
inline constexpr uint8_t kLowLimit = 0x03;      ///< TLow_Limit (R/W), EEPROM backed.
inline constexpr uint8_t kEepromUnlock = 0x04;  ///< EEPROM_UL: EUN (R/W) and EEPROM_Busy (R).
inline constexpr uint8_t kEeprom1 = 0x05;       ///< EEPROM1: traceability word, never written.
inline constexpr uint8_t kEeprom2 = 0x06;       ///< EEPROM2: the user word.
inline constexpr uint8_t kTempOffset = 0x07;    ///< Temp_Offset (R/W), EEPROM backed.
inline constexpr uint8_t kEeprom3 = 0x08;       ///< EEPROM3: traceability word, never written.
inline constexpr uint8_t kDeviceId = 0x0F;      ///< Device_ID (R): 0117h.

/**
 * @brief Whether a pointer names a register of the map: 00h to 08h and 0Fh. The driver never
 * sends any other pointer (Table 7-3, p. 25).
 * @param pointer The pointer byte.
 * @return true for a defined register.
 */
constexpr bool IsDefinedPointer(uint8_t pointer) noexcept {
  return (pointer <= kEeprom3) || (pointer == kDeviceId);
}

/**
 * @brief Whether the driver may write a pointer: Configuration, the two limits, EEPROM_UL, EEPROM2
 * and Temp_Offset. Temp_Result and Device_ID are read-only, and EEPROM1 and EEPROM3 hold the
 * traceability identifier the datasheet says not to erase (sections 7.5.1.1, 7.6.7, 7.6.10).
 * @param pointer The pointer byte.
 * @return true for a register the driver writes.
 */
constexpr bool IsWritablePointer(uint8_t pointer) noexcept {
  return IsDefinedPointer(pointer) && (pointer != kTempResult) && (pointer != kEeprom1) &&
         (pointer != kEeprom3) && (pointer != kDeviceId);
}

// Configuration register 01h (Table 7-6, p. 27-28).
inline constexpr uint16_t kCfgHighAlert = 0x8000_u16;  ///< HIGH_Alert (R), bit 15, clears on read.
inline constexpr uint16_t kCfgLowAlert = 0x4000_u16;   ///< LOW_Alert (R), bit 14, clears on read.
inline constexpr uint16_t kCfgDataReady = 0x2000_u16;  ///< Data_Ready (R), bit 13, clears on read.
inline constexpr uint16_t kCfgEepromBusy = 0x1000_u16; ///< EEPROM_Busy (R), bit 12.
inline constexpr uint16_t kCfgTherm = 0x0010_u16;      ///< T/nA, bit 4: 1 therm, 0 alert.
inline constexpr uint16_t kCfgPolarity = 0x0008_u16;   ///< POL, bit 3: 1 active high.
inline constexpr uint16_t kCfgDrAlert = 0x0004_u16; ///< DR/Alert, bit 2: 1 ALERT shows Data_Ready.
inline constexpr uint16_t kCfgSoftReset = 0x0002_u16; ///< Soft_Reset, bit 1: write 1, reads 0.
/// Bits that clear on a read of the configuration register: the driver latches them (RD-31).
inline constexpr uint16_t kCfgClearOnRead = 0xE000_u16;
/// Bits that must read 0: bit 0 is not used and Soft_Reset always reads back 0 (Table 7-6).
inline constexpr uint16_t kCfgMustBeZero = 0x0003_u16;
/// The writable fields MOD (11:10), CONV, AVG, T/nA, POL, DR/Alert (bits 11:2).
inline constexpr uint16_t kCfgWritableMask = 0x0FFC_u16;
/// The fields EEPROM can store: everything writable except MOD1 (Table 7-6, note 2).
inline constexpr uint16_t kCfgEepromMask = 0x07FC_u16;
/// The writable fields without MOD (bits 9:2): what a one-shot conversion in flight leaves alone.
inline constexpr uint16_t kCfgSettingsMask = 0x03FC_u16;

/// MOD, bits 11:10 (00 continuous, 01 shutdown, 10 continuous again, 11 one-shot).
using CfgMod = detail::Field<uint16_t, 10U, 2U>;
/// CONV, bits 9:7 (Table 7-7, p. 28).
using CfgConv = detail::Field<uint16_t, 7U, 3U>;
/// AVG, bits 6:5 (1, 8, 32, 64 conversions).
using CfgAvg = detail::Field<uint16_t, 5U, 2U>;

inline constexpr uint32_t kModContinuous = 0U;      ///< MOD 00: continuous conversion.
inline constexpr uint32_t kModShutdown = 1U;        ///< MOD 01: shutdown.
inline constexpr uint32_t kModContinuousAlias = 2U; ///< MOD 10: continuous; reads back 00.
inline constexpr uint32_t kModOneShot = 3U;         ///< MOD 11: one-shot.

// EEPROM unlock register 04h (Table 7-10, p. 30).
inline constexpr uint16_t kUnlockEun = 0x8000_u16;        ///< EUN, bit 15 (R/W): 1 unlocks.
inline constexpr uint16_t kUnlockBusy = 0x4000_u16;       ///< EEPROM_Busy, bit 14 (R).
inline constexpr uint16_t kUnlockMustBeZero = 0x3FFF_u16; ///< Bits 13:0 read 0.

// Device ID register 0Fh (section 7.6.11, p. 32).
inline constexpr uint16_t kDeviceIdMask = 0x0FFF_u16;  ///< DID, bits 11:0.
inline constexpr uint16_t kDeviceIdValue = 0x0117_u16; ///< DID of the TMP117.
/// Rev, bits 15:12 (the datasheet gives no per-revision value).
using IdRev = detail::Field<uint16_t, 12U, 4U>;

// Reset values (Table 7-3, p. 25): configuration, limits and offset load from EEPROM, so these are
// the factory values.
inline constexpr uint16_t kTempResultReset = 0x8000_u16; ///< -256 degC until the first result.
/// What a data line stuck at 0 returns for any read (0 degC on a real chip).
inline constexpr uint16_t kTempResultAllZeros = 0x0000_u16;
/// What a data line stuck at 1 returns for any read (-1 count on a real chip).
inline constexpr uint16_t kTempResultAllOnes = 0xFFFF_u16;
inline constexpr uint16_t kConfigurationReset = 0x0220_u16; ///< CC, CONV 100, AVG 01, alert mode.
inline constexpr uint16_t kHighLimitReset = 0x6000_u16;     ///< 192 degC.
inline constexpr uint16_t kLowLimitReset = 0x8000_u16;      ///< -256 degC.
inline constexpr uint16_t kTempOffsetReset = 0x0000_u16;    ///< No offset.
inline constexpr uint16_t kEepromUnlockReset = 0x0000_u16;  ///< Locked, not busy.

/// General-call address and the reset command byte (section 7.5.3.1.6, p. 22).
inline constexpr uint8_t kGeneralCallAddress = 0x00;
inline constexpr uint8_t kGeneralCallReset = 0x06; ///< Second byte: reset to power-up values.

// The typed fields are the register map above, proved at build time (KB-CPP20-012).
static_assert(CfgMod::kMask == 0x0C00U);
static_assert(CfgConv::kMask == 0x0380U);
static_assert(CfgAvg::kMask == 0x0060U);
static_assert((uint32_t{CfgMod::kMask} | uint32_t{CfgConv::kMask} | uint32_t{CfgAvg::kMask} |
               uint32_t{kCfgTherm} | uint32_t{kCfgPolarity} | uint32_t{kCfgDrAlert}) ==
              uint32_t{kCfgWritableMask});
static_assert((uint32_t{kCfgSettingsMask} | uint32_t{CfgMod::kMask}) == uint32_t{kCfgWritableMask});
// The factory configuration 0220h is CONV = 100 and AVG = 01 (Table 7-6, note 1).
static_assert(CfgConv::Extract(kConfigurationReset) == 4U);
static_assert(CfgAvg::Extract(kConfigurationReset) == 1U);
static_assert(CfgMod::Extract(kConfigurationReset) == kModContinuous);
static_assert((uint32_t{kCfgClearOnRead} & uint32_t{kCfgWritableMask}) == 0U);
static_assert(IsDefinedPointer(kDeviceId) && IsDefinedPointer(kEeprom3));
static_assert(IsWritablePointer(kEeprom2) && IsWritablePointer(kTempOffset));
static_assert(!IsWritablePointer(kEeprom1) && !IsWritablePointer(kEeprom3) &&
              !IsWritablePointer(kTempResult) && !IsWritablePointer(kDeviceId));
static_assert(!IsDefinedPointer(0x09_u8) && !IsDefinedPointer(0x0E_u8) &&
              !IsDefinedPointer(0x10_u8));

/** @} */

} // namespace tmp117::reg

#endif // TMP117_REGISTERS_HPP
