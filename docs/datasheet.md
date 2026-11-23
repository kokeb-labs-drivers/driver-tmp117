# Datasheet map

The driver follows the TMP117 datasheet. This page says which revision, where to get it, and where each part of
the driver is described in it, so you can check any behaviour against the source. Section names and page numbers
are the datasheet's own (the printed page numbers).


<!-- generated: datasheet-map -->
- **Datasheet: TMP117 High-Accuracy, Low-Power, Digital Temperature Sensor With SMBus- and I2C-Compatible Interface**,
  Texas Instruments SNOSD82D, revision D (September 2022). Get it from the [vendor
  page](https://www.ti.com/product/TMP117) or as a [PDF](https://www.ti.com/lit/gpn/TMP117).
  SHA-256 of the file used: `78212e91e478035a91165397a8de8a90bfc776759dc445243c033537f0c4b789`.
- **Application note: Precise Temperature Measurements With TMP116 and TMP117**, Texas Instruments SNOA986, revision A
  (2019-08). Get it from the [vendor page](https://www.ti.com/product/TMP117) or as a
  [PDF](https://www.ti.com/lit/pdf/SNOA986).
  SHA-256 of the file used: `dac8d45d612654b130deec775f64401a746b0a63a9b6402ec1ec400b93f17bec`.
- **Application note: Fixed-point interpretation of the temperature output**, Texas Instruments SBAA588, revision A
  (2025-01). Get it from the [vendor page](https://www.ti.com/product/TMP117) or as a
  [PDF](https://www.ti.com/lit/pdf/SBAA588).
  SHA-256 of the file used: `030ca48a844df836f6268e2960b2ea1fdf073eba07ee693b404303efa7848e50`.
- **Application note: Alert and therm threshold design across temperature sensors**, Texas Instruments SDAA311, revision
  none stated (2026-03). Get it from the [vendor page](https://www.ti.com/product/TMP117) or as a
  [PDF](https://www.ti.com/lit/pdf/SDAA311).
  SHA-256 of the file used: `e0f62e796fa5db026c2dc7ccada7d5df1f260e6093d0f32061ea15ed686e5264`.

| Topic | Datasheet section | Page | In the driver | Explained on |
| --- | --- | --- | --- | --- |
| Pins and packages | SNOSD82D: 5 Pin Configuration and Functions | [p. 4](https://www.ti.com/lit/gpn/TMP117#page=4) | `ReadAlertPin()`, `kAddressGnd` to `kAddressScl` | [Hardware](hardware.md), [Quickstart](quickstart.md) |
| Supply range, supply current and shutdown current | SNOSD82D: 6.3 Recommended Operating Conditions; 6.5 Electrical Characteristics | [p. 5](https://www.ti.com/lit/gpn/TMP117#page=5) | `Mode::Shutdown`, `Deinitialize()` | [Hardware](hardware.md) |
| Power-up, the EEPROM load and the first result | SNOSD82D: 7.3.1 Power-Up | [p. 12](https://www.ti.com/lit/gpn/TMP117#page=12) | `EnsureInitialized()`, `Config::reset_timeout_ms`, `Error::NoMeasurement` | [Hardware](hardware.md), [API guide](api-guide.md), [Ambiguities](ambiguities.md) |
| Result format, LSB and the 8000h value | SNOSD82D: 7.3.3 Temperature Result and Limits; Table 7-1 | [p. 13](https://www.ti.com/lit/gpn/TMP117#page=13) | `ReadTemperatureRaw()`, `ReadTemperatureMilliC()`, `CountsToMilliC()`, `MilliCToCounts()` | [API guide](api-guide.md), [Architecture](architecture.md) |
| Continuous, shutdown and one-shot modes | SNOSD82D: 7.4.1 Continuous Conversion; 7.4.2 Shutdown; 7.4.3 One-Shot | [p. 14](https://www.ti.com/lit/gpn/TMP117#page=14) | `Mode`, `SetMode()`, `StartOneShot()`, `ReadOneShotMilliC()`, `WaitDataReady()` | [API guide](api-guide.md), [Configuration](configuration.md) |
| Alert mode and therm mode | SNOSD82D: 7.4.4 Therm and Alert Modes | [p. 15](https://www.ti.com/lit/gpn/TMP117#page=15) | `AlertFunction`, `SetAlertFunction()`, `SetLimits()`, `ReadStatus()` | [API guide](api-guide.md), [Configuration](configuration.md), [Hardware](hardware.md) |
| EEPROM, unlock and programming | SNOSD82D: 7.5.1 EEPROM Overview | [p. 18](https://www.ti.com/lit/gpn/TMP117#page=18) | `ProgramPowerOnDefaults()`, `ProgramUserWord()`, `Error::EepromVerifyFailed` | [API guide](api-guide.md), [Ambiguities](ambiguities.md) |
| Traceability words that must not be erased | SNOSD82D: 7.5.1.1 EEPROM Overview; 7.5.1.2 Programming the EEPROM; 7.6.7 EEPROM1; 7.6.10 EEPROM3 | [p. 18](https://www.ti.com/lit/gpn/TMP117#page=18) | `ReadUniqueId()`, `reg::IsWritablePointer()` | [API guide](api-guide.md), [Contract](contract.md) |
| I2C addresses and the pointer register | SNOSD82D: 7.5.2 Pointer Register; 7.5.3.1.2 Serial Bus Address | [p. 20](https://www.ti.com/lit/gpn/TMP117#page=20) | `kI2cAddress`, `I2cInterface`, `RegisterBus` | [Hardware](hardware.md), [Porting](porting.md) |
| SMBus alert response and general call | SNOSD82D: 7.5.3.1.5 SMBus Alert Function; 7.5.3.1.6 General-Call Reset Function | [p. 22](https://www.ti.com/lit/gpn/TMP117#page=22) | `SmbusAlertResponse()`, `GeneralCallReset()`, `Config::allow_general_call` | [API guide](api-guide.md), [Ambiguities](ambiguities.md), [Porting](porting.md) |
| Bus timeout and the SCL range | SNOSD82D: 7.5.3.1.7 Timeout Function; 6.7 Two-Wire Interface Timing | [p. 22](https://www.ti.com/lit/gpn/TMP117#page=22) | `kMaxClockHz`, `kMinClockHz` | [Hardware](hardware.md), [Porting](porting.md) |
| Register map and reset values | SNOSD82D: 7.6 Register Map; Table 7-3 | [p. 25](https://www.ti.com/lit/gpn/TMP117#page=25) | `inc/tmp117_registers.hpp`, `Config{}` | [Configuration](configuration.md), [Architecture](architecture.md) |
| Configuration register | SNOSD82D: 7.6.3 Configuration Register; Table 7-6 | [p. 27](https://www.ti.com/lit/gpn/TMP117#page=27) | `ConfigWord()`, `DecodeStatus()`, `Status`, `GetLatchedStatus()` | [API guide](api-guide.md), [Architecture](architecture.md) |
| Averaging and the conversion cycle time | SNOSD82D: 7.3.2 Averaging; Table 7-7 | [p. 28](https://www.ti.com/lit/gpn/TMP117#page=28) | `Averaging`, `CycleTime`, `GetCycleTimeUs()` | [Configuration](configuration.md), [API guide](api-guide.md) |
| Limit registers and the offset register | SNOSD82D: 7.6.4 THigh_Limit; 7.6.5 TLow_Limit (p. 29); 7.6.9 Temperature Offset (p. 31) | [p. 29](https://www.ti.com/lit/gpn/TMP117#page=29) | `SetLimits()`, `SetOffsetMilliC()` | [Configuration](configuration.md), [Ambiguities](ambiguities.md) |
| Device ID and the revision field | SNOSD82D: 7.6.11 Device ID Register | [p. 32](https://www.ti.com/lit/gpn/TMP117#page=32) | `EnsureInitialized()`, `GetRevision()`, `Error::DeviceIdMismatch` | [Hardware](hardware.md), [Troubleshooting](troubleshooting.md), [Ambiguities](ambiguities.md) |
| Typical connections and pull-ups | SNOSD82D: 8.2 Typical Application | [p. 33](https://www.ti.com/lit/gpn/TMP117#page=33) | - | [Hardware](hardware.md) |
| Self-heating and averaging noise | SNOSD82D: 8.2.2 Detailed Design Procedure | [p. 34](https://www.ti.com/lit/gpn/TMP117#page=34) | - | [Hardware](hardware.md), [Troubleshooting](troubleshooting.md) |
| Packages and ordering | SNOSD82D: 12 Mechanical, Packaging, and Orderable Information | [p. 39](https://www.ti.com/lit/gpn/TMP117#page=39) | - | [Hardware](hardware.md) |
<!-- /generated -->

## When the datasheet and the driver differ

The driver is written against the revision above. If you have a newer revision, compare its revision history with
the topics above; a change to a pin, a register, a timing or a value the driver uses is a bug report for this
repository. Where the driver deliberately goes beyond the datasheet, the [Contract](contract.md) says so.

The repository never contains the PDF itself: vendors own their documents. Download it from the vendor, and compare
its SHA-256 with the one above to confirm you have the same file.
