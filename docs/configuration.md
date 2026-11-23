# Configuration

Every field of `tmp117::Config`, the presets, how a configuration is checked, and the optional Kconfig layer.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SNOSD82D D:

    - Continuous, shutdown and one-shot modes: 7.4.1 Continuous Conversion; 7.4.2 Shutdown; 7.4.3 One-Shot, [p. 14](https://www.ti.com/lit/gpn/TMP117#page=14)
    - Alert mode and therm mode: 7.4.4 Therm and Alert Modes, [p. 15](https://www.ti.com/lit/gpn/TMP117#page=15)
    - Register map and reset values: 7.6 Register Map; Table 7-3, [p. 25](https://www.ti.com/lit/gpn/TMP117#page=25)
    - Averaging and the conversion cycle time: 7.3.2 Averaging; Table 7-7, [p. 28](https://www.ti.com/lit/gpn/TMP117#page=28)
    - Limit registers and the offset register: 7.6.4 THigh_Limit; 7.6.5 TLow_Limit (p. 29); 7.6.9 Temperature Offset (p. 31), [p. 29](https://www.ti.com/lit/gpn/TMP117#page=29)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Fields

`Config{}` holds the chip's reset values, so it changes nothing on a freshly reset chip. Fields with a unit carry
it in the name. Temperatures are integers in thousandths of a degree Celsius; the chip's step is 7.8125 m°C, so a
value is rounded half away from zero to the nearest step.

| Field | Default | Range | Register |
| --- | --- | --- | --- |
| `mode` | `Mode::Continuous` | `Continuous`, `Shutdown` | Configuration MOD |
| `averaging` | `Averaging::X8` | `X1`, `X8`, `X32`, `X64` | Configuration AVG |
| `cycle_time` | `CycleTime::S1` | `Ms15p5`, `Ms125`, `Ms250`, `Ms500`, `S1`, `S4`, `S8`, `S16` | Configuration CONV |
| `alert_function` | `AlertFunction::Window` | `Window` (alert mode), `Threshold` (therm mode) | Configuration T/nA |
| `alert_polarity` | `AlertPolarity::ActiveLow` | `ActiveLow`, `ActiveHigh` | Configuration POL |
| `alert_source` | `AlertSource::Flags` | `Flags`, `DataReady` | Configuration DR/Alert |
| `low_limit_mc` | -256000 | -256000 to `high_limit_mc` | TLow_Limit |
| `high_limit_mc` | 192000 | `low_limit_mc` to 255992 | THigh_Limit |
| `offset_mc` | 0 | -256000 to 255992 | Temp_Offset |
| `reset_timeout_ms` | 10 | 1 to 1000 | driver only: the longest wait for the power-up load |
| `eeprom_timeout_ms` | 50 | 1 to 1000 | driver only: the longest wait for one EEPROM word |
| `allow_general_call` | `false` | | driver only: opt in to the bus-wide reset |

The one-shot mode is not a `Mode`: a one-shot is an action (`StartOneShot()`, `ReadOneShotMilliC()`), and the chip
cannot store it in EEPROM. The datasheet's second continuous code (MOD 10) reads back as 00 and is never written.

The defaults for the two deadlines are the driver's choice, because the datasheet gives only typical values (about 1.5
ms for the power-up load and about 7 ms for an EEPROM word): see [A1 and A2](ambiguities.md#a1-and-a2-the-two-waits).
The cycle time of a setting is in Table 7-7 and in `GetCycleTimeUs(cycle_time, averaging)`: the longer of the CONV time
and the averaging time, so `CycleTime::Ms15p5` with `Averaging::X8` runs at 125 ms.

## Choosing a setting

| You want | Set |
| --- | --- |
| The lowest power at one result per second | the defaults; `Mode::Shutdown` between `ReadOneShotMilliC()` calls for less |
| The lowest noise | `Averaging::X32` or `X64`; the cycle becomes at least 0.5 s or 1 s |
| The fastest result | `Averaging::X1`, `CycleTime::Ms15p5`; one result every 15.5 ms |
| An alert that stays on until it cools | `AlertFunction::Threshold` with the low limit below the high limit |
| ALERT as a "new result" interrupt | `AlertSource::DataReady` |

The chip heats itself by a few millidegrees when it converts all the time, which is why a long cycle with shutdown
between conversions measures the surroundings better. The datasheet's section 8.2.2.2 gives the numbers.

Start from `Config{}` and change fields:

```cpp
--8<-- "tests/docs/configuration.cpp:custom"
```

## Validation

`tmp117::Validate(config)` checks a configuration without touching the chip, and `EnsureInitialized()` and
`Configure()` call it before any I/O. It returns `Error::InvalidArgument` when an enum holds a value that is not an
enumerator, a limit or the offset is outside -256000 to 255992 m°C, the low limit is above the high limit, or a
deadline is zero or above 1000 ms. `CheckedConfig` does the same at build time: a constant configuration that fails
does not compile ([compile-fail test](../tests/compile_fail/checked_config_invalid.cpp)).

## Kconfig

Under ESP-IDF, `idf.py menuconfig` shows **TMP117 temperature sensor** when the driver is a component of the
project ([Installation](installation.md)); the menu is [`_config/Kconfig`](../_config/Kconfig). The options reach
the code only through [`inc/tmp117_kconfig.hpp`](../inc/tmp117_kconfig.hpp), never through a driver signature:

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```

| Option | Default | `Config` field |
| --- | --- | --- |
| `CONFIG_TMP117_AVERAGING_*` | 8 | `averaging` |
| `CONFIG_TMP117_CYCLE_*` | 1 s | `cycle_time` |
| `CONFIG_TMP117_RESET_TIMEOUT_MS` | 10 | `reset_timeout_ms` |
| `CONFIG_TMP117_EEPROM_TIMEOUT_MS` | 50 | `eeprom_timeout_ms` |

The limits, the offset and the general-call opt-in are deliberately not menu options: a wrong alert limit is a
decision about the system, not a build setting. Without Kconfig (plain CMake, other platforms) `KconfigDefaults()`
returns `Config{}`. Both cases are host tests.

Next: [Porting](porting.md).
