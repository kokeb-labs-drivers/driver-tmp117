# Ambiguities

Where the datasheet is silent, says two things, or gives only a typical value, and what the driver does about it.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SNOSD82D D:

    - Power-up, the EEPROM load and the first result: 7.3.1 Power-Up, [p. 12](https://www.ti.com/lit/gpn/TMP117#page=12)
    - EEPROM, unlock and programming: 7.5.1 EEPROM Overview, [p. 18](https://www.ti.com/lit/gpn/TMP117#page=18)
    - SMBus alert response and general call: 7.5.3.1.5 SMBus Alert Function; 7.5.3.1.6 General-Call Reset Function, [p. 22](https://www.ti.com/lit/gpn/TMP117#page=22)
    - Limit registers and the offset register: 7.6.4 THigh_Limit; 7.6.5 TLow_Limit (p. 29); 7.6.9 Temperature Offset (p. 31), [p. 29](https://www.ti.com/lit/gpn/TMP117#page=29)
    - Device ID and the revision field: 7.6.11 Device ID Register, [p. 32](https://www.ti.com/lit/gpn/TMP117#page=32)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

Every row is a reading of SNOSD82D (Rev. D) that could be wrong. The driver picks the reading that is safe when the
other one is true, and the host model has a switch for each reading, so the whole safety test suite runs under both
(`tests/test_faults.cpp`, `tests/test_eeprom.cpp`). Nothing here was measured on a chip: the driver is at hardware
level H0, so a bench run is what turns a reading into a fact.

## Decisions taken for the first release

These four questions had more than one reasonable answer; the owner's defaults are recorded here with the knob that
changes them.

### Q1: how the chip is locked again after programming

The datasheet describes the EEPROM unlock bit (EUN) and says the general-call reset or a power cycle clears it. A
general-call reset resets every device on the bus that answers the general call, not only this chip. The driver
therefore **locks with a soft reset by default**, after writing EUN = 0 and reading it back, and it sends the
general-call reset only when the caller sets `Config::allow_general_call`. If the lock cannot be confirmed and the
opt-in is off, the call returns `EepromVerifyFailed` and tells you to power-cycle the chip. The soft-reset word is
never written into the EEPROM.

*Knob:* `Config::allow_general_call` (driver), `eun_write_zero_locks`, `soft_reset_clears_eun` (model).

### Q2: limits in the wrong order

`SetLimits()` and `Validate()` **refuse a low limit above the high limit**. See [A10](#a10-limits-in-the-wrong-order).

### Q3: SMBus alert response

The SMBus alert response is a read of the bus-wide address `0x0C`, in which every alerting device answers and the
lowest address wins. It cannot be done safely through a port that models one device, and the chip's ALERT pin plus
`ReadStatus()` already tell the host which chip alerts. **`SmbusAlertResponse()` returns `Unsupported` and sends
nothing** in this release.

*Knob:* none; the model counts `ara_reads` to prove nothing is sent.

### Q4: the two waits

The datasheet gives the power-up load (1.5 ms) and one EEPROM word (about 7 ms) as typical values only. The driver
waits the typical time and then polls EEPROM_Busy up to a deadline of **10 ms** and **50 ms**: see
[A1 and A2](#a1-and-a2-the-two-waits). The switch-off of a driver that lost track of the chip does not poll: it has
its own budget of two 7 ms waits and returns `Timeout` with nothing sent when the chip is still busy.

## What the datasheet leaves open

| Id | The question | What the driver does | Model knob |
| --- | --- | --- | --- |
| [A1](#a1-and-a2-the-two-waits) | The power-up load takes 1.5 ms typical, no maximum | Wait 1.5 ms, then poll EEPROM_Busy, deadline `reset_timeout_ms` (10 ms) | `por_load_us` |
| [A2](#a1-and-a2-the-two-waits) | One EEPROM word takes about 7 ms typical, no maximum | Wait 7 ms, then poll, deadline `eeprom_timeout_ms` (50 ms) | `eeprom_program_us` |
| [A3](#a3-the-user-word-after-a-reset) | EEPROM2 reads 0000h in one place and an unknown value in another | Never assume its value | `eeprom2_reset` |
| [A4](#a4-is-the-offset-stored-in-eeprom) | Whether Temp_Offset is a stored power-on value | Program it only when asked, as a power-on value, and read it back | `offset_in_eeprom` |
| [A5](#a5-what-the-soft-reset-does) | Whether a soft reset reloads the EEPROM and clears EUN | Lock with EUN = 0, then reset, then confirm by reading EUN | `soft_reset_clears_eun`, `soft_reset_reloads` |
| [A6](#a6-writing-eun-as-0) | Whether writing EUN = 0 locks without a reset | Always write EUN = 0 first, then confirm | `eun_write_zero_locks` |
| [A7](#a7-the-offset-at-the-minimum-and-8000h) | A saturated minimum could equal the 8000h no-conversion value | 8000h is never a temperature | `saturate_min_8001` |
| [A8](#a8-at-exactly-the-limit) | Whether "exceeds" means above or at the limit | Documented as strict; the tests pass under both | `limit_compare_inclusive` |
| [A9](#a9-a-reset-to-identical-values) | There is no power-on flag | Evidence from the setting bits, EEPROM_Busy and 8000h; `CheckHealth()` | none (a blind case, tested) |
| [A10](#a10-limits-in-the-wrong-order) | Low above high in therm mode is not described | Refused everywhere | none (policy) |
| [A11](#a11-the-alert-flag-after-a-one-shot) | Whether a one-shot sets the alert flag by comparison or always | Treated as by comparison | `oneshot_sets_alert` |
| [A12](#a12-data_ready-in-therm-mode) | Whether a Configuration read in therm mode clears Data_Ready | Latch Data_Ready on every read | `therm_read_clears_dr` |
| [A13](#a13-the-revision-field) | The Rev field has no defined values | Accept any, report it | `device_rev` |
| [A14](#a14-writes-during-the-power-up-load) | A write during the load is "ignored": ACK or NACK | Never write after a reply that shows the load; a reset no read has shown is a residual risk | `por_write_nack` |
| [A15](#a15-the-bus-timeout) | Bus timeout is 35 ms typical and 20 to 40 ms in the table | Port contract: transactions far below 20 ms | none (port) |
| [A16](#a16-the-oscillator-accuracy) | The cycle time has no stated accuracy | A one-shot waits one conversion longer than needed | `osc_error_pct` |
| [A17](#a17-the-two-traceability-words) | One text says not to erase EEPROM1, another words it for EEPROM[1] | Protect both 05h and 08h | none (policy) |
| [A18](#a18-other-parts-in-the-family) | TMP116 and TMP119 IDs are not in this datasheet | Only the TMP117 ID passes, with the A13 consequence | `device_id` |
| [A19](#a19-mod-while-a-one-shot-runs) | What MOD reads after a one-shot, 01 or still 11 | A one-shot is complete when Data_Ready is seen or MOD reads 01; both readings work | `oneshot_mod_reads_back_11` |
| [A20](#a20-programming-the-power-on-defaults-with-continuous-mode) | Conversions restart while the last word is programmed when the defaults are continuous | Documented; choose shutdown in the defaults | none (policy) |
| [A21](#a21-start-up-and-switch-off-on-an-unlocked-chip) | The chip can be left unlocked by an interrupted programming | Read EEPROM_UL at start-up and lock first; start-up writes nothing when the lock cannot be confirmed; switch-off writes the lock, then sends unless the read after the last lock write is clean with EUN = 1 | `ForceUnlock()`, `eun_write_zero_locks` |
| [A22](#a22-eeprom_ul-during-the-power-up-load) | Whether EEPROM_UL can be written, and EUN read, during the load | No write follows a busy EEPROM_UL reply, so the driver does not depend on it | none |

### A1 and A2: the two waits

SNOSD82D gives 1.5 ms (typical) for the load of the registers from EEPROM after power-up (6.5, p. 7; 7.3.1, p. 12)
and about 7 ms (typical) per EEPROM word (6.6, p. 7), and prints no maximum for either. A fixed wait of the typical
time would be wrong on a slow part, so the driver waits that time and then polls EEPROM_Busy (bit 12 of Configuration,
bit 14 of EEPROM_UL) until it reads 0, up to `reset_timeout_ms` (10 ms) or `eeprom_timeout_ms` (50 ms). Start-up and
programming write nothing before EEPROM_Busy has read 0. If the deadline passes the call returns `Timeout` and the
driver stays uninitialised. The switch-off of a driver that lost track of the chip uses neither deadline: after a
busy EEPROM_UL reply it waits 7 ms and reads again, twice at most in the call (14 ms), and returns `Timeout` with
nothing sent when the chip is still busy ([decision table](contract.md#switch-off-decision-table), rows W1, W2, U6).
The model's `por_load_us` and `eeprom_program_us` stretch the times to prove every outcome.

*If you know the maximum:* raise or lower the two `Config` fields; they are limited to 1 ms to 1000 ms.

### A3: the user word after a reset

The EEPROM2 register is listed with a reset value of 0000h in its own section and with an unknown value in the register
map, and the overview names four general-purpose words while the map has three (7.5.1.1, 7.6.8, Table 7-3). The driver
does not use the user word for anything, reads it only in `ReadUniqueId()`, and writes it only in
`ProgramUserWord()`. `eeprom2_reset` in the model sets its initial value.

### A4: is the offset stored in EEPROM

Temp_Offset is listed among the registers that load from EEPROM at power-up (Table 7-3, note 1), but the text counts
the user-programmable words differently. `ProgramPowerOnDefaults()` programs the offset word because the caller
passes a `Config` that has an offset, and it reads the word back after the reset; with `offset_in_eeprom = false` the
model keeps the offset out of EEPROM, and the verification then reports `EepromVerifyFailed` for a non-zero offset
instead of claiming success.

### A5: what the soft reset does

Table 7-6 describes Soft_Reset (Configuration bit 1) as a 2 ms reset, and the register figure shows it as read-only
while the table says write. The datasheet does not say whether the soft reset reloads the EEPROM and clears EUN the
way the general-call reset does. The driver relocks with EUN = 0 ([A6](#a6-writing-eun-as-0)), resets
(soft by default, [Q1](#q1-how-the-chip-is-locked-again-after-programming)), then **reads EUN and refuses to
succeed unless it reads 0**.

Two consequences are worth knowing:

- If the soft reset does *not* reload the EEPROM (`soft_reset_reloads = false`), the verification reads back the
  registers the driver wrote, not the EEPROM, so the verify step is vacuous: it can pass without the EEPROM holding
  the values. The only proof is a power cycle and a read of the registers. The tests pin this reading and the docs
  say so; an application that must be sure powers the chip down once after programming.
- If the soft reset does not clear EUN (`soft_reset_clears_eun = false`) and writing EUN = 0 does not lock either, the
  driver falls back to the general-call reset when `allow_general_call` is set, and otherwise returns
  `EepromVerifyFailed` with the chip still unlocked, which is why the error text says to power-cycle it.

### A6: writing EUN as 0

The datasheet says the general-call reset or a power cycle clears the unlock bit, and describes EUN as a read/write bit
without saying that writing 0 locks. The driver writes 0 anyway (up to three attempts, each after waiting for
EEPROM_Busy to clear, up to `eeprom_timeout_ms`; after that deadline the lock is written whatever EEPROM_Busy reads,
a [residual risk](contract.md#residual-risks) of a faulty chip) because a locked chip is the safe state, and confirms
by reading EUN. With `eun_write_zero_locks = false` the model ignores that write and only a reset locks it.

### A7: the offset at the minimum and 8000h

A result that saturates at the minimum would be 8000h, the same word the chip reports before any conversion (7.6.2,
7.6.9). The driver treats 8000h as "no result" or, after a result was seen, as a reset, and never returns it as a
temperature. `saturate_min_8001` makes the model saturate at 8001h instead, which the driver reads as -255.99 °C. If
your offset can push a real temperature to the lower end, the call returns an error and not a wrong number.

### A8: at exactly the limit

The text says a result "exceeds" the high limit and is "lower than" the low limit (7.4.4.1, 7.4.4.2). The driver
documents the strict reading and its tests pass under both: a result one count beyond the limit is flagged in both,
a result exactly at the limit only with `limit_compare_inclusive`. A system that must act at the limit should set the
limit one count (7.8125 m°C) inside it.

### A9: a reset to identical values

The chip has no power-on flag. If it resets and its EEPROM holds exactly the values the driver wrote, nothing in the
registers changes, and the only trace is that the next result reads 8000h until the first conversion finishes. The
driver reports `UnexpectedReset` on any of the three pieces of evidence (setting bits changed, EEPROM_Busy set
unexpectedly, 8000h after a result), and `CheckHealth()` re-reads them on demand, but a reset between two
conversions to identical values can pass unseen. The evidence comes from Configuration only: a reset that reloads
the driver's Configuration word but other limits or another offset passes too, once the first conversion is done
(the alert limit and the calibration offset are then the EEPROM's). If that matters, program the power-on defaults so
a reset restores the configuration, and use `Verify::Device` with periodic `CheckHealth()`.

### A10: limits in the wrong order

Therm mode with the low limit above the high limit is not described (7.4.4.2). The driver refuses it with
`InvalidArgument`, sending nothing, in `Validate()`, `SetLimits()`, `Configure()` and `ProgramPowerOnDefaults()`.
The two limits are two register writes, so the driver writes them in the order that never puts the low limit above
the high limit, in the chip or in its own copy, if the second write fails.

### A11: the alert flag after a one-shot

7.4.3 says the data-ready and alert flags are set at the end of a one-shot without saying whether the alert flag is set
by comparison. The driver treats the flags as it does for continuous conversion. `oneshot_sets_alert` makes the model
set the flag unconditionally; the tests show that the driver still returns the temperature and does not act on a flag
alone.

### A12: Data_Ready in therm mode

The text says a read of Configuration in therm mode does not clear the alert bits, and is silent about Data_Ready. The
driver latches Data_Ready on every read, so it is not lost either way (`therm_read_clears_dr`). Under the reading that
a Configuration read does not clear it, a Data_Ready left by an unread continuous conversion can complete a one-shot at
once and `ReadOneShotMilliC()` returns the previous result: a [residual risk](contract.md#residual-risks) (D-4).

### A13: the revision field

Device_ID bits 15:12 are the revision; the register figure shows them as unknown and the table gives a reset of 0h. No
revision value is documented. The driver masks the field, accepts any, and returns it from `GetRevision()`. The
consequence is that a TMP119, whose ID is `0x2117`, reads as revision 2 of a TMP117 and is accepted: the driver cannot
tell them apart, and it does not claim to. `device_rev` varies the field in the model. A TMP116 (`0x1116` in other
projects) fails the check with `DeviceIdMismatch`.

### A14: writes during the power-up load

The datasheet says writes during the load are ignored and does not say whether they are acknowledged. Start-up and
the resets write nothing until EEPROM_Busy has read 0 (twice in a row where it matters), and the switch-off of a
driver that lost track never writes after an EEPROM_UL reply that shows the load (rows W1 and R1, also when a settle
found the reset first, row I3), so for every path that holds evidence of the reset the answer does not change what
the driver does. `por_write_nack` makes the model NACK such writes; the row tests run under both readings.

One case remains, a [residual risk](contract.md#residual-risks): a reset that no read has shown yet (a chip-only
brown-out, another master's general call) within 1.5 ms before an initialised switch-off or a setter. Those send
with no read first, so the write is ignored: under the ACK reading the call returns `Ok`, under the NACK reading
`Bus`. The next call that reads Configuration reports `UnexpectedReset`.

### A15: the bus timeout

The text gives 35 ms typical for the bus timeout and the table gives 20 ms to 40 ms (7.5.3.1.7, 6.7). The port
contract asks for SCL of at least 5 kHz and transactions far below 20 ms. The datasheet lists 1 kHz as the lowest SCL
(6.7, p. 7), but a 3-byte write takes 36 clocks (the address and three bytes, nine each), about 36 ms at 1 kHz, above
the 20 ms minimum timeout, so 5 kHz is the floor this driver states. The driver has no code for it.

### A16: the oscillator accuracy

Table 7-7 gives nominal cycle times with no tolerance. A one-shot therefore waits `(n + 1) x 17.5 ms` for `n`
averaged conversions: one conversion more than needed. `osc_error_pct` slows the model's oscillator to show the
margin holds up to 10 percent.

### A17: the two traceability words

The text says the traceability identifier in EEPROM1 and EEPROM3 must not be erased, and one passage words it as
"EEPROM[1]". The driver protects 05h and 08h both: `RegisterBus::WriteWord` refuses them, and the EEPROM sequence
compares them before and after.

### A18: other parts in the family

The IDs of the TMP116 and the TMP119 come from other projects, not from a datasheet on file, so only `0x117` in bits
11:0 passes. The cost is the TMP119 case in [A13](#a13-the-revision-field). `device_id` in the model changes the ID.

### A19: MOD while a one-shot runs

The datasheet says only that the device "goes to the low-power shutdown mode" when the conversion is done (7.4.3,
p. 15); what MOD reads afterwards is not stated. Two readings are possible: MOD reads 01 (shutdown) when the
conversion has finished, or MOD keeps reading 11 until the next write. The driver works with both: a pending one-shot
is **complete when Data_Ready has been seen since the start, or MOD reads 01**, and while it is pending MOD 11 and
MOD 01 are both accepted (only MOD 00, the power-on mode, is reset evidence). When the conversion ended with MOD still
11, the driver treats 11 as shutdown until its next Configuration write, so a later `CheckHealth()` or settle does
not read it as a reset. `WaitDataReady()` returns when the pending one-shot has completed by either rule.

One window remains open: a one-shot started from continuous mode clears Data_Ready with its pre-read, but a
continuous conversion that ends between that read and the MOD = 11 write sets it again. The driver then counts the
one-shot complete on that flag, and `ReadOneShotMilliC()` returns the continuous conversion's result (a real
temperature, at most one cycle old). The window is the time between two bus transfers. To avoid it, call
`SetMode(Mode::Shutdown)` before `StartOneShot()`. *Bench step, not hardware-verified:* start a one-shot from
continuous mode at the end of a conversion and compare the result with the next one.

Data_Ready clears on the read that sees it, so the driver remembers it. A raw `ReadRegister()` of Configuration while
a one-shot is pending is allowed and remembered the same way; a raw `ReadRegister()` of Temp_Result (which also clears
Data_Ready) is refused with `Busy` while the one-shot is pending, because its value is not valid yet and the driver
could not see the flag go. The model's `oneshot_mod_reads_back_11` selects the second reading; the safety suite runs
under both. *Bench step, not hardware-verified:* read Configuration after a one-shot and record MOD.

### A20: programming the power-on defaults with continuous mode

`ProgramPowerOnDefaults()` writes the defaults, including a continuous MOD, into Configuration while EUN is set. The
datasheet (7.5.1.2) asks to avoid conversions while the EEPROM is unlocked. Only MOD1 is volatile (Table 7-6, notes 1
and 2, p. 28): MOD0 is stored, which is how a continuous power-on mode is programmed, so with continuous defaults the
chip converts from that write on, for the 7 ms of its programming and until the relock reset. The driver writes the
Configuration word last to keep that window short. Choose `Mode::Shutdown` in the defaults to avoid it.

### A21: start-up and switch-off on an unlocked chip

With EUN = 1 every write to a programmable register is stored in EEPROM (7.5.1.2, p. 18, Table 7-10, p. 30), and EUN
is cleared only by a general-call reset or a power cycle (and, by [A6](#a6-writing-eun-as-0), perhaps by a write of 0).
A host that restarts during `ProgramPowerOnDefaults()`, or a lock that failed and was left to a power cycle, leaves the
chip unlocked while the driver starts again with no memory of it. Writing the limits, the offset and the
configuration would program them as power-on defaults, and every later setter would program EEPROM again (1000 writes
minimum endurance, section 6.6).

What the driver does: after Device_ID and before its first register write, `EnsureInitialized()` reads EEPROM_UL. With
EUN = 1 it writes EUN = 0 and reads it back (up to three times, as in the relock). If EUN still reads 1 the call
returns `EepromVerifyFailed` (or the bus error of the failed write) and has written nothing else; the application
power-cycles the chip or sends the general-call reset itself. Start-up never writes anything else without a
confirmed lock.

A lock is confirmed only by a clean read-back with EUN = 0: a reply that breaks bits 13:0 is no lock, at start-up as
in the relock after programming, even when its EUN bit reads 0.

Switch-off is different, because never switching the chip off is the worse failure (RD-05). Its rules are the
[switch-off decision table](contract.md#switch-off-decision-table). When the driver is not initialised,
`Deinitialize()` and `SetMode(Mode::Shutdown)` read EEPROM_UL once. EUN = 0 on a clean reply: the shutdown write is
sent. Otherwise (EUN = 1, a failed read, or a reply that breaks bits 13:0) the driver writes EUN = 0 and reads it
back, at most twice; the lock write programs no EEPROM. Only the read after the last lock write decides: when it is
clean and shows EUN = 1, the lock cannot be cleared and the write would be programmed as a power-on default of
"shutdown" (the chip would then need a power cycle anyway), so it is refused with `EepromVerifyFailed` and nothing is
sent. A clean EUN = 1 seen before a later lock write is stale and never refuses; a failed or garbled read is no
evidence. A chip that answers neither the first read nor the lock write gets nothing more (`Bus`). The residual risk
is stated: if every lock write was lost or did not take and no read-back succeeded, the shutdown word is programmed.
No write follows a clean EEPROM_UL reply with EEPROM_Busy set (the power-up load or a word being programmed): the
driver waits 7 ms and reads again, twice at most, and returns `Timeout` with nothing sent when the chip is still
busy. The whole call makes at most 8 transfers and waits at most 14 ms. An initialised driver is always locked (it
became initialised only after a confirmed lock), so its switch-off needs no extra read, unless its settle finds reset
evidence (row I3).

Two residual risks are stated in the [contract](contract.md#residual-risks): row U5 above, and one bit flipped on the
bus in the first EEPROM_UL reply of an unlocked chip (EUN read as 0 with bits 13:0 clean). The bus has no CRC, so the
driver takes that reply for a lock and writes one word: start-up programs one limit as a power-on default and then
fails its read-back, switch-off row U1 programs the shutdown word. It needs an interrupted programming and a bus
error on exactly that read; the cost is one EEPROM word.

*Knob:* the model's `ForceUnlock()` leaves EUN set as an interrupted programming does; `eun_write_zero_locks = false`
makes the lock write ineffective. *Bench step, not hardware-verified:* interrupt a programming and read EEPROM_UL at
the next start.

### A22: EEPROM_UL during the power-up load

7.5.1.1 names only the limit registers and Configuration as ignored during the load; it says nothing about a write to
EEPROM_UL then, or about whether EUN reads valid while EEPROM_Busy is set. The switch-off writes nothing after a busy
EEPROM_UL reply and reads again after the wait, so neither answer changes what it does. A lock write after a failed
read (no evidence) may still land in a load; it programs nothing either way.

*Knob:* none. *Bench step, not hardware-verified:* power-cycle the chip and read EEPROM_UL within 1 ms.

## How the safety tests use these

`tests/test_faults.cpp` runs a whole session (start, set limits and alert function, read, one-shot, program a user
word, deinitialise) under each reading above and checks that the driver never wrote while the chip was loading or busy,
never wrote a traceability or read-only register or an undefined pointer, and never left the EEPROM unlocked. That
session waits out every load it causes; the switch-off rows W1, I3 and R1 in `tests/test_switch_off.cpp` switch off
inside the load, with no time passing, under both readings of A14.
`tests/test_eeprom.cpp` covers A2, A5 and A6 in both readings and every failure path of the relock;
`tests/test_safety_regressions.cpp` covers A19 and A21 in both readings, and `tests/test_switch_off.cpp` every row of
the switch-off decision table.
