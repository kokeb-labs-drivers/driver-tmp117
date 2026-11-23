# Contract

What the TMP117 driver promises, what it needs, and the rules a caller follows.

The driver inherits the [Kokeb Labs driver contract](https://github.com/kokeb-labs/.github/blob/main/standards/driver.md);
this page states only what is specific to it. A change to anything here is a breaking change, listed under
**Breaking** in the [changelog](changelog.md).

## Promises

- The constructor is `noexcept`, stores the address and the configuration, does no I/O and cannot fail.
- `EnsureInitialized()` is the only way to start the driver. Its first call checks the address and the stored
  configuration (`InvalidArgument`, nothing sent), brings the bus up, waits for the chip's power-up load, checks
  Device_ID, reads EEPROM_UL and locks the EEPROM when the unlock bit is set (see below), writes the limits, the
  offset and the configuration, and reads them back. On any failure the driver stays
  uninitialised and the next call starts again from the beginning; once it has succeeded, later calls return `Ok`
  with no I/O.
- Every call that touches the chip returns `Error::NotInitialized` before `EnsureInitialized()` succeeds and sends
  nothing. No call initialises or resets the chip as a side effect.
- **An argument that is refused sends nothing.** `InvalidArgument` is decided before the first transfer, in every
  call.
- **The driver never writes a register it does not own.** Temp_Result and Device_ID are read-only, and EEPROM1 and
  EEPROM3 hold the traceability identifier the datasheet says not to erase; `RegisterBus::WriteWord` refuses their
  pointers and every pointer outside the map. `ProgramPowerOnDefaults()` reads the two traceability words before and
  after and reports a change.
- **The EEPROM is locked again on every path.** `ProgramPowerOnDefaults()` and `ProgramUserWord()` never return with
  the unlock bit left set unless the chip refused to lock; then they return `EepromVerifyFailed` when that is the only
  failure, and the driver is uninitialised. After an earlier error (a `Timeout` of a word that never finished, a `Bus`
  error) the first error is returned and the lock may still not have taken: power-cycle the chip.
- **A write never programs EEPROM by accident.** With the unlock bit set, every write to a programmable register is
  stored in EEPROM as a power-on default (7.5.1.2, p. 18). A restart of the host during programming, or a lock that did
  not take, leaves the chip so. `EnsureInitialized()` therefore reads EEPROM_UL before its first register write; when
  EUN reads 1 it writes EUN = 0 and reads it back, and when it cannot confirm the lock it returns
  `EepromVerifyFailed` and has written nothing else ([A21](ambiguities.md#a21-start-up-and-switch-off-on-an-unlocked-chip)).
  Outside `ProgramPowerOnDefaults()` and `ProgramUserWord()` no call programs an EEPROM word, except in the two
  [residual risks](#residual-risks) of A21: switch-off row U5, and one bit flipped on the bus in the first EEPROM_UL
  reply.
- **No write follows reset evidence.** When the driver sees that the chip restarted (the setting bits of
  Configuration differ from its copy, EEPROM_Busy is set unexpectedly, or a result of `8000h` follows a real one), the
  call returns `UnexpectedReset`, the driver is uninitialised, and no further write is sent until
  `EnsureInitialized()` has run again, except the switch-off, which follows its
  [decision table](#switch-off-decision-table). A reset to exactly the values the driver wrote is invisible
  ([A9](ambiguities.md#a9-a-reset-to-identical-values)), and so is a reset that changes only the limits or the
  offset ([residual risks](#residual-risks)).
- **`8000h` is never a temperature.** It reads as `NoMeasurement` before the first conversion and as
  `UnexpectedReset` after one.
- **Switch-off always sends, except to a driver that never reached the chip (N1), to a chip that reads unlocked or
  busy, to a chip that answers nothing (U2), or to another device.** `Deinitialize()`
  and `SetMode(Mode::Shutdown)` run one routine whose rules are the [decision table](#switch-off-decision-table)
  below. An initialised driver always sends the one Configuration write with MOD = 01, whatever doubt it holds: a
  write in doubt is settled first by `SetMode()`, and when the settle read fails the shutdown write is sent anyway;
  when the settle finds reset evidence the lost-track rows apply. A driver that lost track of the chip
  (uninitialised after a failed programming, a reset or a start-up) reads EEPROM_UL first and never writes after a
  clean reply with EEPROM_Busy set: the chip is in its power-up load, which ignores writes, or programming a word,
  which forbids them (7.5.1.1 and 7.5.1.2, p. 18). It waits 7 ms and reads again, twice at most per call, and returns
  `Timeout` with nothing sent when the chip is still busy. It then writes the EEPROM lock (EUN = 0, a write that
  programs no EEPROM) and reads it back. Only the read that follows the last lock write decides: when it is clean and
  shows EUN = 1 the call returns `EepromVerifyFailed` and sends nothing, because the write would be programmed as the
  power-on default ([A21](ambiguities.md#a21-start-up-and-switch-off-on-an-unlocked-chip)); a failed or garbled read
  is no evidence and the shutdown write goes out. A chip that answers neither the read nor the lock write gets nothing
  more (`Bus`). After a Device_ID mismatch the driver reads Device_ID once more and sends nothing when another part
  answers (`DeviceIdMismatch`).
- **A failed write is not forgotten.** The driver keeps its copy of the chip's configuration only for what the chip
  accepted; a write that failed is read back before the next change and the copy follows the chip.
- **The alert flags are not lost.** The three flags the chip clears on a read of Configuration are latched by every
  read the driver makes (`GetLatchedStatus()`).
- Every port failure propagates as `Error::Bus`; no getter returns a stale or zero value after a failed read.
- Every wait is bounded: the power-up load by `Config::reset_timeout_ms`, an EEPROM word by
  `Config::eeprom_timeout_ms`, a one-shot by `(n + 1) x 17.5 ms` for `n` averaged conversions, `WaitDataReady()` by its
  argument (at most 20 s). The switch-off of a driver that lost track of the chip makes at most 8 transfers (9 after
  a Device_ID mismatch) and waits at most 14 ms (two 7 ms waits for a busy EEPROM), whatever the bus returns;
  `SetMode()` adds the settle reads (at most 5) in front. Deadlines are poll counts, not a clock: a port whose failed
  transfer itself takes long (an SMBus timeout of 25 ms to 35 ms) stretches a call by that time per poll.
- `Config{}` equals the datasheet reset values, proved by `static_assert`.
- A missing `DelayUs()` is a compile error; a missing `ReadAlertPin()` gives `Error::Unsupported` from
  `ReadAlertPin()`; a missing `Log()` logs nothing. A misdeclared hook is a compile error.
- `Deinitialize()` that returns `Ok` leaves the chip in shutdown, its lowest-current state, except row N1 (the driver
  never reached the chip, so nothing is sent: a chip that converts from its power-on defaults keeps converting) and
  the [residual risks](#residual-risks) (a reset the driver cannot see, row U5). A boot path that needs the chip in
  shutdown after a warm reset calls `EnsureInitialized()` first, then `Deinitialize()` or `SetMode(Mode::Shutdown)`.
- The bus-wide general-call reset is sent only with `Config::allow_general_call`. The SMBus alert response is not
  supported and sends nothing.

## Switch-off decision table

The specification of `Deinitialize()` and `SetMode(Mode::Shutdown)`; the code follows it row by row (`switchOff()`
and `lockBeforeSwitchOff()` in `src/tmp117.ipp`), and each row has a test in
[`tests/test_switch_off.cpp`](../tests/test_switch_off.cpp) named after it. An EEPROM_UL reply is *clean* when bits
13:0 read 0 (Table 7-10, p. 30); a failed read and a reply with any of those bits set are *no evidence*. A clean reply
is *busy* when EEPROM_Busy (bit 14) reads 1, whatever EUN reads. `R` is a read of EEPROM_UL, `L` the lock write
(EEPROM_UL = 0000h), `S` the Configuration write with MOD = 01, `C` a Configuration write of `SetMode()` (MOD = 01 from
the copy), `D` a read of Device_ID.

| Row | Driver | First read `R` | Then, per attempt (at most 2): `L`, then `R` | Transfers sent | Status |
| --- | --- | --- | --- | --- | --- |
| N1 | Never reached the chip | not sent | | none | `Deinitialize()`: `Ok`; `SetMode()`: `NotInitialized` |
| N2 | Last Device_ID read was another part | `D` first: another part's word | | `D` | `DeviceIdMismatch`, nothing else sent |
| I1 | Initialised, `SetMode()` | not sent | | settle reads (any result), `C` | the write's status; the copy follows the chip |
| I2 | Initialised, `Deinitialize()` | not sent | | `S` | the write's status; uninitialised afterwards |
| I3 | Initialised, `SetMode()`, the settle finds reset evidence | as the U and W rows | | settle reads, then a U row | as that U row |
| U1 | Lost track | clean, EUN = 0, not busy | | `R S` | the write's status |
| U2 | Lost track | failed | first `L` not acknowledged: stop | `R L` | `Bus` |
| U3 | Lost track | EUN = 1, failed or no evidence | an `R` after an `L` reads clean EUN = 0, not busy: stop | `R (L R){1,2} S` | the write's status |
| U4 | Lost track | EUN = 1, failed or no evidence | the `R` after the last `L` reads clean EUN = 1, not busy | `R L R L R` | `EepromVerifyFailed`, nothing else sent |
| U5 | Lost track | EUN = 1, failed or no evidence | the `R` after the last `L` failed or is no evidence | `R L R L R S` | the write's status, a warning logged |
| U6 | Lost track | busy after two waits | or an `R` after an `L` busy after the second wait | `R R R`, or ending in `R` | `Timeout`, nothing else sent; call again |
| W1 | Lost track, chip in its power-up load | clean, busy, EUN = 0 | any `R` busy the same way | the busy `R`, then 7 ms, `R` again | as the row the next `R` leads to |
| W2 | Lost track, a word being programmed | clean, busy, EUN = 1 | any `R` busy the same way | the busy `R`, then 7 ms, `R` again | as the row the next `R` leads to |
| R1 | Lost track after a reset command that failed but took effect | clean, busy (the load) | | as W1 | as W1 |

- No write follows a clean busy `R`. The two 7 ms waits (`kEepromProgramUs`, the typical programming time of 6.6,
  p. 7, and over four times the 1.5 ms load of A1) are a budget for the whole call, as in Figure 7-7 (p. 19: wait,
  then read EEPROM_Busy again). The datasheet prints no maximum; a chip still busy after 14 ms gets nothing (U6), and
  the caller calls again. A failed or garbled read is not busy: it is no evidence, as everywhere in the table.
- Evidence never carries over: a clean EUN = 1 seen before a lock write says nothing about that write (U5 after an
  earlier clean EUN = 1 still sends). The lock is written whatever the first read returned, except in U1, W1 and U6.
- U2 counts the `R` read just before the first `L`: after a busy reply and its wait, a failed `R` and a NACKed `L`
  also stop (three transfers: `R R L`).
- N2 needs another part's word on that `D`: a failed read, DID 117h, or 0000h or FFFFh (the words of a line stuck at
  0 or 1, which may still pass writes) go on by the other rows. Without a mismatch there is no `D`.
- R1: a soft or general-call reset whose ACK was lost (`SoftReset()`, `GeneralCallReset()`, the relock of
  `ProgramPowerOnDefaults()` and `ProgramUserWord()`) leaves the driver uninitialised and the chip in its load; the
  first `R` shows the load and W1 applies.
- U5 is the residual risk of A21: when every lock write was lost or did not take and no read-back succeeded, the
  shutdown word is programmed as a power-on default, and it is sent blind: a chip busy at that moment ignores it.
  Start-up never takes that risk: a lock not confirmed by a clean EUN = 0 refuses there, a garbled read included.
- The `S` of the lost-track rows carries the driver's copy of every field, not only MOD (FC-12): after a reset the
  alert polarity, alert function, averaging and cycle time of the copy replace the chip's EEPROM defaults. Harmless in
  shutdown; a board whose ALERT level comes only from programmed defaults sees the copy's polarity.
- Tests: N1, N2 (twice), I1, I2, I3, U1 to U6, W1 (twice), W2, R1 ("Switch-off row ..."), U3 also for a NACKed first
  lock write, U4 also after a failed first read-back, U5 also for every read failing and for the residual risk; a
  sweep fails every subset of the first eight transfers of the call on a locked, an unlocked, an unlockable, a
  programming and a loading chip, and checks the rows, the 8-transfer and 14 ms bounds, the busy rule, the effect
  (`Ok` after a confirmed lock leaves the chip in shutdown) and the programming rule. The fuzz target checks the
  frames of U2 to U6 and N2 with no exclusion, the busy rule, and the effect: a switch-off that returns `Ok` leaves
  the model in shutdown outside the residual risks below: a write of the call lost or NACKed, a reply altered in the
  call, an initialised switch-off whose write the chip ignored although no read of the call showed EEPROM_Busy, and U5
  (`tests/fuzz/fuzz_tmp117.cpp`). A read that shows EEPROM_Busy, in a settle or anywhere else, is reset evidence and is
  never excluded: the I3 tests "a settle read that shows EEPROM_Busy is reset evidence" (PROBE-R5-1) and "a lost ACK, a
  power cycle, then SetMode(Shutdown)" (D-2) and the corpus input `regress-settle-reads-eeprom-busy` pin it.

### Shutdown path states

What the driver knows before the call, what it does, and where it ends. "Lost track" is `IsInitialized()` false after
a start-up reached the chip.

| State before | Event in the call | Next state | Chip afterwards |
| --- | --- | --- | --- |
| Never started | `Deinitialize()` or `SetMode(Shutdown)` | Never started | untouched |
| Initialised | `SetMode(Shutdown)`, write acknowledged | Initialised, copy in shutdown | shutdown |
| Initialised | `SetMode(Shutdown)`, write failed | Initialised, write in doubt (settled by the next call) | unknown |
| Initialised | `SetMode(Shutdown)`, the settle finds reset evidence (I3) | Lost track, then as the U rows | as the U rows |
| Initialised | `Deinitialize()` | Lost track | shutdown if the write was acknowledged |
| Lost track | lock confirmed or not read back (U1, U3, U5) | Lost track | shutdown if the write was acknowledged |
| Lost track | EUN reads 1 after the last lock write (U4) | Lost track | unchanged, unlocked: power-cycle it |
| Lost track | no answer (U2) | Lost track | unchanged; the next call starts again from the first read |
| Lost track | EEPROM_UL still busy after 14 ms (U6) | Lost track | unchanged; call again |
| Any, after a Device_ID mismatch | another part answers Device_ID (N2) | Lost track | that device untouched |
| Lost track | `EnsureInitialized()` succeeds | Initialised | as configured, lock confirmed |

## Residual risks

The owner's rule: an event so improbable that no reasonable design defends against it is documented here, not
engineered. Each entry names the finding it comes from and, where one exists, the test that pins it.

- **One flipped EUN bit in the first EEPROM_UL reply (R4-3, FC-9, F-A3, B-5).** It needs a chip left unlocked by an
  interrupted programming and a bus error that turns exactly bit 15 of exactly that reply from 1 to 0 (or a line that
  goes stuck at 0 on that read) with bits 13:0 still clean. The bus has no CRC, so the driver takes it for a lock:
  start-up programs one limit word and then fails its read-back (`BadReply`); switch-off row U1 programs the
  shutdown word as a power-on default. At most one EEPROM word ("regress-r4-3").
- **A reset the driver cannot see, within the 1.5 ms load before an initialised switch-off or setter (FC-10,
  F-A1).** A chip-only brown-out, another master's general call, or another driver object's `GeneralCallReset()` on
  the same bus. Rows I1 (no write in doubt) and I2 send with no read first, so the write lands in the load and is
  ignored, and a setter's write is lost the same way; the call returns `Ok` (or `Bus` under the NACK reading of A14).
  The next call that reads Configuration sees the reset (`UnexpectedReset`). The same holds for any state in which
  the chip ignores writes and that no read of the session has shown, such as an EEPROM still busy that an earlier
  corrupted reply hid.
- **Start-up and the relock write the lock after the busy deadline (FC-11).** `tryLock()` waits for EEPROM_Busy up to
  `Config::eeprom_timeout_ms` and then writes EUN = 0 whatever it reads. Only a chip still programming after that
  deadline (50 ms by default, about seven times the typical time) gets a write while busy; that chip is faulty, the
  call already reports `Timeout` or `EepromVerifyFailed`, and the lock write programs nothing.
- **The lost-track shutdown word carries the copy (FC-12).** See the decision table notes.
- **A reset that changes only the limits or the offset (F-A5).** Reset evidence is read from Configuration, EEPROM_Busy
  and 8000h. When the EEPROM holds the driver's Configuration word but other limits or another offset, a reset outside
  the first conversion passes `ReadStatus()` and `CheckHealth()`; see [A9](ambiguities.md#a9-a-reset-to-identical-values).
- **An offset below about -200 °C (F-A8).** It saturates a cold result to 8000h, which reads as `NoMeasurement` or
  `UnexpectedReset` ([A7](ambiguities.md#a7-the-offset-at-the-minimum-and-8000h)); no real use needs it.
- **Corruption the bus cannot reveal (B-6).** A write whose data is corrupted on the way (no CRC), a read reply that
  repeats the previous transfer, and a legitimate 8000h caused by offset saturation.
- **A part swapped, or an address clash, after a good start-up (D-1).** A Device_ID mismatch seen by a read
  (`ReadTemperatureRaw(Verify::Device)`, a status poll) returns `DeviceIdMismatch`, but only `CheckHealth()` uninitialises
  the driver; the setters and `ProgramUserWord()` that follow still write to whatever answers at the
  address, and `ProgramUserWord()` can program one word of it before its final identity read fails. It needs a part
  exchanged or a second device appearing on a running bus; the switch-off (N2) never writes to another part. After a
  `DeviceIdMismatch`, call `Deinitialize()` and `EnsureInitialized()` before any other call.
- **Rows W1, W2, R1 and U6 rest on one truthful EEPROM_UL reply (D-3).** One reply that reads 0000h (a stuck-0 line) or
  has bit 14 flipped, at the moment the chip is loading or programming, reads as "clean, EUN = 0, not busy": row U1
  sends the shutdown write into the load or into a programming word, where it is ignored or forbidden. A second reset
  between the last read and that write, and a reset no read has shown before an initialised switch-off or setter, are
  the same class as the one above (FC-10). Whole-call failure of every read while the chip loads is row U5.
- **Stale one-shot result under the alternative reading of A12 (D-4).** If a Configuration read in therm mode does not
  clear Data_Ready, a Data_Ready left by an earlier continuous conversion that nobody read can complete a one-shot
  at once: `ReadOneShotMilliC()` returns the previous result with `Ok` while the new conversion still runs. The chip
  ends in shutdown on its own, so no switch-off is affected; read Temp_Result once (or call `ReadTemperatureMilliC()`)
  before starting a one-shot in therm mode if the board's chip reads that way
  ([A12](ambiguities.md#a12-data_ready-in-therm-mode)).
- **Row U5, a write sent blind.** No clean read-back of the last lock write: the shutdown word is sent, programmed
  when the lock did not take ([A21](ambiguities.md#a21-start-up-and-switch-off-on-an-unlocked-chip)), and ignored by
  a chip that is busy at that moment.

## Depends on

| Dependency | Version | Why |
| --- | --- | --- |
| A C++20 compiler | GCC 10, Clang 13 or later | Concepts constrain the port |
| CMake | 3.16 or later | Generates `tmp117_version.h` |
| Your port | | The bus, 5 kHz to 400 kHz, with a repeated START and address 0 for one byte; the delay; optionally the ALERT pin and logging ([Porting](porting.md)) |

Nothing else: no RTOS, no SDK, no other repository of the org.

## Threading

The driver is not thread-safe and takes no lock. Use each driver object from one task at a time, or guard every
call on it with one mutex; the layer that owns the driver owns that lock, and the bus lock when several chips
share a bus. Nothing may run in an ISR: every call can block for a bus transfer or a delay. Separate objects
share no state.

## Errors

`tmp117::Error` inside `tmp117::Result<T>`. Values 0 to 9 are the shared base set; this driver returns every one of
them except `Crc` and `DeviceFault`, because the TMP117's bus has no checksum and the chip reports no sensing fault.
Values from 16 are specific to this chip.

| `Error` | Value | Returned when |
| --- | --- | --- |
| `Ok` | 0 | The call succeeded |
| `NotInitialized` | 1 | `EnsureInitialized()` has not succeeded, a reset was seen, or `Deinitialize()` ran since |
| `InvalidArgument` | 2 | A `Config` field or argument is out of range (`Validate()`); nothing was sent |
| `Bus` | 3 | A port hook returned `false` |
| `Timeout` | 4 | The power-up load, a conversion, `WaitDataReady()` or an EEPROM word did not finish in time; a switch-off found the EEPROM still busy after 14 ms (nothing sent) |
| `DeviceIdMismatch` | 5 | Device_ID is not `0x117` in bits 11:0; a switch-off found another part (nothing sent) |
| `Crc` | 6 | Never returned (no CRC on this chip) |
| `Busy` | 7 | A one-shot is pending |
| `DeviceFault` | 8 | Never returned (the chip reports no sensing fault) |
| `Unsupported` | 9 | `ReadAlertPin()` without the hook, `GeneralCallReset()` without the opt-in, `SmbusAlertResponse()` |
| `UnexpectedReset` | 16 | Reset evidence: the chip restarted behind the driver's back. The driver is uninitialised afterwards |
| `NoMeasurement` | 17 | Temp_Result reads `8000h` and no result has been seen since the start |
| `EepromVerifyFailed` | 18 | The EEPROM readback differs from what was written, or the chip could not be locked again |
| `BadReply` | 19 | A bit that must read 0 reads 1, or a register does not read back what was written |

The [API guide](api-guide.md#handle-errors) says what to do about each.

## Memory

No heap, no exceptions, no RTTI, no `std::function`, no `std::string`, no `printf` and no static mutable state. A
driver object holds the bus reference, the address, two `Config` copies, a few flags and the transfer buffers (a
3-byte write and a 2-byte read): about 100 bytes (104 with the mock port). No stack buffer is larger than 8 bytes
(the register words of a programming call); the setters copy a `Config` (32 bytes) on the stack.

## Platform

Platform-free: the same code runs on any target with a C++20 compiler, including the host tests. The ESP-IDF port
in [`examples/esp32`](examples.md) is an example, not part of the driver.

## Deviations from the standard

None.
