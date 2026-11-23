/**
 * @file fuzz_tmp117.cpp
 * @brief Fuzz target: the driver over a bus whose behaviour the fuzzer controls.
 *
 * What the driver reads comes from the chip over the bus, so read data is the untrusted input. The
 * fuzzer's bytes choose a configuration, then a sequence of driver calls; on every transfer they
 * also choose what the bus does: pass it to the chip model, fail it, acknowledge and lose a write,
 * flip bits of the data read or replace it. They can also change the chip: a wrong Device_ID, a
 * power cycle, a stuck conversion or EEPROM, any temperature, a stuck line.
 *
 * After every call the target checks the driver's promises (docs/contract.md), and aborts, naming
 * the line, when one breaks:
 *
 * - a failed call returns an `Error` the enum defines, and `T{}`;
 * - before `EnsureInitialized()` succeeds, no call but `Deinitialize()` and the resets makes a
 *   transfer: they return `NotInitialized`;
 * - `InvalidArgument` means nothing was sent;
 * - a temperature is inside the range of the 16-bit result, -256 degC to +255.99 degC;
 * - `UnexpectedReset` leaves the driver uninitialised;
 * - the driver never writes Temp_Result, Device_ID, EEPROM1 or EEPROM3, or a pointer that is not in
 *   the map, and never sends the general-call reset without the opt-in;
 * - `SetMode(Shutdown)` on an initialised driver always puts a Configuration write with MOD 01 on
 *   the bus, whatever the settle reads returned (RD-05), unless it was refused for an unlocked
 *   EEPROM (`EepromVerifyFailed`), for another device (row N2), or the settle found reset
 *   evidence (row I3: the next rule applies);
 * - `SetMode(Shutdown)` and `Deinitialize()` of an uninitialised driver that has touched the chip
 *   (the switch-off decision table, docs/contract.md) put a Configuration write with MOD 01 on the
 *   bus unless the EEPROM_UL reply the driver received after its last lock write was clean with
 *   EUN = 1 (U4), the read before the first lock write and that write both failed (U2), the last
 *   EEPROM_UL reply was clean with EEPROM_Busy set and the call returned `Timeout` (U6), or
 *   another device answered Device_ID (N2). No exclusion: checked whatever the reads return,
 *   failed, garbled or altered. When a U4 refusal came with no altered reply in the call, the chip
 *   model's true EUN is 1, and when an N2 refusal did, the model's Device_ID is not 117h;
 * - in a switch-off call no write follows an EEPROM_UL reply that the driver received clean with
 *   EEPROM_Busy set (the power-up load ignores writes, programming forbids them);
 * - the effect: a switch-off that returns `Ok` leaves the chip model in shutdown (MOD 01). Outside
 *   it only the documented residual risks: a write of the call lost by the bus or NACKed by the
 *   model, a read reply altered in the call, an initialised switch-off (rows I1 and I2, no read
 *   precedes the write, or none showed EEPROM_Busy) whose write the chip ignored: its power-up
 *   load after a reset no read has shown (A14), or an EEPROM that stays busy; a call in which a
 *   read showed EEPROM_Busy is never excluded; and row U5 (no clean EEPROM_UL reply after the last
 *   lock write: the write is sent blind);
 * - while no read reply was altered, no EEPROM word is programmed outside
 *   `ProgramPowerOnDefaults()` and `ProgramUserWord()`: start-up and every setter leave an unlocked
 *   chip locked or refuse. The one admitted exception (A21, residual risk): a switch-off whose
 *   last lock write was not read back clean sends the shutdown write, which programs a word when
 *   the lock did not take;
 * - every log line is a non-empty plain-text message.
 *
 * Built two ways (tests/fuzz/CMakeLists.txt): with libFuzzer (Clang, `-DTMP117_BUILD_FUZZERS=ON`),
 * and as a plain executable that replays the corpus in tests/fuzz/corpus/ as a CTest on any
 * compiler.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mock_tmp117.hpp"
#include "mock_tmp117_bus.hpp"
#include "tmp117.hpp"

namespace {

using tmp117::Error;

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Input {
public:
  Input(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

  [[nodiscard]] bool Empty() const noexcept {
    return pos_ >= size_;
  }
  uint8_t Byte() noexcept {
    return pos_ < size_ ? data_[pos_++] : uint8_t{0};
  }
  uint16_t Word() noexcept {
    const auto high = Byte();
    return static_cast<uint16_t>((static_cast<uint32_t>(high) << 8U) | Byte());
  }
  int32_t Milli() noexcept {
    // Mostly inside the range of the chip, sometimes far outside it.
    const int32_t v = static_cast<int32_t>(tmp117::detail::AsSigned16(Word())) * 8;
    return (Byte() & 0x0FU) == 0U ? v * 40 : v;
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

/// A broken promise: name the line and abort, so libFuzzer saves the input as a crash.
void Check(bool promise, int line = __builtin_LINE()) noexcept {
  if (!promise) {
    (void)std::fprintf(stderr, "fuzz_tmp117: promise broken at line %d\n", line);
    std::abort();
  }
}

constexpr uint8_t kFail = 0xFF;    ///< The transfer fails; it never reached the chip.
constexpr uint8_t kFlip = 0xFE;    ///< Data read: bits flipped. A write: acknowledged and lost.
constexpr uint8_t kReplace = 0xFD; ///< The data read is replaced (another device answered).
constexpr uint8_t kLostAck = 0xFC; ///< A write: stored by the chip, reported failed (ACK lost).

/// The port: the chip model of the host tests behind a bus the fuzzer controls.
class FuzzPort : public tmp117::I2cInterface<FuzzPort> {
public:
  explicit FuzzPort(Input& in) noexcept : in_(in) {}

  [[nodiscard]] bool EnsureInitialized() const noexcept {
    return true;
  }

  /// What the driver received in reply to the EEPROM_UL read that followed its last lock write.
  enum class AfterLock : uint8_t { NoLock, NotRead, Failed, Garbled, CleanEun0, CleanEun1 };

  /// Starts the per-call record of the switch-off checks.
  void BeginCall(bool switch_off) noexcept {
    after_lock_ = AfterLock::NoLock;
    switch_off_ = switch_off;
    busy_reply_ = false;
    busy_shown_ = false;
    last_ul_busy_ = false;
    last_read_ok_ = true;
    no_answer_ = false;
  }

  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    Check(data != nullptr && (len == 3U || (len == 1U && addr == 0U)));
    Check(addr == tmp117::kI2cAddress || addr == 0U);
    Check(!switch_off_ || !busy_reply_); // rows W1, W2, U6: no write after a clean busy reply
    const bool lock = (data != nullptr) && (len == 3U) && (data[0] == tmp117::reg::kEepromUnlock) &&
                      ((static_cast<uint32_t>(data[1]) & 0x80U) == 0U); // EUN = 0
    const bool first_lock = lock && (after_lock_ == AfterLock::NoLock);
    if (lock) {
      after_lock_ = AfterLock::NotRead;
    }
    const bool acked = write(addr, data, len);
    if (first_lock) {
      no_answer_ = !last_read_ok_ && !acked; // U2: the read before it and the lock both failed
    }
    return acked;
  }

  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    const bool ok = writeRead(addr, tx, tx_len, rx, rx_len);
    last_read_ok_ = ok;
    const uint32_t word = (static_cast<uint32_t>(rx[0]) << 8U) | static_cast<uint32_t>(rx[1]);
    const bool clean_busy = ok && ((word & tmp117::reg::kUnlockMustBeZero) == 0U) &&
                            ((word & tmp117::reg::kUnlockBusy) != 0U);
    busy_reply_ = (tx[0] == tmp117::reg::kEepromUnlock) && clean_busy;
    // A reply the driver received with EEPROM_Busy set: Configuration bit 12 or EEPROM_UL bit 14.
    if (ok &&
        (((tx[0] == tmp117::reg::kConfiguration) && ((word & tmp117::reg::kCfgEepromBusy) != 0U)) ||
         ((tx[0] == tmp117::reg::kEepromUnlock) && ((word & tmp117::reg::kUnlockBusy) != 0U)))) {
      busy_shown_ = true;
    }
    if (tx[0] == tmp117::reg::kEepromUnlock) {
      last_ul_busy_ = clean_busy;
    }
    if ((tx[0] == tmp117::reg::kEepromUnlock) && (after_lock_ != AfterLock::NoLock)) {
      after_lock_ = ok ? classify(word) : AfterLock::Failed;
    }
    return ok;
  }

  /// A read of the call returned a reply with EEPROM_Busy set (the driver saw a load or a program).
  [[nodiscard]] bool BusyShown() const noexcept {
    return busy_shown_;
  }

  /// The last EEPROM_UL reply of the call was clean with EEPROM_Busy set.
  [[nodiscard]] bool LastUnlockBusy() const noexcept {
    return last_ul_busy_;
  }

  [[nodiscard]] AfterLock AfterLastLock() const noexcept {
    return after_lock_;
  }
  /// Row U2: the read just before the first lock write of the call failed, and so did that write.
  [[nodiscard]] bool NoAnswer() const noexcept {
    return no_answer_;
  }

private:
  static AfterLock classify(uint32_t word) noexcept {
    if ((word & tmp117::reg::kUnlockMustBeZero) != 0U) {
      return AfterLock::Garbled;
    }
    return ((word & tmp117::reg::kUnlockEun) != 0U) ? AfterLock::CleanEun1 : AfterLock::CleanEun0;
  }

  bool write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    if ((len == 3U) && (data[0] == tmp117::reg::kConfiguration)) {
      const uint32_t mod = tmp117::reg::CfgMod::Extract(static_cast<uint16_t>(
          (static_cast<uint32_t>(data[1]) << 8U) | static_cast<uint32_t>(data[2])));
      if (mod == tmp117::reg::kModShutdown) {
        ++shutdown_writes_;
      }
    }
    const uint8_t action = next();
    if (action == kFail) {
      ++lost_writes_;
      return false;
    }
    if (action == kFlip) {
      ++lost_writes_;
      return true; // acknowledged, never stored
    }
    const bool acked = bus_.DoWrite(addr, data, len);
    if (!acked) {
      ++lost_writes_; // NACKed by the model
    }
    return acked && (action != kLostAck); // kLostAck: stored, the ACK lost
  }

  bool writeRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    Check(addr == tmp117::kI2cAddress && tx != nullptr && rx != nullptr && tx_len == 1U &&
          rx_len == 2U);
    const uint8_t action = next();
    if (action == kFail || !bus_.DoWriteRead(addr, tx, tx_len, rx, rx_len)) {
      return false;
    }
    if ((action == kFlip) || (action == kReplace)) {
      ++altered_reads_;
    }
    for (std::size_t i = 0; i < rx_len; ++i) {
      if (action == kFlip) {
        rx[i] = static_cast<uint8_t>(rx[i] ^ in_.Byte());
      } else if (action == kReplace) {
        rx[i] = in_.Byte();
      } else {
        // Pass the byte through unchanged.
      }
    }
    return true;
  }

public:
  void DelayUs(uint32_t us) noexcept {
    bus_.chip.Elapse(us);
  }

  bool ReadAlertPin(bool& level) noexcept {
    if (next() == kFail) {
      return false;
    }
    level = bus_.chip.AlertPinHigh();
    return true;
  }

  void Log(tmp117::LogLevel level, const char* message) noexcept {
    (void)level;
    Check(message != nullptr && message[0] != '\0');
  }

  tmp117_test::Tmp117Model& Chip() noexcept {
    return bus_.chip;
  }
  [[nodiscard]] int Transfers() const noexcept {
    return bus_.transfers;
  }
  /// Configuration writes with MOD 01 that reached the bus (acknowledged or not).
  [[nodiscard]] int ShutdownWrites() const noexcept {
    return shutdown_writes_;
  }
  /// Read replies the fuzzer altered (the driver may then believe a lie).
  [[nodiscard]] int AlteredReads() const noexcept {
    return altered_reads_;
  }
  /// Writes that failed, were lost after an ACK, or were NACKed by the model.
  [[nodiscard]] int LostWrites() const noexcept {
    return lost_writes_;
  }

private:
  uint8_t next() noexcept {
    return in_.Byte();
  }

  Input& in_;
  tmp117_test::MockBusState bus_;
  int shutdown_writes_ = 0;
  int altered_reads_ = 0;
  int lost_writes_ = 0;
  bool switch_off_ = false;
  bool busy_reply_ = false;
  bool busy_shown_ = false;
  bool last_ul_busy_ = false;
  bool last_read_ok_ = true;
  bool no_answer_ = false;
  AfterLock after_lock_ = AfterLock::NoLock;
};

/// Writes the model ignored so far: sent during its power-up load or while EEPROM_Busy was set.
int IgnoredWrites(FuzzPort& port) noexcept {
  return port.Chip().writes_during_load + port.Chip().writes_while_busy;
}

using Driver = tmp117::TMP117<FuzzPort>;

/// The driver under test, for the checks that look at it after a call.
const Driver*& DriverUnderTest() noexcept {
  static constinit const Driver* driver = nullptr;
  return driver;
}

bool KnownError(Error e) noexcept {
  return std::strcmp(tmp117::ToString(e), "Unknown") != 0;
}

tmp117::Config FuzzConfig(Input& in) noexcept {
  tmp117::Config c{};
  c.averaging = static_cast<tmp117::Averaging>(in.Byte() & 0x03U);
  c.cycle_time = static_cast<tmp117::CycleTime>(in.Byte() & 0x07U);
  c.alert_function =
      (in.Byte() & 1U) != 0U ? tmp117::AlertFunction::Threshold : tmp117::AlertFunction::Window;
  c.alert_polarity =
      (in.Byte() & 1U) != 0U ? tmp117::AlertPolarity::ActiveHigh : tmp117::AlertPolarity::ActiveLow;
  c.alert_source =
      (in.Byte() & 1U) != 0U ? tmp117::AlertSource::DataReady : tmp117::AlertSource::Flags;
  c.low_limit_mc = in.Milli();
  c.high_limit_mc = in.Milli();
  c.offset_mc = (in.Byte() & 1U) != 0U ? in.Milli() : 0;
  c.mode = (in.Byte() & 7U) == 0U ? tmp117::Mode::Shutdown : tmp117::Mode::Continuous;
  c.allow_general_call = (in.Byte() & 3U) == 0U;
  return c;
}

/// Checks that hold after every call.
template <typename R>
void After(const R& r, bool was_initialized, int transfers_before, FuzzPort& port,
           bool is_start) noexcept {
  Check(KnownError(r.error));
  if (!r) {
    Check(r.error != Error::Ok);
  }
  if (r.error == Error::InvalidArgument) {
    Check(port.Transfers() == transfers_before);
  }
  if (r.error == Error::UnexpectedReset) {
    Check((DriverUnderTest() != nullptr) &&
          !DriverUnderTest()->IsInitialized()); // the driver stops the moment it sees the evidence
  }
  if (!was_initialized && !is_start && r.error == Error::NotInitialized) {
    Check(port.Transfers() == transfers_before);
  }
  const auto& m = port.Chip();
  Check(m.traceability_writes == 0);
  Check(m.readonly_writes == 0);
  Check(m.bad_pointer_accesses == 0);
}

void CheckTemperature(int32_t milli_c) noexcept {
  Check(milli_c >= tmp117::kMinMilliC && milli_c <= tmp117::kMaxMilliC);
}

void RunOne(Driver& d, FuzzPort& port, Input& in) noexcept {
  const bool was = d.IsInitialized();
  const int before = port.Transfers();
  const bool opted_in = d.GetConfig().allow_general_call;
  const int resets_before = port.Chip().general_call_resets;
  const int programmed_before = port.Chip().eeprom_words_programmed;
  const int shutdown_before = port.ShutdownWrites();
  const int altered_before = port.AlteredReads();
  const int lost_before = port.LostWrites();
  const int ignored_before = IgnoredWrites(port);
  const auto op = static_cast<uint8_t>(in.Byte() % 30U);
  const bool shutdown_op = (op == 1U) || (op == 5U);
  port.BeginCall(shutdown_op);
  bool is_start = false;
  bool is_shutdown_request = false;
  tmp117::Error shutdown_error = Error::Ok;
  switch (op) {
  case 0: {
    is_start = true;
    After(d.EnsureInitialized(), was, before, port, is_start);
    break;
  }
  case 1: {
    is_start = true; // Deinitialize may send shutdown after a failed start
    is_shutdown_request = true;
    const auto r = d.Deinitialize();
    shutdown_error = r.error;
    After(r, was, before, port, is_start);
    break;
  }
  case 2: {
    is_start = true;
    After(d.SoftReset(), was, before, port, is_start);
    break;
  }
  case 3: {
    is_start = true;
    After(d.GeneralCallReset(), was, before, port, is_start);
    break;
  }
  case 4:
    After(d.Configure(FuzzConfig(in)), was, before, port, false);
    break;
  case 5: {
    is_shutdown_request = (in.Byte() & 1U) != 0U;
    const auto r =
        d.SetMode(is_shutdown_request ? tmp117::Mode::Shutdown : tmp117::Mode::Continuous);
    shutdown_error = r.error;
    After(r, was, before, port, false);
    break;
  }
  case 6:
    After(d.SetAveraging(static_cast<tmp117::Averaging>(in.Byte())), was, before, port, false);
    break;
  case 7:
    After(d.SetCycleTime(static_cast<tmp117::CycleTime>(in.Byte())), was, before, port, false);
    break;
  case 8:
    After(d.SetAlertFunction(static_cast<tmp117::AlertFunction>(in.Byte() & 3U)), was, before, port,
          false);
    break;
  case 9:
    After(d.SetAlertPolarity(static_cast<tmp117::AlertPolarity>(in.Byte() & 3U)), was, before, port,
          false);
    break;
  case 10:
    After(d.SetAlertSource(static_cast<tmp117::AlertSource>(in.Byte() & 3U)), was, before, port,
          false);
    break;
  case 11:
    After(d.SetLimits(in.Milli(), in.Milli()), was, before, port, false);
    break;
  case 12:
    After(d.SetOffsetMilliC(in.Milli()), was, before, port, false);
    break;
  case 13: {
    const auto r = d.ReadTemperatureRaw((in.Byte() & 1U) != 0U ? tmp117::Verify::Device
                                                               : tmp117::Verify::None);
    After(r, was, before, port, false);
    Check(r || r.value == 0);
    break;
  }
  case 14: {
    const auto r = d.ReadTemperatureMilliC((in.Byte() & 1U) != 0U ? tmp117::Verify::Device
                                                                  : tmp117::Verify::None);
    After(r, was, before, port, false);
    if (r) {
      CheckTemperature(r.value);
    } else {
      Check(r.value == 0);
    }
    break;
  }
  case 15:
    After(d.StartOneShot(), was, before, port, false);
    break;
  case 16: {
    const auto r = d.ReadOneShotMilliC();
    After(r, was, before, port, false);
    if (r) {
      CheckTemperature(r.value);
    }
    break;
  }
  case 17:
    After(d.WaitDataReady(static_cast<uint32_t>(in.Byte()) * 20U), was, before, port, false);
    break;
  case 18:
    After(d.ReadStatus(), was, before, port, false);
    break;
  case 19:
    After(d.ReadAlertPin(), was, before, port, false);
    break;
  case 20: {
    tmp117::Config c = FuzzConfig(in);
    After(d.ProgramPowerOnDefaults(c), was, before, port, false);
    break;
  }
  case 21:
    After(d.ProgramUserWord(in.Word()), was, before, port, false);
    break;
  case 22:
    After(d.ReadUniqueId(), was, before, port, false);
    break;
  case 23:
    After(d.CheckHealth(), was, before, port, false);
    break;
  case 24:
    After(d.ReadRegister(in.Byte()), was, before, port, false);
    break;
  case 25:
    port.DelayUs(static_cast<uint32_t>(in.Word()) * 64U); // time passes
    break;
  case 26:
    port.Chip().PowerCycle(); // a brown-out the driver was not told about
    break;
  case 27:
    port.Chip().temperature_counts = static_cast<int32_t>(tmp117::detail::AsSigned16(in.Word()));
    break;
  case 28:
    port.Chip().device_id = in.Word();
    break;
  default:
    port.Chip().conversion_stuck = (in.Byte() & 1U) != 0U;
    port.Chip().eeprom_stuck_busy = (in.Byte() & 1U) != 0U;
    port.Chip().eun_write_zero_locks = (in.Byte() & 3U) != 0U; // A6: sometimes only a reset locks
    if (!was && ((in.Byte() & 3U) == 0U)) {
      port.Chip().ForceUnlock(); // a restart in the middle of programming left EUN set
    }
    break;
  }
  const bool sent = port.ShutdownWrites() > shutdown_before;
  const bool altered_in_call = port.AlteredReads() != altered_before;
  // N2: refused for another device; not a lie unless a reply was altered.
  const bool foreign = is_shutdown_request && (shutdown_error == Error::DeviceIdMismatch);
  if (foreign && !altered_in_call) {
    Check((static_cast<uint32_t>(port.Chip().device_id) & tmp117::reg::kDeviceIdMask) !=
          tmp117::reg::kDeviceIdValue);
  }
  // I3: SetMode(Shutdown) of an initialised driver whose settle found reset evidence runs the
  // rows of a driver that lost track.
  const bool i3 = is_shutdown_request && was && (op == 5U) && !d.IsInitialized();
  // Switch-off of an initialised driver is always sent, unless the EEPROM is believed unlocked.
  if (is_shutdown_request && was && !i3) {
    Check(sent || (shutdown_error == Error::EepromVerifyFailed) || foreign);
  }
  // Switch-off of a driver that lost track of the chip (the decision table, docs/contract.md):
  // sent, unless the reply to the EEPROM_UL read after the last lock write was clean with EUN = 1
  // (A21, U4), the read before the first lock write and that write both failed (U2), the EEPROM
  // stayed busy (U6) or another device answered (N2). No exclusion for failed, garbled or altered
  // reads. A call that made no transfer found a driver that never reached the chip (nothing to
  // switch off).
  const bool lost_track_off = is_shutdown_request && (!was || i3) && (port.Transfers() > before);
  const auto after_lock = port.AfterLastLock();
  if (lost_track_off) {
    const bool refused_on_evidence = after_lock == FuzzPort::AfterLock::CleanEun1;
    const bool nothing_answered = port.NoAnswer();
    const bool still_busy = port.LastUnlockBusy() && (shutdown_error == Error::Timeout);
    Check(sent || refused_on_evidence || nothing_answered || still_busy || foreign);
    // A refusal on evidence that was not a lie: the chip model really is unlocked.
    if (!sent && refused_on_evidence && !altered_in_call) {
      Check(port.Chip().EunSet());
    }
  }
  // The effect, not the frame: a switch-off that returns Ok leaves the chip model in shutdown.
  // Outside it only the documented residual risks: a write of the call lost or NACKed, a reply
  // altered, an initialised switch-off (I1, I2) whose write the chip ignored although no read of
  // the call showed EEPROM_Busy (its power-up load after a reset no read has shown, or an EEPROM
  // that stays busy), and U5 (sent with no clean read-back of the last lock write). A read that
  // showed EEPROM_Busy is reset evidence: it is never excluded (R5-1).
  const bool blind_u5 =
      (after_lock == FuzzPort::AfterLock::Failed) || (after_lock == FuzzPort::AfterLock::Garbled);
  const bool blind_initialised =
      was && !i3 && !port.BusyShown() && (IgnoredWrites(port) != ignored_before);
  if (is_shutdown_request && (shutdown_error == Error::Ok) && (port.Transfers() > before) &&
      (port.LostWrites() == lost_before) && !altered_in_call && !blind_initialised && !blind_u5) {
    Check(tmp117::reg::CfgMod::Extract(port.Chip().RawConfig()) == tmp117::reg::kModShutdown);
  }
  // EEPROM words are programmed only by the two programming calls, as long as no reply was a lie.
  // The one admitted exception (A21, U5): a switch-off whose last lock write was not read back
  // clean sends the shutdown write anyway, which programs a word if EUN was in fact set.
  const bool unconfirmed_off = lost_track_off && ((after_lock == FuzzPort::AfterLock::Failed) ||
                                                  (after_lock == FuzzPort::AfterLock::Garbled));
  if ((op != 20U) && (op != 21U) && (port.AlteredReads() == 0) && !unconfirmed_off) {
    Check(port.Chip().eeprom_words_programmed == programmed_before);
  }
  // The bus-wide reset is sent only with the opt-in that was in force before the call.
  if (!opted_in && op != 4U && op != 20U) {
    Check(port.Chip().general_call_resets == resets_before);
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  Input in(data, size);
  FuzzPort port(in);
  Driver driver(port, FuzzConfig(in));
  DriverUnderTest() = &driver;
  for (int calls = 0; calls < 64 && !in.Empty(); ++calls) {
    RunOne(driver, port, in);
  }
  DriverUnderTest() = nullptr;
  return 0;
}
