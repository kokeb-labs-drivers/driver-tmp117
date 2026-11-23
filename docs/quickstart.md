# Quickstart

Run the driver on your PC first, with no hardware; then read the temperature from your MCU: wire the chip, write
a small port class, call two driver functions.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From Texas Instruments SNOSD82D D:

    - Pins and packages: 5 Pin Configuration and Functions, [p. 4](https://www.ti.com/lit/gpn/TMP117#page=4)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Try it on your PC

[`examples/host/`](../examples/host/README.md) runs the real driver against a simulated TMP117, through a host
port. From the repository root, with CMake and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/tmp117_host_example
```

```text
TMP117 driver 0.1.0 on the host
tmp117: initialised
23.351 C
23.351 C
23.351 C
one-shot 23351 mC
```

The calls in [`main.cpp`](../examples/host/main.cpp) are the ones below; only the port differs on an MCU. CI builds
and runs this example on every pull request.

## 1. Wire it

| MCU | TMP117 | Notes |
| --- | --- | --- |
| I2C SCL | SCL (pin 1) | Pull-up to V+ only if the MCU pin is open drain |
| I2C SDA | SDA (pin 6) | 5 kΩ pull-up to V+ |
| GPIO (interrupt) | ALERT (pin 3) | 5 kΩ pull-up to V+; optional |
| GND | ADD0 (pin 4) | Address `0x48` (`kI2cAddress`) |
| 3.3 V | V+ (pin 5) | 1.8 V to 5.5 V; 0.1 µF to GND close to the pin |
| GND | GND (pin 2) | |

ALERT is optional: leave it open for this example. The [Hardware](hardware.md) page has the full pin list and the
timing.

## 2. Add the driver

Add the repository as a submodule and link the `tmp117::tmp117` CMake target. [Installation](installation.md)
covers CMake on any MCU, the installed package and ESP-IDF.

```bash
git submodule add https://github.com/kokeb-labs-drivers/driver-tmp117.git external/driver-tmp117
```

## 3. Write the port

The port is the only platform code: it brings the bus up and moves bytes. Replace the `platform_*` calls with your
SDK's.

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

[Porting](porting.md) explains each method, the optional hooks and the transfer rules.

## 4. Read the temperature

```cpp title="main.cpp"
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

What each step does:

1. `EnsureInitialized()` calls your port's `EnsureInitialized()`, waits for the chip's power-up load (about 1.5 ms,
   checked by reading EEPROM_Busy), checks that Device_ID reads `0x117` in bits 11:0, writes the configuration
   registers (`Config{}` by default: the datasheet reset values, one result per second, 8 conversions averaged) and
   reads them back.
2. `ReadTemperatureMilliC()` reads the latest result, refuses the `8000h` no-conversion value and returns
   thousandths of a degree Celsius.
3. One count is 7.8125 m°C, so the value moves in steps of about 8 m°C.

Right after the start the first result is not there yet: the call returns `NoMeasurement` until one cycle has
passed (about one second with the defaults). Wait and call again: the example returns the error to its caller.
This example is built by the host CI, so it compiles against the current headers.

## If it does not work

| Symptom | First thing to check |
| --- | --- |
| `EnsureInitialized()` returns `Bus` | Your port's `EnsureInitialized()` and the wiring: the chip did not acknowledge |
| `EnsureInitialized()` returns `DeviceIdMismatch` | The address pin and the bus wiring ([Troubleshooting](troubleshooting.md#ensureinitialized-returns-deviceidmismatch)) |
| `EnsureInitialized()` returns `Timeout` | V+ and the pull-ups: the chip never finished its power-up load |
| `ReadTemperatureMilliC()` returns `NoMeasurement` | Wait one cycle: no conversion has finished since the start |
| `ReadTemperatureMilliC()` returns `UnexpectedReset` | The chip restarted under the driver: check the supply, then start again |

## Next

- [API guide](api-guide.md): one-shot measurements, alerts, errors, the EEPROM.
- [Configuration](configuration.md): every `Config` field and its default, Kconfig.
- [Examples](examples.md): the host example and the MCU examples.
- [Platforms](platforms.md): which MCUs have a port and an example.
