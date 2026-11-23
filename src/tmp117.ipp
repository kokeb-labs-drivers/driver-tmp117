/**
 * @file tmp117.ipp
 * @brief TMP117 driver implementation, included by `tmp117.hpp`.
 * @copyright Copyright (c) 2026 The driver-tmp117 authors. SPDX-License-Identifier: MIT
 *
 * Section and page numbers refer to SNOSD82D, the TMP117 datasheet, Rev. D.
 */
#ifndef TMP117_IPP
#define TMP117_IPP

namespace tmp117 {

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> TMP117<Port>::EnsureInitialized() noexcept {
  if (initialized_) {
    return Result<void>::Success();
  }
  if (auto r = startUp(); !r) {
    return r;
  }
  detail::Log(bus_.GetPort(), LogLevel::Info, "tmp117: initialised");
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::startUp() noexcept {
  initialized_ = false;
  one_shot_pending_ = false;
  one_shot_doubt_ = false;
  one_shot_dr_seen_ = false;
  one_shot_mod_stuck_ = false;
  doubt_mask_ = 0U;
  have_result_ = false;
  // Nothing is sent for an argument that cannot be right (handbook KB-CPP-120).
  if ((address_ < kAddressGnd) || (address_ > kAddressScl) || (Validate(config_) != Error::Ok)) {
    return fail(Error::InvalidArgument, "tmp117: invalid address or Config");
  }
  if (!bus_.GetPort().EnsureInitialized()) {
    return fail(Error::Bus, "tmp117: the port could not bring the bus up");
  }
  touched_ = true;
  // Power-up: the EEPROM loads the registers for about 1.5 ms and writes in that time are ignored
  // (section 7.5.1.1, p. 18), so nothing is written until EEPROM_Busy has read 0 twice.
  if (auto r = waitPowerUp(config_.reset_timeout_ms); !r) {
    return r;
  }
  const auto id = readIdentity();
  if (!id) {
    return Result<void>::Failure(id.error);
  }
  revision_ = static_cast<uint8_t>(reg::IdRev::Extract(id.value));
  // With EUN set every write below would be programmed into EEPROM (section 7.5.1.2, p. 18): an
  // interrupted programming or a failed lock leaves the chip so. Lock first, write nothing else
  // when it cannot be confirmed (ambiguity A21).
  if (auto r = lockForWrite(); !r) {
    return r;
  }
  if (auto r = writeAll(config_); !r) {
    return r;
  }
  if (auto r = readBackAll(config_); !r) {
    return r;
  }
  latched_flags_ = 0U; // the flags of the start-up reads are not news
  initialized_ = true;
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::lockForWrite() noexcept {
  // Before the first write of a start-up: with EUN set the write would be programmed as a power-on
  // default (section 7.5.1.2, p. 18). Read EEPROM_UL; lock when it is set; refuse (nothing
  // written) when that cannot be confirmed (ambiguity A21). Switch-off has its own rules
  // (lockBeforeSwitchOff()). An initialised driver needs neither, since it is initialised only
  // after a lock was confirmed; its switch-off runs the lost-track rows only when a settle finds
  // reset evidence (row I3).
  const auto word = bus_.ReadWord(reg::kEepromUnlock);
  if (!word) {
    return fail(word.error, "tmp117: EEPROM_UL read failed: nothing is written");
  }
  if ((static_cast<uint32_t>(word.value) & reg::kUnlockMustBeZero) != 0U) {
    return fail(Error::BadReply, "tmp117: EEPROM_UL bits 13:0 read as 1: nothing is written");
  }
  if ((static_cast<uint32_t>(word.value) & reg::kUnlockEun) == 0U) {
    return Result<void>::Success();
  }
  Error first = Error::Ok;
  if (tryLock(first)) {
    return Result<void>::Success();
  }
  return fail((first != Error::Ok) ? first : Error::EepromVerifyFailed,
              "tmp117: the EEPROM is unlocked and could not be locked: nothing is written, "
              "power-cycle the chip");
}

template <I2cPort Port>
typename TMP117<Port>::UnlockSeen TMP117<Port>::classifyUnlock(
    const Result<uint16_t>& word) noexcept {
  // A failed read, or a reply with a bit of 13:0 set (they always read 0, Table 7-10, p. 30), says
  // nothing about EUN: no evidence either way.
  if (!word) {
    return UnlockSeen::Nothing;
  }
  const uint32_t bits = word.value;
  if ((bits & reg::kUnlockMustBeZero) != 0U) {
    return UnlockSeen::Nothing;
  }
  return ((bits & reg::kUnlockEun) == 0U) ? UnlockSeen::Locked : UnlockSeen::Unlocked;
}

template <I2cPort Port>
typename TMP117<Port>::UnlockSeen TMP117<Port>::classifyForSwitchOff(
    const Result<uint16_t>& word) noexcept {
  // EEPROM_Busy before EUN: during the power-up load (section 7.5.1.1, p. 18) and while a word is
  // programmed (7.5.1.2) a write is ignored or forbidden, whatever EUN reads.
  if (word.ok() && ((static_cast<uint32_t>(word.value) & reg::kUnlockMustBeZero) == 0U) &&
      ((static_cast<uint32_t>(word.value) & reg::kUnlockBusy) != 0U)) {
    return UnlockSeen::Busy;
  }
  return classifyUnlock(word);
}

template <I2cPort Port>
Result<uint16_t> TMP117<Port>::readUnlockWhenIdle(uint32_t& waits) noexcept {
  // Rows W1 and W2: a clean reply with EEPROM_Busy set is followed by no write. Wait 7 ms and read
  // again, as Figure 7-7 (p. 19) does, within the budget of the whole call.
  auto word = bus_.ReadWord(reg::kEepromUnlock);
  while ((classifyForSwitchOff(word) == UnlockSeen::Busy) && (waits < kSwitchOffBusyWaits)) {
    ++waits;
    detail::DelayUs(bus_.GetPort(), kEepromProgramUs);
    word = bus_.ReadWord(reg::kEepromUnlock);
  }
  return word;
}

template <I2cPort Port>
Result<void> TMP117<Port>::lockBeforeSwitchOff() noexcept {
  // Rows U1 to U6, W1 and W2 of the switch-off decision table (docs/contract.md). No write follows
  // a clean busy reply; only the EEPROM_UL read that follows the last lock write decides: a clean
  // EUN = 1 there refuses, a busy one times out, anything else sends. At most 1 +
  // kSwitchOffBusyWaits + 2 x kSwitchOffLockAttempts transfers and kSwitchOffBusyWaits x 7 ms.
  uint32_t waits = 0U;
  const auto first = readUnlockWhenIdle(waits);
  UnlockSeen seen = classifyForSwitchOff(first);
  for (uint32_t attempt = 0U; attempt < kSwitchOffLockAttempts; ++attempt) {
    if ((seen == UnlockSeen::Locked) || (seen == UnlockSeen::Busy)) {
      break;
    }
    const auto lock = bus_.WriteWord(reg::kEepromUnlock, reg::kEepromUnlockReset);
    if (!lock && !first && (attempt == 0U)) {
      // U2: the chip answered neither the read before it nor the lock write: nothing more.
      return fail(lock.error, "tmp117: no answer from the chip: shutdown not sent");
    }
    seen = classifyForSwitchOff(readUnlockWhenIdle(waits));
  }
  if (seen == UnlockSeen::Locked) {
    return Result<void>::Success(); // U1, U3
  }
  if (seen == UnlockSeen::Busy) {
    // U6: still loading or programming after the wait budget: a write now would be ignored.
    return fail(Error::Timeout, "tmp117: EEPROM_Busy still set: shutdown not sent, call again");
  }
  if (seen == UnlockSeen::Nothing) {
    // U5: the last lock write was not read back: no evidence that the shutdown word would be
    // programmed, so it is sent (ambiguity A21, residual risk).
    detail::Log(bus_.GetPort(), LogLevel::Warning,
                "tmp117: the lock is not confirmed, shutting down anyway");
    return Result<void>::Success();
  }
  // U4: EUN reads 1 after the last lock write: a write now would be programmed into EEPROM.
  return fail(Error::EepromVerifyFailed,
              "tmp117: the EEPROM reads unlocked after the lock write: shutdown not sent, "
              "power-cycle the chip");
}

template <I2cPort Port>
Result<void> TMP117<Port>::switchOff(bool from_set_mode) noexcept {
  // The one switch-off routine, for Deinitialize() and SetMode(Mode::Shutdown); its decision table
  // is in docs/contract.md. Rows I1 and I2: an initialised driver always sends; U1 to U6: a driver
  // that lost track of the chip clears a possible EEPROM unlock first (lockBeforeSwitchOff()).
  if (!touched_) {
    // N1: the driver never reached the chip.
    return from_set_mode ? Result<void>::Failure(Error::NotInitialized) : Result<void>::Success();
  }
  if (identity_mismatch_) {
    // N2: the last Device_ID read named another part. Ask once more: only another part's word
    // again stops the call. A failed read, DID 117h, or 0000h or FFFFh (the words of a line stuck
    // at 0 or 1, which may still pass writes) go on by the rows below.
    const auto id = bus_.ReadWord(reg::kDeviceId);
    const bool foreign =
        id.ok() && (id.value != reg::kTempResultAllZeros) &&
        (id.value != reg::kTempResultAllOnes) &&
        ((static_cast<uint32_t>(id.value) & reg::kDeviceIdMask) != reg::kDeviceIdValue);
    if (foreign) {
      initialized_ = false;
      one_shot_pending_ = false;
      one_shot_mod_stuck_ = false;
      return fail(Error::DeviceIdMismatch, "tmp117: another device answers: shutdown not sent");
    }
  }
  if (initialized_ && from_set_mode) {
    // I1: a write in doubt is settled first, but a failure to settle never stops the shutdown:
    // the write goes out without a second settle, and the copy follows the chip (RD-05).
    if (const auto s = settle(); !s) {
      detail::Log(bus_.GetPort(), LogLevel::Warning, "tmp117: settling failed, shutting down");
    }
    if (initialized_) {
      Config off = config_;
      off.mode = Mode::Shutdown;
      return writeConfig(off, true);
    }
    // I3: the settle found reset evidence, so the chip may still be loading: the rows of a
    // driver that lost track apply.
  }
  const bool known = initialized_;
  initialized_ = false;
  one_shot_pending_ = false;
  one_shot_mod_stuck_ = false;
  if (!known) {
    if (auto r = lockBeforeSwitchOff(); !r) {
      return r;
    }
  }
  // I2 and U1, U3, U5: one Configuration write with MOD = shutdown from the driver's copy, never a
  // read first, never the tail of another write.
  return bus_.WriteWord(reg::kConfiguration,
                        reg::CfgMod::Insert(detail::ConfigWord(config_), reg::kModShutdown));
}

template <I2cPort Port>
Result<void> TMP117<Port>::waitPowerUp(uint32_t timeout_ms) noexcept {
  detail::DelayUs(bus_.GetPort(), kPowerOnLoadUs);
  // The bound is the validated timeout, capped by the same maximum (KB-CPP20-058).
  const uint32_t capped_ms = (timeout_ms < kMaxTimeoutMs) ? timeout_ms : kMaxTimeoutMs;
  const uint32_t polls = ((capped_ms * 1000U) / kBusyPollUs) + 1U;
  uint32_t clear_reads = 0U;
  for (uint32_t poll = 0U; poll < polls; ++poll) {
    const auto word = readConfigWord();
    if (!word) {
      return fail(word.error, "tmp117: Configuration read failed during power-up");
    }
    if (detail::DecodeStatus(word.value).eeprom_busy) {
      clear_reads = 0U;
    } else {
      ++clear_reads;
    }
    // Two reads in a row: a line stuck at 0 shows "ready" once, a real chip stays ready.
    if (clear_reads >= 2U) {
      return Result<void>::Success();
    }
    detail::DelayUs(bus_.GetPort(), kBusyPollUs);
  }
  return fail(Error::Timeout, "tmp117: EEPROM_Busy did not clear");
}

template <I2cPort Port>
Result<uint16_t> TMP117<Port>::readIdentity() noexcept {
  const auto id = bus_.ReadWord(reg::kDeviceId);
  if (!id) {
    return id;
  }
  identity_mismatch_ =
      (static_cast<uint32_t>(id.value) & reg::kDeviceIdMask) != reg::kDeviceIdValue;
  if (identity_mismatch_) {
    detail::Log(bus_.GetPort(), LogLevel::Error, "tmp117: Device_ID is not DID 117h");
    return Result<uint16_t>::Failure(Error::DeviceIdMismatch);
  }
  return id;
}

template <I2cPort Port>
Result<void> TMP117<Port>::writeAll(const Config& config) noexcept {
  // Limits, offset, then the configuration (which may start conversions) last.
  const std::array<uint8_t, 4> pointers = {reg::kHighLimit, reg::kLowLimit, reg::kTempOffset,
                                           reg::kConfiguration};
  const std::array<uint16_t, 4> words = {
      detail::CountsWord(config.high_limit_mc), detail::CountsWord(config.low_limit_mc),
      detail::CountsWord(config.offset_mc), detail::ConfigWord(config)};
  for (std::size_t i = 0U; i < pointers.size(); ++i) {
    if (const auto r = bus_.WriteWord(pointers[i], words[i]); !r) {
      return fail(r.error, "tmp117: register write failed");
    }
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::readBackAll(const Config& config) noexcept {
  const std::array<uint8_t, 3> pointers = {reg::kHighLimit, reg::kLowLimit, reg::kTempOffset};
  const std::array<uint16_t, 3> words = {detail::CountsWord(config.high_limit_mc),
                                         detail::CountsWord(config.low_limit_mc),
                                         detail::CountsWord(config.offset_mc)};
  for (std::size_t i = 0U; i < pointers.size(); ++i) {
    const auto value = bus_.ReadWord(pointers[i]);
    if (!value) {
      return fail(value.error, "tmp117: read-back failed");
    }
    if (value.value != words[i]) {
      return fail(Error::BadReply, "tmp117: a register did not read back what was written");
    }
  }
  const auto word = readConfigWord();
  if (!word) {
    return fail(word.error, "tmp117: Configuration read-back failed");
  }
  const uint32_t mask = reg::kCfgWritableMask;
  if ((static_cast<uint32_t>(word.value) & mask) !=
      (static_cast<uint32_t>(detail::ConfigWord(config)) & mask)) {
    return fail(Error::BadReply, "tmp117: Configuration did not read back what was written");
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::Deinitialize() noexcept {
  return switchOff(false);
}

template <I2cPort Port>
Result<void> TMP117<Port>::resetAndStart(bool general_call) noexcept {
  initialized_ = false;
  one_shot_pending_ = false;
  const Result<void> sent = general_call ? bus_.GeneralCallReset()
                                         : bus_.WriteWord(reg::kConfiguration, reg::kCfgSoftReset);
  if (!sent) {
    return fail(sent.error, "tmp117: reset command failed");
  }
  detail::DelayUs(bus_.GetPort(), kSoftResetUs);
  if (auto r = startUp(); !r) {
    return r;
  }
  detail::Log(bus_.GetPort(), LogLevel::Info, "tmp117: reset and initialised");
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::SoftReset() noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  return resetAndStart(false);
}

template <I2cPort Port>
Result<void> TMP117<Port>::GeneralCallReset() noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  if (!config_.allow_general_call) {
    return Result<void>::Failure(Error::Unsupported); // bus-wide: needs the explicit opt-in
  }
  return resetAndStart(true);
}

template <I2cPort Port>
Result<void> TMP117<Port>::SmbusAlertResponse() noexcept {
  return Result<void>::Failure(Error::Unsupported);
}

// -------------------------------------------------------------------------------------------------
// State guards and evidence
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> TMP117<Port>::require() const noexcept {
  return initialized_ ? Result<void>::Success() : Result<void>::Failure(Error::NotInitialized);
}

template <I2cPort Port>
Result<void> TMP117<Port>::requireIdle() const noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  return one_shot_pending_ ? Result<void>::Failure(Error::Busy) : Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::prepare() noexcept {
  // Guards first, then any write in doubt is settled, so the copy a setter edits is the chip's.
  if (auto r = requireIdle(); !r) {
    return r;
  }
  return settle();
}

template <I2cPort Port>
Result<void> TMP117<Port>::fail(Error error, const char* message) noexcept {
  detail::Log(bus_.GetPort(), LogLevel::Error, message);
  return Result<void>::Failure(error);
}

template <I2cPort Port>
Result<void> TMP117<Port>::unexpectedReset() noexcept {
  // No write follows until EnsureInitialized() runs again (RD-01).
  initialized_ = false;
  one_shot_pending_ = false;
  one_shot_doubt_ = false;
  one_shot_dr_seen_ = false;
  one_shot_mod_stuck_ = false;
  doubt_mask_ = 0U;
  detail::Log(bus_.GetPort(), LogLevel::Warning, "tmp117: unexpected reset");
  return Result<void>::Failure(Error::UnexpectedReset);
}

template <I2cPort Port>
Result<uint16_t> TMP117<Port>::readConfigWord() noexcept {
  // Every read of Configuration goes through here: it clears three flags on the chip, so the driver
  // keeps them (RD-31). A reply that breaks a fixed bit is not decoded and not latched.
  const auto word = bus_.ReadWord(reg::kConfiguration);
  if (!word) {
    return word;
  }
  const uint32_t bits = word.value;
  if (((bits & reg::kCfgMustBeZero) != 0U) ||
      (reg::CfgMod::Extract(word.value) == reg::kModContinuousAlias)) {
    return Result<uint16_t>::Failure(Error::BadReply);
  }
  last_config_word_ = word.value;
  latched_flags_ =
      static_cast<uint16_t>(static_cast<uint32_t>(latched_flags_) | (bits & reg::kCfgClearOnRead));
  noteDataReady(word.value);
  if (one_shot_mod_stuck_ && !one_shot_pending_ &&
      (reg::CfgMod::Extract(word.value) == reg::kModOneShot)) {
    // A19: the chip finished a one-shot and still reads MOD 11. It is shutdown for the driver.
    return Result<uint16_t>::Success(reg::CfgMod::Insert(word.value, reg::kModShutdown));
  }
  return word;
}

template <I2cPort Port>
void TMP117<Port>::noteDataReady(uint16_t word) noexcept {
  // Data_Ready clears on the read that sees it: remember it for the one-shot that is pending (A19).
  if (one_shot_pending_ && ((static_cast<uint32_t>(word) & reg::kCfgDataReady) != 0U)) {
    one_shot_dr_seen_ = true;
  }
}

template <I2cPort Port>
Result<void> TMP117<Port>::checkEvidence(uint16_t word) noexcept {
  // The chip has no power-on flag. Evidence of a reset: the writable fields differ from the copy
  // the driver wrote, or the EEPROM load is running again (RD-01; ambiguity A9 for a reset that
  // reloads identical values).
  const uint32_t bits = word;
  const uint32_t expected = detail::ConfigWord(config_);
  if (((bits & reg::kCfgSettingsMask) != (expected & reg::kCfgSettingsMask)) ||
      ((bits & reg::kCfgEepromBusy) != 0U)) {
    return unexpectedReset();
  }
  const uint32_t mod = reg::CfgMod::Extract(word);
  if (one_shot_pending_) {
    // Running (11), or finished: back in shutdown (01) or still 11 (ambiguity A19). Only
    // continuous (00) is evidence of a reset: it is the chip's power-on mode.
    if ((mod != reg::kModOneShot) && (mod != reg::kModShutdown)) {
      return unexpectedReset();
    }
  } else if (mod != detail::ModBits(config_.mode)) {
    return unexpectedReset();
  } else {
    // The mode is as written.
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<uint16_t> TMP117<Port>::pollConfig() noexcept {
  if (auto s = settle(); !s) {
    return Result<uint16_t>::Failure(s.error);
  }
  const auto word = readConfigWord();
  if (!word) {
    return word;
  }
  if (word.value == reg::kTempResultAllZeros) {
    // The word a data line stuck at 0 returns, and the word of the all-zero configuration: only
    // Device_ID tells them apart, so the status is never decided on it alone (RD-03).
    if (const auto id = readIdentity(); !id) {
      return Result<uint16_t>::Failure(id.error);
    }
  }
  if (auto r = checkEvidence(word.value); !r) {
    return Result<uint16_t>::Failure(r.error);
  }
  const uint32_t mod = reg::CfgMod::Extract(word.value);
  if (one_shot_pending_ && ((mod == reg::kModShutdown) || one_shot_dr_seen_)) {
    // The one-shot is complete when Data_Ready was seen, or when MOD reads shutdown (A19).
    one_shot_mod_stuck_ = (mod == reg::kModOneShot);
    one_shot_pending_ = false;
    one_shot_dr_seen_ = false;
    config_.mode = Mode::Shutdown;
  }
  return word;
}

template <I2cPort Port>
Result<void> TMP117<Port>::settleLimit(uint8_t doubt_bit, uint8_t pointer, int32_t old_mc,
                                       int32_t new_mc) noexcept {
  if ((static_cast<uint32_t>(doubt_mask_) & doubt_bit) == 0U) {
    return Result<void>::Success();
  }
  const auto value = bus_.ReadWord(pointer);
  if (!value) {
    return Result<void>::Failure(value.error);
  }
  if (value.value == detail::CountsWord(old_mc)) {
    // The write did not take effect.
  } else if (value.value == detail::CountsWord(new_mc)) {
    adoptFields(doubt_bit, attempted_);
  } else {
    return unexpectedReset(); // neither the old nor the attempted value
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::settleConfig() noexcept {
  if ((static_cast<uint32_t>(doubt_mask_) & kDoubtConfig) == 0U) {
    return Result<void>::Success();
  }
  const auto word = readConfigWord();
  if (!word) {
    return Result<void>::Failure(word.error);
  }
  if ((static_cast<uint32_t>(word.value) & reg::kCfgEepromBusy) != 0U) {
    // The power-up load runs (or an EEPROM word is being programmed, which an initialised driver
    // never starts): the chip was reset, and a write in doubt was ignored by the load. The mask
    // below would drop this bit and read the reset values as "the write did not take effect".
    return unexpectedReset();
  }
  const uint32_t masked = static_cast<uint32_t>(word.value) & reg::kCfgWritableMask;
  const bool same_settings =
      (masked & reg::kCfgSettingsMask) ==
      (static_cast<uint32_t>(detail::ConfigWord(config_)) & reg::kCfgSettingsMask);
  if (masked == detail::ConfigWord(config_)) {
    // The write did not take effect.
  } else if (masked == detail::ConfigWord(attempted_)) {
    adoptFields(kDoubtConfig, attempted_);
  } else if (one_shot_pending_ && same_settings &&
             (reg::CfgMod::Extract(word.value) == reg::kModOneShot)) {
    // The pending one-shot still runs, or ended with MOD 11 (A19): the write did not take effect.
  } else {
    return unexpectedReset();
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::settleOneShot() noexcept {
  if (!one_shot_doubt_) {
    return Result<void>::Success();
  }
  const auto word = readConfigWord();
  if (!word) {
    return Result<void>::Failure(word.error);
  }
  const uint32_t bits = word.value;
  if ((bits & reg::kCfgEepromBusy) != 0U) {
    return unexpectedReset(); // the power-up load runs: see settleConfig()
  }
  const uint32_t mod = reg::CfgMod::Extract(word.value);
  if ((bits & reg::kCfgSettingsMask) !=
      (static_cast<uint32_t>(detail::ConfigWord(config_)) & reg::kCfgSettingsMask)) {
    return unexpectedReset();
  }
  if (mod == reg::kModOneShot) {
    one_shot_pending_ = true; // the conversion is running, or finished and MOD stayed 11 (A19)
    one_shot_mod_stuck_ = false;
    one_shot_dr_seen_ = (bits & reg::kCfgDataReady) != 0U;
  } else if ((mod == reg::kModShutdown) && (config_.mode == Mode::Continuous)) {
    config_.mode = Mode::Shutdown; // it already finished
  } else if (mod != detail::ModBits(config_.mode)) {
    return unexpectedReset();
  } else {
    // The start did not take effect.
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::settle() noexcept {
  if ((doubt_mask_ == 0U) && !one_shot_doubt_) {
    return Result<void>::Success();
  }
  // A write failed and may have taken effect: ask the chip before trusting either value. A value
  // that is neither the old nor the attempted one is a reset.
  if (auto r =
          settleLimit(kDoubtHigh, reg::kHighLimit, config_.high_limit_mc, attempted_.high_limit_mc);
      !r) {
    return r;
  }
  if (auto r =
          settleLimit(kDoubtLow, reg::kLowLimit, config_.low_limit_mc, attempted_.low_limit_mc);
      !r) {
    return r;
  }
  if (auto r = settleLimit(kDoubtOffset, reg::kTempOffset, config_.offset_mc, attempted_.offset_mc);
      !r) {
    return r;
  }
  if (auto r = settleConfig(); !r) {
    return r;
  }
  if (auto r = settleOneShot(); !r) {
    return r;
  }
  doubt_mask_ = 0U;
  one_shot_doubt_ = false;
  return Result<void>::Success();
}

template <I2cPort Port>
void TMP117<Port>::adoptFields(uint8_t doubt_bit, const Config& from) noexcept {
  switch (doubt_bit) {
  case kDoubtConfig:
    config_.mode = from.mode;
    config_.averaging = from.averaging;
    config_.cycle_time = from.cycle_time;
    config_.alert_function = from.alert_function;
    config_.alert_polarity = from.alert_polarity;
    config_.alert_source = from.alert_source;
    break;
  case kDoubtHigh:
    config_.high_limit_mc = from.high_limit_mc;
    break;
  case kDoubtLow:
    config_.low_limit_mc = from.low_limit_mc;
    break;
  case kDoubtOffset:
    config_.offset_mc = from.offset_mc;
    break;
  default:
    break;
  }
}

// -------------------------------------------------------------------------------------------------
// Configuration
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> TMP117<Port>::writeCommitted(uint8_t pointer, uint16_t word, uint8_t doubt_bit,
                                          const Config& next) noexcept {
  const auto r = bus_.WriteWord(pointer, word);
  if (!r) {
    // The write may have taken effect: keep the old copy, remember the attempt, ask the chip first
    // next time (RD-05).
    attempted_ = next;
    doubt_mask_ = static_cast<uint8_t>(static_cast<uint32_t>(doubt_mask_) | doubt_bit);
    return Result<void>::Failure(r.error);
  }
  adoptFields(doubt_bit, next); // the copy follows the chip only after the chip accepted
  doubt_mask_ = static_cast<uint8_t>(static_cast<uint32_t>(doubt_mask_) & ~uint32_t{doubt_bit});
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::setConfig(const Config& next, bool force_config) noexcept {
  if (auto s = settle(); !s) {
    return s;
  }
  return writeConfig(next, force_config);
}

template <I2cPort Port>
Result<void> TMP117<Port>::writeConfig(const Config& next, bool force_config) noexcept {
  // Whole-register writes from the validated copy, only the registers that change: limits, offset,
  // configuration. The two limits go in the order that never puts the low limit above the high
  // limit, in the chip or in the copy, if the second write fails (ambiguity A10): the new high
  // limit goes first when it is not below the old low limit, else the new low limit goes first
  // (one of the two always holds, as low <= high holds before and after).
  const bool high_first = next.high_limit_mc >= config_.low_limit_mc;
  for (uint32_t step = 0U; step < 2U; ++step) {
    const bool is_high = (step == 0U) == high_first;
    const int32_t from = is_high ? config_.high_limit_mc : config_.low_limit_mc;
    const int32_t to = is_high ? next.high_limit_mc : next.low_limit_mc;
    if (detail::CountsWord(to) != detail::CountsWord(from)) {
      const Result<void> r =
          is_high ? writeCommitted(reg::kHighLimit, detail::CountsWord(to), kDoubtHigh, next)
                  : writeCommitted(reg::kLowLimit, detail::CountsWord(to), kDoubtLow, next);
      if (!r) {
        return r;
      }
    }
  }
  if (detail::CountsWord(next.offset_mc) != detail::CountsWord(config_.offset_mc)) {
    if (auto r = writeCommitted(reg::kTempOffset, detail::CountsWord(next.offset_mc), kDoubtOffset,
                                next);
        !r) {
      return r;
    }
  }
  if (force_config || one_shot_pending_ ||
      (detail::ConfigWord(next) != detail::ConfigWord(config_))) {
    if (auto r = writeCommitted(reg::kConfiguration, detail::ConfigWord(next), kDoubtConfig, next);
        !r) {
      return r;
    }
    one_shot_pending_ = false; // the MOD field was just written: no one-shot runs any more
    one_shot_doubt_ = false;
    one_shot_dr_seen_ = false;
    one_shot_mod_stuck_ = false;
  }
  // Limits and offset that map to the same register word still update the copy's millidegrees.
  config_.high_limit_mc = next.high_limit_mc;
  config_.low_limit_mc = next.low_limit_mc;
  config_.offset_mc = next.offset_mc;
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::Configure(const Config& config) noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  if (Validate(config) != Error::Ok) {
    return Result<void>::Failure(Error::InvalidArgument); // nothing sent
  }
  if (one_shot_pending_) {
    return Result<void>::Failure(Error::Busy);
  }
  // The driver-side fields (timeouts, opt-in) take effect when the call succeeds; the chip fields
  // are committed one register at a time.
  const Config next = config;
  const Result<void> done = setConfig(next, false);
  if (done) {
    config_.reset_timeout_ms = config.reset_timeout_ms;
    config_.eeprom_timeout_ms = config.eeprom_timeout_ms;
    config_.allow_general_call = config.allow_general_call;
  }
  return done;
}

template <I2cPort Port>
Result<void> TMP117<Port>::SetMode(Mode mode) noexcept {
  if (!detail::IsEnumerator(mode)) {
    return require().ok() ? Result<void>::Failure(Error::InvalidArgument) : require();
  }
  if (mode == Mode::Shutdown) {
    return switchOff(true); // the switch-off decision table, docs/contract.md
  }
  if (auto r = require(); !r) {
    return r;
  }
  if (auto r = prepare(); !r) {
    return r;
  }
  Config next = config_;
  next.mode = mode;
  return setConfig(next, false);
}

template <I2cPort Port>
Result<void> TMP117<Port>::SetAveraging(Averaging averaging) noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  if (!detail::IsEnumerator(averaging)) {
    return Result<void>::Failure(Error::InvalidArgument); // nothing sent
  }
  if (auto r = prepare(); !r) {
    return r;
  }
  Config next = config_;
  next.averaging = averaging;
  return setConfig(next, false);
}

template <I2cPort Port>
Result<void> TMP117<Port>::SetCycleTime(CycleTime cycle_time) noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  if (!detail::IsEnumerator(cycle_time)) {
    return Result<void>::Failure(Error::InvalidArgument); // nothing sent
  }
  if (auto r = prepare(); !r) {
    return r;
  }
  Config next = config_;
  next.cycle_time = cycle_time;
  return setConfig(next, false);
}

template <I2cPort Port>
Result<void> TMP117<Port>::SetAlertFunction(AlertFunction function) noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  if (!detail::IsEnumerator(function)) {
    return Result<void>::Failure(Error::InvalidArgument); // nothing sent
  }
  if (auto r = prepare(); !r) {
    return r;
  }
  Config next = config_;
  next.alert_function = function;
  return setConfig(next, false);
}

template <I2cPort Port>
Result<void> TMP117<Port>::SetAlertPolarity(AlertPolarity polarity) noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  if (!detail::IsEnumerator(polarity)) {
    return Result<void>::Failure(Error::InvalidArgument); // nothing sent
  }
  if (auto r = prepare(); !r) {
    return r;
  }
  Config next = config_;
  next.alert_polarity = polarity;
  return setConfig(next, false);
}

template <I2cPort Port>
Result<void> TMP117<Port>::SetAlertSource(AlertSource source) noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  if (!detail::IsEnumerator(source)) {
    return Result<void>::Failure(Error::InvalidArgument); // nothing sent
  }
  if (auto r = prepare(); !r) {
    return r;
  }
  Config next = config_;
  next.alert_source = source;
  return setConfig(next, false);
}

template <I2cPort Port>
Result<void> TMP117<Port>::SetLimits(int32_t low_mc, int32_t high_mc) noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  Config probe{}; // the limits are checked on their own: range, and low not above high
  probe.low_limit_mc = low_mc;
  probe.high_limit_mc = high_mc;
  if (Validate(probe) != Error::Ok) {
    return Result<void>::Failure(Error::InvalidArgument); // nothing sent
  }
  if (auto r = prepare(); !r) {
    return r;
  }
  Config next = config_;
  next.low_limit_mc = low_mc;
  next.high_limit_mc = high_mc;
  return setConfig(next, false);
}

template <I2cPort Port>
Result<void> TMP117<Port>::SetOffsetMilliC(int32_t offset_mc) noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  Config probe{};
  probe.offset_mc = offset_mc;
  if (Validate(probe) != Error::Ok) {
    return Result<void>::Failure(Error::InvalidArgument); // nothing sent
  }
  if (auto r = prepare(); !r) {
    return r;
  }
  Config next = config_;
  next.offset_mc = offset_mc;
  return setConfig(next, false);
}

// -------------------------------------------------------------------------------------------------
// Measurement
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<int16_t> TMP117<Port>::ReadTemperatureRaw(Verify verify) noexcept {
  if (auto r = requireIdle(); !r) {
    return Result<int16_t>::Failure(r.error);
  }
  if (!detail::IsEnumerator(verify)) {
    return Result<int16_t>::Failure(Error::InvalidArgument);
  }
  if (verify == Verify::Device) {
    const auto id = readIdentity(); // a line stuck at 0 or 1 cannot pass this
    if (!id) {
      return Result<int16_t>::Failure(id.error);
    }
  }
  const auto word = bus_.ReadWord(reg::kTempResult);
  if (!word) {
    return Result<int16_t>::Failure(word.error);
  }
  if (word.value == reg::kTempResultReset) {
    // 8000h is -256 degC: never a temperature. Before the first result it means "no conversion
    // yet"; after one it is evidence of a reset (section 7.3.1, p. 12; ambiguity A7).
    if (have_result_) {
      const auto r = unexpectedReset();
      return Result<int16_t>::Failure(r.error);
    }
    return Result<int16_t>::Failure(Error::NoMeasurement);
  }
  if ((verify == Verify::None) &&
      ((word.value == reg::kTempResultAllZeros) || (word.value == reg::kTempResultAllOnes))) {
    // The two words a data line stuck at 0 or 1 returns. A real chip also reads them (0 degC,
    // -1 count), so Device_ID decides: one more transfer only for these two values.
    const auto id = readIdentity();
    if (!id) {
      return Result<int16_t>::Failure(id.error);
    }
  }
  have_result_ = true;
  return Result<int16_t>::Success(detail::AsSigned16(word.value));
}

template <I2cPort Port>
Result<int32_t> TMP117<Port>::ReadTemperatureMilliC(Verify verify) noexcept {
  const auto raw = ReadTemperatureRaw(verify);
  if (!raw) {
    return Result<int32_t>::Failure(raw.error);
  }
  return Result<int32_t>::Success(CountsToMilliC(raw.value));
}

template <I2cPort Port>
Result<void> TMP117<Port>::StartOneShot() noexcept {
  if (auto r = requireIdle(); !r) {
    return r;
  }
  // Read Configuration first: it clears a stale Data_Ready (section 7.6.3), so the flag seen later
  // belongs to this conversion, and it checks that the chip was not reset.
  const auto word = pollConfig();
  if (!word) {
    return Result<void>::Failure(word.error);
  }
  const uint16_t start = reg::CfgMod::Insert(detail::ConfigWord(config_), reg::kModOneShot);
  if (const auto w = bus_.WriteWord(reg::kConfiguration, start); !w) {
    one_shot_doubt_ = true; // the conversion may have started: ask the chip next time
    return Result<void>::Failure(w.error);
  }
  one_shot_pending_ = true;
  one_shot_dr_seen_ = false;
  one_shot_mod_stuck_ = false;
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::WaitDataReady(uint32_t timeout_ms) noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  if ((timeout_ms == 0U) || (timeout_ms > kMaxWaitMs)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  const uint32_t polls =
      timeout_ms + 1U; // one poll per millisecond, bounded by the validated value
  const bool was_pending = one_shot_pending_;
  for (uint32_t poll = 0U; poll < polls; ++poll) {
    const auto word = pollConfig();
    if (!word) {
      return Result<void>::Failure(word.error);
    }
    // Complete: Data_Ready set on this read, or the pending one-shot ended (A19: MOD shows
    // shutdown, or Data_Ready was seen by an earlier read of the call).
    if (detail::DecodeStatus(word.value).data_ready || (was_pending && !one_shot_pending_)) {
      return Result<void>::Success();
    }
    detail::DelayUs(bus_.GetPort(), kDataReadyPollUs);
  }
  return Result<void>::Failure(Error::Timeout);
}

template <I2cPort Port>
Result<int32_t> TMP117<Port>::ReadOneShotMilliC() noexcept {
  if (auto r = StartOneShot(); !r) {
    return Result<int32_t>::Failure(r.error);
  }
  // The wait comes from the AVG setting actually in use (KB-CPP-128), plus one conversion of
  // margin.
  const uint32_t wait_ms = (GetOneShotWaitUs(config_.averaging) / 1000U) + 1U;
  if (const auto r = WaitDataReady(wait_ms); !r) {
    if (r.error == Error::Timeout) {
      if (const auto off = SetMode(Mode::Shutdown); !off) {
        detail::Log(bus_.GetPort(), LogLevel::Error, "tmp117: shutdown after a timeout failed");
      }
    }
    return Result<int32_t>::Failure(r.error);
  }
  return ReadTemperatureMilliC(Verify::None);
}

template <I2cPort Port>
Result<Status> TMP117<Port>::ReadStatus() noexcept {
  if (auto r = require(); !r) {
    return Result<Status>::Failure(r.error);
  }
  const auto word = pollConfig();
  if (!word) {
    return Result<Status>::Failure(word.error);
  }
  return Result<Status>::Success(detail::DecodeStatus(word.value));
}

template <I2cPort Port>
Result<bool> TMP117<Port>::ReadAlertPin() noexcept {
  if (auto r = require(); !r) {
    return Result<bool>::Failure(r.error);
  }
  if constexpr (detail::HasReadAlertPin<Port>) {
    bool level = false;
    if (!bus_.GetPort().ReadAlertPin(level)) {
      return Result<bool>::Failure(Error::Bus);
    }
    const bool active_high = config_.alert_polarity == AlertPolarity::ActiveHigh;
    return Result<bool>::Success(level == active_high); // POL decides which level is "active"
  } else {
    return Result<bool>::Failure(Error::Unsupported);
  }
}

// -------------------------------------------------------------------------------------------------
// EEPROM (section 7.5.1, p. 18)
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<uint16_t> TMP117<Port>::readUnlock(bool expect_unlocked) noexcept {
  const auto word = bus_.ReadWord(reg::kEepromUnlock);
  if (!word) {
    return word;
  }
  const uint32_t bits = word.value;
  if ((bits & reg::kUnlockMustBeZero) != 0U) {
    return Result<uint16_t>::Failure(Error::BadReply); // bits 13:0 always read 0
  }
  const bool unlocked = (bits & reg::kUnlockEun) != 0U;
  if (unlocked != expect_unlocked) {
    return Result<uint16_t>::Failure(expect_unlocked ? Error::BadReply : Error::EepromVerifyFailed);
  }
  return word;
}

template <I2cPort Port>
Result<void> TMP117<Port>::waitEepromIdle() noexcept {
  // Programming one word takes 7 ms typically and no maximum is printed (ambiguity A2): wait that,
  // then poll EEPROM_Busy until the validated deadline.
  detail::DelayUs(bus_.GetPort(), kEepromProgramUs);
  const uint32_t capped_ms =
      (config_.eeprom_timeout_ms < kMaxTimeoutMs) ? config_.eeprom_timeout_ms : kMaxTimeoutMs;
  const uint32_t polls = capped_ms + 1U;
  for (uint32_t poll = 0U; poll < polls; ++poll) {
    const auto word = readUnlock(true); // EUN must still read 1 while the words are written
    if (!word) {
      return Result<void>::Failure(word.error);
    }
    if ((static_cast<uint32_t>(word.value) & reg::kUnlockBusy) == 0U) {
      return Result<void>::Success();
    }
    detail::DelayUs(bus_.GetPort(), kEepromPollUs);
  }
  return fail(Error::Timeout, "tmp117: EEPROM programming did not finish");
}

template <I2cPort Port>
Result<void> TMP117<Port>::unlockAndWrite(const std::array<uint8_t, 4>& pointers,
                                          const std::array<uint16_t, 4>& words,
                                          uint32_t count) noexcept {
  // Avoid conversions while unlocked (section 7.5.1.2, p. 18): shutdown first. The Configuration
  // word of the defaults comes last; with a continuous MOD the chip converts from it on (A20).
  const uint16_t off = reg::CfgMod::Insert(detail::ConfigWord(config_), reg::kModShutdown);
  if (const auto r = bus_.WriteWord(reg::kConfiguration, off); !r) {
    return fail(r.error, "tmp117: shutdown before programming failed");
  }
  if (const auto r = bus_.WriteWord(reg::kEepromUnlock, reg::kUnlockEun); !r) {
    return fail(r.error, "tmp117: EEPROM unlock failed");
  }
  if (const auto r = readUnlock(true); !r) {
    return fail(r.error, "tmp117: EUN did not read back as set");
  }
  const uint32_t bounded = (count < 4U) ? count : 4U;
  for (uint32_t i = 0U; i < bounded; ++i) {
    if (const auto r = bus_.WriteWord(pointers[i], words[i]); !r) {
      return fail(r.error, "tmp117: EEPROM word write failed");
    }
    if (const auto r = waitEepromIdle(); !r) {
      return r;
    }
  }
  return Result<void>::Success();
}

template <I2cPort Port>
void TMP117<Port>::waitEepromBusyClear() noexcept {
  // A write while EEPROM_Busy is set is ignored (section 7.5.1.2, p. 18): let a word in flight end
  // before the lock is written. Raw reads, bounded; whatever they return, the lock follows.
  const uint32_t capped_ms =
      (config_.eeprom_timeout_ms < kMaxTimeoutMs) ? config_.eeprom_timeout_ms : kMaxTimeoutMs;
  const uint32_t polls = capped_ms + 1U;
  for (uint32_t poll = 0U; poll < polls; ++poll) {
    const auto word = bus_.ReadWord(reg::kEepromUnlock);
    if (word && ((static_cast<uint32_t>(word.value) & reg::kUnlockBusy) == 0U)) {
      return;
    }
    detail::DelayUs(bus_.GetPort(), kEepromPollUs);
  }
}

template <I2cPort Port>
bool TMP117<Port>::tryLock(Error& first) noexcept {
  // Start-up and the relock after programming: write EUN = 0 and read it back, a bounded number of
  // times; the frame is idempotent. `first` is the error of the first failed write, for the
  // caller's report. Only a clean read-back (bits 13:0 zero) with EUN = 0 confirms the lock.
  bool locked = false;
  for (uint32_t attempt = 0U; attempt < kLockAttempts; ++attempt) {
    waitEepromBusyClear();
    if (const auto w = bus_.WriteWord(reg::kEepromUnlock, reg::kEepromUnlockReset); !w) {
      first = w.error;
    }
    locked = classifyUnlock(bus_.ReadWord(reg::kEepromUnlock)) == UnlockSeen::Locked;
    if (locked) {
      break;
    }
  }
  return locked;
}

template <I2cPort Port>
Result<void> TMP117<Port>::relock() noexcept {
  // Runs on every path, including a failed write, a timeout and a bad reply. Writing EUN = 0 locks
  // the EEPROM on the chip's account (ambiguity A6); a read confirms it, and the idempotent write
  // is sent again, a bounded number of times, when it did not. Only a confirmed lock lets a soft
  // reset be written to Configuration (with EUN set it would be programmed into EEPROM). A lock
  // that did not take needs the general-call reset, which the opt-in allows (section 7.5.1.2, p.
  // 18); without it only a power cycle locks.
  Error first = Error::Ok;
  const bool locked = tryLock(first);
  if (!locked && !config_.allow_general_call) {
    return fail((first != Error::Ok) ? first : Error::EepromVerifyFailed,
                "tmp117: the EEPROM could not be locked: power-cycle the chip");
  }
  const Result<void> sent =
      locked ? bus_.WriteWord(reg::kConfiguration, reg::kCfgSoftReset) : bus_.GeneralCallReset();
  if (!sent) {
    return fail(sent.error, "tmp117: reset after programming failed");
  }
  detail::DelayUs(bus_.GetPort(), kSoftResetUs);
  if (auto r = waitPowerUp(config_.reset_timeout_ms); !r) {
    return r;
  }
  if (const auto r = readUnlock(false); !r) {
    return fail(r.error, "tmp117: EUN is still set after the reset");
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::verifyWords(const std::array<uint8_t, 4>& pointers,
                                       const std::array<uint16_t, 4>& words,
                                       uint32_t count) noexcept {
  // Read back what the reset reloaded from EEPROM and compare (7.5.1.1: the
  // load takes the same path as a power-up).
  const uint32_t bounded = (count < 4U) ? count : 4U;
  for (uint32_t i = 0U; i < bounded; ++i) {
    const auto value = bus_.ReadWord(pointers[i]);
    if (!value) {
      return Result<void>::Failure(value.error);
    }
    // MOD1 is not stored in EEPROM, and the flag bits are the chip's own.
    const uint32_t mask = (pointers[i] == reg::kConfiguration) ? reg::kCfgEepromMask : 0xFFFFU;
    if ((static_cast<uint32_t>(value.value) & mask) != (static_cast<uint32_t>(words[i]) & mask)) {
      return fail(Error::EepromVerifyFailed, "tmp117: EEPROM word differs after the reset");
    }
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::verifyTraceability(uint16_t id1, uint16_t id3) noexcept {
  const auto after1 = bus_.ReadWord(reg::kEeprom1);
  if (!after1) {
    return Result<void>::Failure(after1.error);
  }
  const auto after3 = bus_.ReadWord(reg::kEeprom3);
  if (!after3) {
    return Result<void>::Failure(after3.error);
  }
  if ((after1.value != id1) || (after3.value != id3)) {
    return fail(Error::EepromVerifyFailed, "tmp117: a traceability word changed");
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::program(const std::array<uint8_t, 4>& pointers,
                                   const std::array<uint16_t, 4>& words, uint32_t count) noexcept {
  // The traceability words must come out of the operation unchanged.
  const auto id1 = bus_.ReadWord(reg::kEeprom1);
  if (!id1) {
    return Result<void>::Failure(id1.error);
  }
  const auto id3 = bus_.ReadWord(reg::kEeprom3);
  if (!id3) {
    return Result<void>::Failure(id3.error);
  }
  const Config running = config_;
  initialized_ = false; // after any error the application starts the driver again
  one_shot_doubt_ = false;
  doubt_mask_ = 0U;
  const Result<void> written = unlockAndWrite(pointers, words, count);
  const Result<void> locked = relock(); // always, whatever happened above
  if (!written) {
    return written;
  }
  if (!locked) {
    return locked;
  }
  if (auto r = verifyWords(pointers, words, count); !r) {
    return r;
  }
  if (auto r = verifyTraceability(id1.value, id3.value); !r) {
    return r;
  }
  // Put the running configuration back over the freshly loaded defaults.
  if (const auto id = readIdentity(); !id) {
    return Result<void>::Failure(id.error);
  }
  if (auto r = writeAll(running); !r) {
    return r;
  }
  if (auto r = readBackAll(running); !r) {
    return r;
  }
  config_ = running;
  one_shot_pending_ = false;
  one_shot_dr_seen_ = false;
  one_shot_mod_stuck_ = false;
  have_result_ = false;
  initialized_ = true;
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> TMP117<Port>::ProgramPowerOnDefaults(const Config& defaults) noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  if (Validate(defaults) != Error::Ok) {
    return Result<void>::Failure(Error::InvalidArgument); // nothing sent
  }
  if (one_shot_pending_) {
    return Result<void>::Failure(Error::Busy);
  }
  if (auto s = settle(); !s) {
    return s;
  }
  const std::array<uint8_t, 4> pointers = {reg::kHighLimit, reg::kLowLimit, reg::kTempOffset,
                                           reg::kConfiguration};
  const std::array<uint16_t, 4> words = {
      detail::CountsWord(defaults.high_limit_mc), detail::CountsWord(defaults.low_limit_mc),
      detail::CountsWord(defaults.offset_mc), detail::ConfigWord(defaults)};
  return program(pointers, words, 4U);
}

template <I2cPort Port>
Result<void> TMP117<Port>::ProgramUserWord(uint16_t value) noexcept {
  if (auto r = requireIdle(); !r) {
    return r;
  }
  if (auto s = settle(); !s) {
    return s;
  }
  const std::array<uint8_t, 4> pointers = {reg::kEeprom2, reg::kEeprom2, reg::kEeprom2,
                                           reg::kEeprom2};
  const std::array<uint16_t, 4> words = {value, value, value, value};
  return program(pointers, words, 1U);
}

template <I2cPort Port>
Result<UniqueId> TMP117<Port>::ReadUniqueId() noexcept {
  if (auto r = require(); !r) {
    return Result<UniqueId>::Failure(r.error);
  }
  const auto w1 = bus_.ReadWord(reg::kEeprom1);
  if (!w1) {
    return Result<UniqueId>::Failure(w1.error);
  }
  const auto w2 = bus_.ReadWord(reg::kEeprom2);
  if (!w2) {
    return Result<UniqueId>::Failure(w2.error);
  }
  const auto w3 = bus_.ReadWord(reg::kEeprom3);
  if (!w3) {
    return Result<UniqueId>::Failure(w3.error);
  }
  const bool uniform = (w1.value == w2.value) && (w2.value == w3.value);
  if (uniform &&
      ((w1.value == reg::kTempResultAllZeros) || (w1.value == reg::kTempResultAllOnes))) {
    // Three identical words of 0000h or FFFFh are what a stuck line returns: Device_ID decides.
    if (const auto id = readIdentity(); !id) {
      return Result<UniqueId>::Failure(id.error);
    }
  }
  return Result<UniqueId>::Success(UniqueId{w1.value, w2.value, w3.value});
}

// -------------------------------------------------------------------------------------------------
// Diagnostics
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> TMP117<Port>::CheckHealth() noexcept {
  if (auto r = require(); !r) {
    return r;
  }
  const auto id = readIdentity();
  if (!id) {
    if (id.error == Error::DeviceIdMismatch) {
      initialized_ = false; // another chip, or none: nothing is written until the next start
    }
    return Result<void>::Failure(id.error);
  }
  const auto word = pollConfig(); // flags it reads reach the latch
  if (!word) {
    return Result<void>::Failure(word.error);
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<uint16_t> TMP117<Port>::ReadRegister(uint8_t pointer) noexcept {
  if (auto r = require(); !r) {
    return Result<uint16_t>::Failure(r.error);
  }
  if (!reg::IsDefinedPointer(pointer)) {
    return Result<uint16_t>::Failure(Error::InvalidArgument); // nothing sent
  }
  if (one_shot_pending_ && (pointer == reg::kTempResult)) {
    // A read of Temp_Result clears Data_Ready, which the driver would not see (ambiguity A19).
    return Result<uint16_t>::Failure(Error::Busy); // nothing sent
  }
  const auto word = bus_.ReadWord(pointer);
  if (word && (pointer == reg::kConfiguration)) {
    noteDataReady(word.value);
    // Raw access, no decoding, but the clear-on-read flags still reach the latch.
    last_config_word_ = word.value;
    latched_flags_ =
        static_cast<uint16_t>(static_cast<uint32_t>(latched_flags_) |
                              (static_cast<uint32_t>(word.value) & reg::kCfgClearOnRead));
  }
  return word;
}

} // namespace tmp117

#endif // TMP117_IPP
