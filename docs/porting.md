# Porting

Run the driver on a new MCU by writing one class: the port.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SNOSD82D D:

    - I2C addresses and the pointer register: 7.5.2 Pointer Register; 7.5.3.1.2 Serial Bus Address, [p. 20](https://www.ti.com/lit/gpn/TMP117#page=20)
    - SMBus alert response and general call: 7.5.3.1.5 SMBus Alert Function; 7.5.3.1.6 General-Call Reset Function, [p. 22](https://www.ti.com/lit/gpn/TMP117#page=22)
    - Bus timeout and the SCL range: 7.5.3.1.7 Timeout Function; 6.7 Two-Wire Interface Timing, [p. 22](https://www.ti.com/lit/gpn/TMP117#page=22)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver never calls your SDK. It calls hooks on a class you derive from `tmp117::I2cInterface<Self>` (in
[`inc/tmp117_i2c_interface.hpp`](../inc/tmp117_i2c_interface.hpp)). The base uses CRTP and declares no hook
itself; the driver template is constrained by the `tmp117::I2cPort` concept, so calls are resolved at compile
time (no virtual functions, nothing to allocate) and a missing or misdeclared hook is a compile error at the point
of use.

## What you implement

| Method | Required | Contract |
| --- | --- | --- |
| `bool EnsureInitialized() noexcept` | yes | Bring the bus up on the first call; return `true` at once afterwards. `false` makes the driver's `EnsureInitialized()` return `Bus` |
| `bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | yes | START, `addr` + W, `len` bytes, STOP. `false` on a NACK or any bus error |
| `bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx, std::size_t rx_len) noexcept` | yes | START, `addr` + W, `tx`, repeated START, `addr` + R, `rx_len` bytes, STOP |
| `void DelayUs(uint32_t us) noexcept` | yes, for every wait | Wait at least `us` microseconds |
| `bool ReadAlertPin(bool& level) noexcept` | no | Read the raw level of the ALERT pin (true = high); `false` if the pin cannot be read |
| `void Log(LogLevel, const char* message) noexcept` | no | Print one line; `message` is plain text, never a format string |

`addr` is the 7-bit address the driver was constructed with; shift it yourself if your SDK wants the 8-bit form.

## The port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

Rules the transfers must follow:

- **One call, one transaction.** The pointer byte is the first byte of `Write()` and the one byte of `tx` in
  `WriteRead()`. A write is three bytes (pointer, high byte, low byte); a read returns two bytes, high byte first.
  Never split a transfer.
- **Address 0 must work for one byte.** `GeneralCallReset()` sends byte `06h` to the general-call address `0x00`
  with `Write()`. A port whose SDK refuses address 0 makes that call return `Bus`; leave `allow_general_call` off.
- **Repeated START for `WriteRead()`**, no STOP between the write and the read.
- **Raw bytes only.** Do not retry; the driver owns the framing.
- **Return `false` for a NACK or a bus error.** The driver turns it into `Error::Bus`.
- **Bounded time-outs.** A stuck bus fails the call; it does not block forever. The chip itself releases SDA after
  20 to 40 ms without a clock, so a transaction must finish well inside 20 ms (SCL of at least 5 kHz: the longest
  transaction is 45 clocks).
- **Do not hot-plug the chip** on a powered bus: the datasheet warns about it in 7.5.3.1.1.

## Bus requirements

The driver states what the TMP117 needs from the bus as `constexpr` values in
[`inc/tmp117_i2c_interface.hpp`](../inc/tmp117_i2c_interface.hpp) (handbook KB-CPP-123). Check your port's
settings against them with `static_assert`, as both example ports do:

| Constant | Value | Meaning (datasheet 6.7, 7.5.3.1.2) |
| --- | --- | --- |
| `kI2cAddress` | `0x48` | 7-bit address with ADD0 at GND; V+, SDA and SCL give `0x49`, `0x4A`, `0x4B` |
| `kMaxClockHz` | `400'000` | Fast mode is the fastest the chip supports |
| `kMinClockHz` | `5'000` | Below it the chip's bus timeout (20 to 40 ms) can release SDA in the middle of a transfer |
| `kRepeatedStart` | `true` | A register read is one write-read with a repeated START |

The driver never probes with an empty or zero-length transfer. A NACK from the address is `Error::Bus`.

## The ALERT pin

ALERT is the chip's one signal outside the I2C bus. It is an open-drain output that needs a pull-up resistor. The
driver reads it only through the optional `ReadAlertPin()` hook, which returns the raw level, and applies the
polarity setting itself (`ReadAlertPin()` of the driver returns true while the pin is active). The chip has no input
pins other than ADD0, so the driver drives no pin; ADD0 is wired to GND, V+, SDA or SCL on the board.

## Optional hooks

The driver finds the optional hooks at compile time with C++20 `requires`, in one place
(`tmp117::detail`). A missing hook falls back in a documented, safe way or fails to compile; it never skips a
step silently.

| Hook | Used for | Without it |
| --- | --- | --- |
| `DelayUs()` | The power-up load, conversions, EEPROM programming, the polls between them | Compile error: every wait needs it |
| `ReadAlertPin()` | The raw level of the ALERT pin, for the driver's `ReadAlertPin()` | `ReadAlertPin()` returns `Error::Unsupported`; poll `ReadStatus()` instead |
| `Log()` | One line per lifecycle event or failure | Nothing is logged |

A port with every hook:

```cpp
--8<-- "tests/docs/ports.cpp:full-port"
```

```cpp
--8<-- "tests/docs/ports.cpp:alert-pin"
```

A `ReadAlertPin()` that returns `void` is a compile error: the driver must see a failed pin read. A `DelayUs()` that
takes a narrower argument than `uint32_t` is a compile error too, so no wait is truncated. The
[compile-fail tests](testing.md) check both rules.

## Reference implementations

Every port lives with its example, in `examples/<platform>/`; the driver has none. [Platforms](platforms.md) lists
them and how to add one.

- [`examples/host/host_tmp117_port.hpp`](../examples/host/host_tmp117_port.hpp): any PC, against a simulated
  chip. Start from it for a host build or a test bench; it is also the smallest complete port.
- [`examples/esp32/main/esp32_tmp117_port.hpp`](../examples/esp32/main/esp32_tmp117_port.hpp): ESP-IDF, with
  every hook. Start from it for any ESP-IDF target.

## Check your port

1. Build it: the ports on this page are compiled by the host CI from [`tests/docs/`](../tests/docs/my_port.hpp),
   with the driver instantiated over them.
2. Call `EnsureInitialized()`: `Ok` proves that writes and reads work and that Device_ID read `0x117`.
3. Wait a second, call `ReadTemperatureMilliC()` and warm the chip with a finger: the value rises within seconds.
4. If the board has ALERT wired, call `ReadAlertPin()` with the limits at their defaults: it returns false (inactive).

## Threads

The driver is not thread-safe. Give each driver object one owner task, or guard every call with one mutex. When
several chips share a bus, the port must also serialise the bus. See [Contract](contract.md#threading).

## Editor setup

The clang-format and clang-tidy configuration is in `_config/`, and the repository root has no copy of it. Tools
that take a path use it directly; CI does the same:

| Tool | Setting |
| --- | --- |
| Command line | `clang-format --style=file:_config/.clang-format`, `clang-tidy --config-file=_config/.clang-tidy -p build` |
| VS Code (C/C++ extension) | `"C_Cpp.clang_format_style": "file:${workspaceFolder}/_config/.clang-format"` and `"C_Cpp.codeAnalysis.clangTidy.args": ["--config-file=${workspaceFolder}/_config/.clang-tidy"]` |
| clangd (VS Code clangd, Neovim, Emacs), CLion | These find `.clang-format` and `.clang-tidy` only by name, from the file upwards: copy both to the root once (`cp _config/.clang-format _config/.clang-tidy .`). `.gitignore` lists the copies, so they never reach a commit; copy again after `_config/` changes |

Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (clangd: `--compile-commands-dir=build`) so the editor sees the
same flags as the build.
