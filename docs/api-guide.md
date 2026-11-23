# API guide

The driver's API by task, with code that compiles: initialise, configure, measure, handle errors and shut down.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SNOSD82D D:

    - Power-up, the EEPROM load and the first result: 7.3.1 Power-Up, [p. 12](https://www.ti.com/lit/gpn/TMP117#page=12)
    - Result format, LSB and the 8000h value: 7.3.3 Temperature Result and Limits; Table 7-1, [p. 13](https://www.ti.com/lit/gpn/TMP117#page=13)
    - Continuous, shutdown and one-shot modes: 7.4.1 Continuous Conversion; 7.4.2 Shutdown; 7.4.3 One-Shot, [p. 14](https://www.ti.com/lit/gpn/TMP117#page=14)
    - Alert mode and therm mode: 7.4.4 Therm and Alert Modes, [p. 15](https://www.ti.com/lit/gpn/TMP117#page=15)
    - EEPROM, unlock and programming: 7.5.1 EEPROM Overview, [p. 18](https://www.ti.com/lit/gpn/TMP117#page=18)
    - Traceability words that must not be erased: 7.5.1.1 EEPROM Overview; 7.5.1.2 Programming the EEPROM; 7.6.7 EEPROM1; 7.6.10 EEPROM3, [p. 18](https://www.ti.com/lit/gpn/TMP117#page=18)
    - SMBus alert response and general call: 7.5.3.1.5 SMBus Alert Function; 7.5.3.1.6 General-Call Reset Function, [p. 22](https://www.ti.com/lit/gpn/TMP117#page=22)
    - Configuration register: 7.6.3 Configuration Register; Table 7-6, [p. 27](https://www.ti.com/lit/gpn/TMP117#page=27)
    - Averaging and the conversion cycle time: 7.3.2 Averaging; Table 7-7, [p. 28](https://www.ti.com/lit/gpn/TMP117#page=28)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## The shape of the API

| Group | Calls |
| --- | --- |
| Lifecycle | `EnsureInitialized()`, `IsInitialized()`, `Deinitialize()`, `SoftReset()`, `GeneralCallReset()` |
| Configuration | `Configure()`, `GetConfig()`, `SetMode()`, `SetAveraging()`, `SetCycleTime()`, `SetAlertFunction()`, `SetAlertPolarity()`, `SetAlertSource()`, `SetLimits()`, `SetOffsetMilliC()` |
| Measurement | `ReadTemperatureMilliC()`, `ReadTemperatureRaw()`, `StartOneShot()`, `ReadOneShotMilliC()`, `WaitDataReady()` |
| Status and alerts | `ReadStatus()`, `GetLatchedStatus()`, `ClearLatchedStatus()`, `ReadAlertPin()` |
| EEPROM | `ProgramPowerOnDefaults()`, `ProgramUserWord()`, `ReadUniqueId()` |
| Diagnostics | `CheckHealth()`, `ReadRegister()`, `GetLastConfigWord()`, `GetRevision()`, `tmp117::GetDriverVersion()` |
| Not covered | `SmbusAlertResponse()` always returns `Unsupported` ([Ambiguities](ambiguities.md#q3-smbus-alert-response)) |

Every call that touches the chip returns `Result<T>`: check `ok()` (or the `bool` conversion) and read `value`, or
read `error`. `Result` is `[[nodiscard]]`, so an unchecked call is a compiler warning. A failed call holds `T{}`.

## Initialise

```cpp
--8<-- "tests/docs/api_tasks.cpp:lifecycle"
```

The constructor stores the port, the address and the configuration (`Config{}` unless you pass one) and does no I/O.
`EnsureInitialized()` is the one way to start the chip. Its first call brings the bus up, waits for the chip's
power-up load (about 1.5 ms, then a bounded poll of EEPROM_Busy, see `Config::reset_timeout_ms`), checks that
Device_ID reads `0x117` in bits 11:0, writes the limits, the offset and the configuration, and reads them back.
It does not reset the chip: the chip is in the state its EEPROM gives it and the configuration is written over it.

Once it has succeeded, `EnsureInitialized()` returns `Ok` at once with no I/O, so a caller can call it before every
use. After a failure the driver stays uninitialised and the next call starts again from the beginning. No other call
starts the chip: before `EnsureInitialized()` succeeds every read and write returns `NotInitialized` and sends
nothing, and `Configure()` and the setters do not store anything either. To change the start-up settings, pass them to
the constructor.

The Device_ID check masks the revision field (bits 15:12) and compares bits 11:0 with `0x117`, so a TMP117 of any
silicon revision passes, and a TMP119 (ID `0x2117`) reads as revision 2 of a TMP117: see
[Ambiguities](ambiguities.md#a13-the-revision-field).

## Read continuously

In `Mode::Continuous` (the reset value) the chip converts on its own cycle and `ReadTemperatureMilliC()` returns the
latest result in thousandths of a degree Celsius, rounded half away from zero. One count is 7.8125 m°C, so
`ReadTemperatureRaw()` gives the counts for code that wants to avoid the multiplication.

The result `8000h` (-256 °C) is never returned as a temperature. Before the first conversion after the start it
means "no result yet" and the call returns `NoMeasurement`: wait one cycle (`GetCycleTimeUs()` gives it from the
settings) and ask again. After the driver has seen a real result, `8000h` can only mean the chip restarted, and the
call returns `UnexpectedReset`.

A continuous read does not tell a fresh result from an old one. `ReadStatus().value.data_ready` says that a
conversion finished since the last read of Configuration or Temp_Result.

`Verify::Device` reads Device_ID before the temperature, one more transfer. A data line stuck at all zeros or all
ones cannot pass it, so use it when the temperature feeds a decision and the bus is long or noisy. Without it, a
result of `0000h` or `FFFFh` (what a stuck line returns, and also 0 degC and -1 count on a working chip) is confirmed
by a Device_ID read before it is returned, so those two values cost one more transfer and a stuck line fails with
`DeviceIdMismatch`. A line that repeats a word other than these two is not caught without `Verify::Device`.

## Measure on demand

Shutdown between samples keeps the chip at about 0.15 µA. `ReadOneShotMilliC()` starts one conversion, waits for the
time the averaging setting needs, and reads the result:

```cpp
--8<-- "tests/docs/api_tasks.cpp:one-shot"
```

The wait is `(conversions + 1) x 17.5 ms`, one conversion more than needed as margin for the oscillator, which the
datasheet does not give an accuracy for: 35 ms for one conversion up to 1.14 s for 64. If the chip does not finish,
the call sends shutdown and returns `Timeout`. `StartOneShot()` and `WaitDataReady()` are the two halves for code that
does something else meanwhile; while a one-shot is pending the setters and `ReadTemperature*()` return `Busy`, except
`SetMode(Mode::Shutdown)`, which ends it.

`ReadOneShotMilliC()` leaves the chip in shutdown, also when the driver was in continuous mode, and `GetConfig().mode`
says `Shutdown` afterwards: `ReadTemperatureMilliC()` then returns the last result for ever, with no error. Call
`SetMode(Mode::Continuous)` to resume continuous conversion.

## Alerts

```cpp
--8<-- "tests/docs/api_tasks.cpp:alerts"
```

Alert mode (`AlertFunction::Window`): ALERT is active when a result is above the high limit or below the low limit,
and the flags stay set until Configuration is read. Therm mode (`AlertFunction::Threshold`): ALERT is active above the
high limit and stays active until the result is below the low limit, so the two limits make a hysteresis. In both
modes `SetLimits()` takes both limits at once in thousandths of a degree, refuses a value outside -256000 to 255992
and refuses a low above the high ([Ambiguities](ambiguities.md#a10-limits-in-the-wrong-order)). Nothing is clamped.

The chip clears HIGH_Alert, LOW_Alert and Data_Ready every time Configuration is read. The driver reads
Configuration itself (to check for a reset, to wait), so it keeps what it saw:

```cpp
--8<-- "tests/docs/api_tasks.cpp:status"
```

`GetLatchedStatus()` returns every flag any read has seen since `ClearLatchedStatus()`.

**The driver's own reads release the ALERT pin.** In alert mode a read of Configuration clears HIGH_Alert and
LOW_Alert and so deasserts ALERT (7.4.4.1, p. 16). `ReadStatus()`, `WaitDataReady()`, `StartOneShot()`,
`CheckHealth()`, the settle reads after a failed write and a raw `ReadRegister()` of Configuration all read it, so an
application that services ALERT as a level interrupt can find the pin released by the driver's own polling. The flags
are not lost: they are in `GetLatchedStatus()`, which an interrupt service routine should consult rather than the pin.
Poll less, or do not poll while a level interrupt is pending, if the pin itself must stay asserted until your handler
has run. In therm mode the flag follows the limits and a read does not clear it.

`ReadAlertPin()` returns the
level of the ALERT pin after polarity, true while the pin is active, and needs the optional `ReadAlertPin()` hook
of the port; without it the call returns `Unsupported`.

## Handle errors

```cpp
--8<-- "tests/docs/api_tasks.cpp:errors"
```

| Signal | Meaning | What to do |
| --- | --- | --- |
| `Error::Bus` | The port failed a transfer | Check the port and the wiring; retry, or `Deinitialize()` and `EnsureInitialized()` |
| `Error::Timeout` | The power-up load, a conversion or an EEPROM word did not finish in time | Check V+ and the pull-ups; raise the deadline in `Config` if the part is slow. From a switch-off: the EEPROM was still busy after 14 ms and nothing was sent; call it again |
| `Error::DeviceIdMismatch` | Device_ID is not `0x117` in bits 11:0 | Check the address pin and the wiring; another device may answer. A switch-off after it sends nothing to that device |
| `Error::BadReply` | A bit that must read 0 reads 1, or a register does not read back what was written | A noisy or stuck line; lower the clock, check the pull-ups |
| `Error::NoMeasurement` | No conversion has finished since the start | Wait one cycle and read again |
| `Error::UnexpectedReset` | The chip restarted behind the driver's back | Check the supply; `EnsureInitialized()` starts it again. To switch it off instead, call `Deinitialize()` at once: it waits out the chip's power-up load (up to 14 ms); on `Timeout` call it again |
| `Error::EepromVerifyFailed` | The EEPROM did not hold what was written, or it could not be relocked | Power-cycle the chip, then read the registers before trusting it |
| `Error::Busy` | A one-shot is pending | Wait for it, or `SetMode(Mode::Shutdown)` |
| `Error::Unsupported` | `ReadAlertPin()` without the hook, `GeneralCallReset()` without the opt-in, `SmbusAlertResponse()` | Add the hook, set `allow_general_call`, or read `ReadStatus()` |
| `Error::InvalidArgument` | An argument is out of range | Nothing was sent; fix the argument |

`Crc` and `DeviceFault` belong to the base set that every driver of the family shares; the TMP117 has no CRC on its
bus and reports no sensing fault, so this driver never returns them. Every value, with the calls that return it, is on
the [Contract](contract.md#errors) page. `tmp117::ToString()` names an error for a log.

## Check that the chip is still the one you started

`CheckHealth()` reads Device_ID and Configuration and compares the setting fields with the driver's copy. It returns
`UnexpectedReset` when the chip no longer holds them. A reset to exactly the values the driver wrote is invisible
without a reset flag (ambiguity A9); only a result of `8000h` after a real one gives it away.

## Diagnostics

For bring-up and fault finding:

| Call | I/O | What it gives |
| --- | --- | --- |
| `ReadRegister(pointer)` | one transfer, no retry | The raw word of one register of the map (00h to 08h and 0Fh); other pointers return `InvalidArgument` and send nothing |
| `GetLastConfigWord()` | none | The raw Configuration word of the last read, by any call |
| `GetLatchedStatus()` | none | The alert and data-ready flags of every Configuration read since the last `ClearLatchedStatus()` |
| `ReadUniqueId()` | three transfers | EEPROM1, EEPROM2 and EEPROM3; the first and third hold the traceability identifier |
| `GetRevision()` | none | The revision field of Device_ID read at the start |

## Program the power-on defaults

The chip loads its configuration, limits and offset from its EEPROM at power-up, so a chip that must start in a
known state without a host (a thermostat that guards a load) can be programmed once:

```cpp
tmp117::Config defaults{};
defaults.low_limit_mc = 5000;   // alert below 5 degC
defaults.high_limit_mc = 60000; // alert above 60 degC
const auto r = sensor.ProgramPowerOnDefaults(defaults);
// r.error: Ok, or EepromVerifyFailed, Timeout, Bus, BadReply, InvalidArgument, Busy
```

This is a guarded operation, never started by another call. It shuts the chip down, unlocks the EEPROM, writes
each word and waits for it, locks the EEPROM again, resets the chip, checks that the lock is off, reads the words back
and starts the running configuration. EEPROM1 and EEPROM3, the NIST traceability words the datasheet says not to
erase, are never written: the driver refuses their pointers and checks, before and after, that they are unchanged.
The EEPROM endurance is 1000 writes, so do not call it in a loop. After any error the driver is uninitialised and the
chip's state is unknown: read the registers, power-cycle if the error is `EepromVerifyFailed`, and
start again. [Ambiguities](ambiguities.md#a5-what-the-soft-reset-does) says what the datasheet leaves open about
the relock and what the verification can and cannot prove. `ProgramUserWord()` writes the user word EEPROM2 the same
way.

## Shut down

```cpp
--8<-- "tests/docs/api_tasks.cpp:deinit"
```

`Deinitialize()` sends the shutdown write whatever doubt the driver holds, and marks the driver uninitialised even if
the write failed. After the driver lost track of the chip it reads EEPROM_UL first and sends nothing while the chip
is busy (its power-up load, or a word being programmed): it waits 7 ms and reads again, twice at most, and returns
`Timeout` when the chip is still busy (call it again). It then writes the EEPROM lock, and it sends nothing when the
read after its last lock write shows EUN = 1 (`EepromVerifyFailed`; power-cycle the chip, A21). After a Device_ID
mismatch it sends nothing to another device (`DeviceIdMismatch`). The rules are the
[switch-off decision table](contract.md#switch-off-decision-table). After it every call returns
`NotInitialized` until the next `EnsureInitialized()`, which starts the chip again from the beginning: `Deinitialize()`
then `EnsureInitialized()` is also the recovery after a fault. Two calls still reach the chip in that state, because a
driver that lost track of it must be able to switch it off: `Deinitialize()` and `SetMode(Mode::Shutdown)`. On a driver
that never reached the chip (no `EnsureInitialized()` yet, or one refused for an argument or a port that did not come
up) `Deinitialize()` returns `Ok` and sends nothing: it does not switch off a chip that converts from its power-on
defaults (row N1), and `SetMode()` returns `NotInitialized`.

Next: [Configuration](configuration.md).
