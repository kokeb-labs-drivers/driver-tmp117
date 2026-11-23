# Troubleshooting

Symptoms, causes and fixes, cheapest check first.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SNOSD82D D:

    - Device ID and the revision field: 7.6.11 Device ID Register, [p. 32](https://www.ti.com/lit/gpn/TMP117#page=32)
    - Self-heating and averaging noise: 8.2.2 Detailed Design Procedure, [p. 34](https://www.ti.com/lit/gpn/TMP117#page=34)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## EnsureInitialized returns Bus

The port reported a failure: either `EnsureInitialized()` returned `false` or a transfer did.

1. Check that your port's `EnsureInitialized()` returns `true` on its own (log it).
2. Check the bus pins in your port against the board, and that SDA and SCL have pull-ups.
3. With a logic analyser, check that the first frame (a read of Configuration or Device_ID at the address you passed)
   appears on the bus and is acknowledged.

## EnsureInitialized returns DeviceIdMismatch

Device_ID did not read `0x117` in bits 11:0. The chip answered with something else, or nothing answered.

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| `Bus` on the first transaction | No ACK at the address | Check ADD0 against the address you passed, the pull-ups and V+ |
| `0x0000` or `0xFFFF` from Device_ID | SDA stuck, or a different device at the address | Scan the bus; check SDA continuity |
| `0x0116` or `0x1116` | A TMP116 (the driver does not support it) | Check the part number on the package |
| `0x0118` or similar | Another part at the address | Check the part number on the package |

A TMP119 reads `0x2117`, which the driver cannot tell from a TMP117 of revision 2 and therefore accepts
([A13](ambiguities.md#a13-the-revision-field)).

## EnsureInitialized returns Timeout

EEPROM_Busy did not clear after the power-up load. Check V+ (1.8 V to 5.5 V), that the supply rises cleanly, and
that `reset_timeout_ms` is not shorter than the 1.5 ms the datasheet gives plus your port's delay overhead.

## EnsureInitialized returns BadReply

A bit that must read 0 read 1, or a register did not read back what was written. That is a noisy bus or a line that
is not what it should be: lower the clock, shorten the wires, check the pull-ups, and run with `Verify::Device` to see
whether the line itself is stuck.

## Reads return NoMeasurement

No conversion has finished since the start: a continuous read within the first cycle after `EnsureInitialized()`, or a
chip in shutdown. Wait `GetCycleTimeUs(cycle_time, averaging)` and read again. With 64 averaged conversions the first
result takes a second or more.

## Reads return UnexpectedReset

The driver saw evidence of a reset: the setting bits of Configuration changed, EEPROM_Busy was set when no programming
was running, or a result of `8000h` came after a real one. The usual cause is a brown-out on V+. The driver is
uninitialised and has stopped writing; `EnsureInitialized()` writes the configuration again. If it repeats, check the
supply and its decoupling.

## ReadOneShotMilliC returns Timeout

The conversion took longer than `(n + 1) x 17.5 ms` for `n` averaged conversions. Check that your port's `DelayUs()`
really waits: a delay that returns at once makes every timeout far too short. The driver sends shutdown before it
returns.

## SetMode, SetAveraging and the other setters return Busy

A one-shot is pending. Wait for it, or end it with `SetMode(Mode::Shutdown)`.

## ProgramPowerOnDefaults returns EepromVerifyFailed

Either the readback after the reset differs from what was written, or the chip could not be locked again. Power-cycle
the chip, then read the registers with `ReadRegister()` and compare. The EEPROM allows 1000 writes; a chip that
fails to hold the values after a handful of writes is suspect. Read [A5 and A6](ambiguities.md#a5-what-the-soft-reset-does)
for what the datasheet leaves open about the relock.

## ALERT does not fire

1. Check the polarity: the default is active low, and the pin needs a pull-up.
2. In therm mode ALERT stays on above the high limit until the result falls below the low limit; in alert mode it
   follows the flags, which clear when Configuration is read. Use `GetLatchedStatus()` to see what was set.
3. `ReadAlertPin()` returns `Unsupported` unless the port has the hook.
4. The comparison at exactly the limit may differ from your expectation ([A8](ambiguities.md#a8-at-exactly-the-limit)).

## The temperature reads a few degrees high

Self-heating of the board or the chip: a long cycle with shutdown between conversions heats less. Measure the offset
against a reference and set `Config::offset_mc`; the chip adds it to every result.
