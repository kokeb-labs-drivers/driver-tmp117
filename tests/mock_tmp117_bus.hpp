/**
 * @file mock_tmp117_bus.hpp
 * @brief I2C mock ports over the register model. The transaction decoding here is the only
 * bus-specific part of the host tests.
 *
 * | Port | Hooks |
 * | --- | --- |
 * | `MinimalPort` | the required ones plus `DelayUs()` (needed by every wait) |
 * | `FullPort` | also `ReadAlertPin()` and `Log()` |
 */
#ifndef TMP117_MOCK_TMP117_BUS_HPP
#define TMP117_MOCK_TMP117_BUS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "mock_tmp117.hpp"
#include "tmp117_i2c_interface.hpp"
#include "tmp117_types.hpp"

namespace tmp117_test {

/// One transfer the driver made, for tests that assert on the exact bytes.
struct Frame {
  bool is_read{false};
  uint8_t address{0};
  uint8_t bytes[3]{}; ///< Written bytes (pointer and word), or the pointer of a read.
  uint8_t length{0};  ///< Written length.
  uint16_t reply{0};  ///< Read: the word returned to the driver.
};

/// How a read reply is corrupted, to model a dead, shorted or floating line (RD-03).
enum class Stuck : uint8_t { None, Zeros, Ones, RepeatLast };

/// Shared state of the mock ports: the chip, fault injection, a frame log and counters.
struct MockBusState {
  Tmp117Model chip;
  uint8_t address = tmp117::kI2cAddress; ///< The address the chip answers.
  bool bus_up_ok = true;                 ///< EnsureInitialized() result.
  int fail_transfer = -1;      ///< Fail the transaction with this index (0-based); -1 never.
  int transfers = 0;           ///< Transactions so far.
  int bus_up_calls = 0;        ///< EnsureInitialized() calls so far.
  bool fail_reads = false;     ///< Every read fails (writes still go through).
  Stuck stuck = Stuck::None;   ///< Corrupts every read reply.
  int flip_at = -1;            ///< Flip one bit of the reply of read number `flip_at` (0-based).
  uint16_t flip_mask = 0x0001; ///< The bits flipped.
  int reads = 0;               ///< Reads so far.
  uint16_t last_reply = 0;
  int reset_at_transfer = -1; ///< Power-cycle the chip just before this transfer.
  int ack_lost_at = -1;       ///< Apply this write, then report it as failed (a lost ACK).
  int ara_reads = 0;
  /// Lowest start and highest end address of every buffer handed to the port (RD-06).
  std::uintptr_t buffer_lo = UINTPTR_MAX;
  std::uintptr_t buffer_hi = 0;

  void NoteBuffer(const uint8_t* p, std::size_t len) noexcept {
    const auto lo = AddressOf(p);
    buffer_lo = (lo < buffer_lo) ? lo : buffer_lo;
    buffer_hi = ((lo + len) > buffer_hi) ? (lo + len) : buffer_hi;
  }
  static constexpr std::size_t kMaxFrames = 4096;
  std::array<Frame, kMaxFrames> frames{};
  std::size_t frame_count = 0;

  void Record(const Frame& f) noexcept {
    if (frame_count < kMaxFrames) {
      frames[frame_count] = f;
      ++frame_count;
    }
  }

  /// Writes seen so far to this pointer (3-byte frames only).
  [[nodiscard]] int WritesTo(uint8_t pointer) const noexcept {
    int n = 0;
    for (std::size_t i = 0; i < frame_count; ++i) {
      if (!frames[i].is_read && frames[i].length == 3 && frames[i].bytes[0] == pointer) {
        ++n;
      }
    }
    return n;
  }

  /// START, addr + W, [pointer, hi, lo], STOP; or the general call [06h] to address 00h.
  bool DoWrite(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    const int index = transfers++;
    NoteBuffer(data, len);
    if (index == reset_at_transfer) {
      chip.PowerCycle();
    }
    Frame f;
    f.is_read = false;
    f.address = addr;
    f.length = static_cast<uint8_t>(len > 3 ? 3 : len);
    for (std::size_t i = 0; (i < len) && (i < 3); ++i) {
      f.bytes[i] = data[i];
    }
    Record(f);
    if (index == fail_transfer || data == nullptr || len == 0) {
      return false; // NACK
    }
    const bool lose_ack = index == ack_lost_at;
    if (addr == chip::kGeneralCall) {
      if (len == 1 && data[0] == chip::kGeneralCallReset) {
        chip.GeneralCallReset();
      }
      return !lose_ack;
    }
    if (addr != address || len != 3) {
      return false;
    }
    const auto word = static_cast<uint16_t>((static_cast<uint32_t>(data[1]) << 8U) | data[2]);
    const bool acked = chip.Write(data[0], word);
    return acked && !lose_ack;
  }

  /// START, addr + W, [pointer], repeated START, addr + R, 2 bytes, STOP.
  bool DoWriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                   std::size_t rx_len) noexcept {
    const int index = transfers++;
    NoteBuffer(tx, tx_len);
    NoteBuffer(rx, rx_len);
    if (index == reset_at_transfer) {
      chip.PowerCycle();
    }
    Frame f;
    f.is_read = true;
    f.address = addr;
    f.length = 1;
    if (tx != nullptr && tx_len >= 1) {
      f.bytes[0] = tx[0];
    }
    if (index == fail_transfer || fail_reads || addr != address || tx == nullptr || tx_len != 1 ||
        rx == nullptr || rx_len != 2) {
      Record(f);
      return false;
    }
    uint16_t word = chip.Read(tx[0]);
    switch (stuck) {
    case Stuck::Zeros:
      word = 0x0000;
      break;
    case Stuck::Ones:
      word = 0xFFFF;
      break;
    case Stuck::RepeatLast:
      word = last_reply;
      break;
    case Stuck::None:
      break;
    default:
      break;
    }
    if (reads == flip_at) {
      word = static_cast<uint16_t>(word ^ flip_mask);
    }
    ++reads;
    last_reply = word;
    rx[0] = static_cast<uint8_t>(static_cast<uint32_t>(word) >> 8U);
    rx[1] = static_cast<uint8_t>(static_cast<uint32_t>(word) & 0xFFU);
    f.reply = word;
    Record(f);
    return true;
  }
};

/// The required I2C hooks over a MockBusState. Derive `class P : public MockPortBase<P>`.
template <typename Self>
class MockPortBase : public tmp117::I2cInterface<Self> {
public:
  MockBusState state;

  bool EnsureInitialized() noexcept {
    state.bus_up_calls++;
    return state.bus_up_ok;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return state.DoWrite(addr, data, len);
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return state.DoWriteRead(addr, tx, tx_len, rx, rx_len);
  }
};

/// Required hooks plus the delay; time passes in the model as the driver waits.
class MinimalPort : public MockPortBase<MinimalPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
};

/// Every optional hook, with counters.
class FullPort : public MockPortBase<FullPort> {
public:
  uint64_t delayed_us = 0; ///< Sum of DelayUs() arguments.
  int log_lines = 0;       ///< Log() calls.
  int error_lines = 0;     ///< Log() calls at LogLevel::Error.
  bool pin_ok = true;      ///< ReadAlertPin() result.

  void DelayUs(uint32_t us) noexcept {
    delayed_us += us;
    state.chip.Elapse(us);
  }
  bool ReadAlertPin(bool& level) noexcept {
    if (!pin_ok) {
      return false;
    }
    level = state.chip.AlertPinHigh();
    return true;
  }
  void Log(tmp117::LogLevel level, [[maybe_unused]] const char* message) noexcept {
    log_lines++;
    if (level == tmp117::LogLevel::Error) {
      error_lines++;
    }
  }
};

} // namespace tmp117_test

#endif // TMP117_MOCK_TMP117_BUS_HPP
