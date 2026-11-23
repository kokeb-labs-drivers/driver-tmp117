# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Added

- First version of the TMP117 driver (Texas Instruments TMP117, I2C), written from datasheet SNOSD82D Rev. D.
- Start-up that waits for the power-up load, checks Device_ID (any revision), writes the limits, the offset and the
  configuration and reads them back.
- Continuous, shutdown and one-shot conversion, averaging of 1, 8, 32 or 64 conversions, the eight cycle times of
  Table 7-7 as data, alert and therm mode, ALERT polarity and source, limits and offset in thousandths of a degree.
- A latch for the flags the chip clears on read, reset evidence without a reset flag, and a settle step after a failed
  write.
- Start-up reads EEPROM_UL and locks an unlocked chip before its first write, and refuses (writing nothing else) when
  the lock cannot be confirmed; switch-off sends even when settling a write in doubt failed.
- Switch-off follows one decision table (docs/contract.md): an uninitialised driver writes the EEPROM lock and sends
  the shutdown write unless the read after its last lock write is clean with EUN = 1 (A21); a failed, garbled or
  earlier read never refuses. No write follows an EEPROM_UL reply that shows the power-up load or a word being
  programmed: two 7 ms waits at most, then `Timeout` with nothing sent. A settle that finds a reset hands over to these
  rows, and after a Device_ID mismatch nothing is sent to another device. At most 8 transfers (9 after a mismatch)
  and 14 ms; a chip that answers nothing costs 2 transfers. A settle read that shows EEPROM_Busy counts as reset
  evidence. The code builds on GCC 10 and Clang 13 (no `<bit>`).
- A failed shutdown write while a one-shot runs is settled as "not taken", not as a reset.
- A one-shot counts as complete when Data_Ready was seen or MOD reads shutdown, so either reading of ambiguity A19
  works; `ReadStatus()` confirms a reply of 0000h with Device_ID.
- A guarded EEPROM path (`ProgramPowerOnDefaults()`, `ProgramUserWord()`) that relocks on every failure path and never
  writes the two traceability words.
- An optional `ReadAlertPin()` hook, `GeneralCallReset()` behind an explicit opt-in, and a documented list of the
  datasheet's ambiguities with the model switch that tests each reading.
- Host tests against a register model written from the datasheet, a fuzz target, a host example, an ESP-IDF example and
  the documentation site with its figures.
- Not included: the SMBus alert response (returns `Unsupported`), the TMP116 and TMP119, any run on hardware.
