# TMP117 ESP32 examples

ESP-IDF applications that run the TMP117 driver on an ESP32-C6, built by CI on every pull request.

## Wiring (ESP32-C6 DevKitC-1)

| ESP32-C6 | TMP117 | Notes |
| --- | --- | --- |
| GPIO6 | SDA | I2C0, 100 kHz; internal pull-ups enabled, add 4.7 kΩ for long wires |
| GPIO7 | SCL | |
| GND | ADD0 | Address `0x48` |
| GPIO11 | ALERT | Optional, open drain with a 5 kΩ pull-up; set `alert = -1` in `Esp32Tmp117Pins` when not wired |
| 3V3 | V+ | 100 nF to GND at the pin |
| GND | GND | |

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh tmp117_basic_example Debug
./scripts/flash_app.sh flash_monitor tmp117_basic_example Debug
```

`idf.py menuconfig` shows "TMP117 temperature sensor" under Component config: the averaging, the cycle
time and the two driver deadlines that `tmp117::KconfigDefaults()` returns.

## Expected output

```text
I (312) tmp117_example: TMP117 driver 0.1.0
tmp117: initialised
I (318) tmp117_example: initialised, silicon revision 1
I (1420) tmp117_example: 23.456 C
```

An `EnsureInitialized failed: DeviceIdMismatch` line means Device_ID did not read 0x0117 in bits 11:0: check the
address pin, SDA and SCL first. `NoMeasurement` just after start-up is normal: the first result needs one cycle.

## Files

| File | What it is |
| --- | --- |
| [`main/tmp117_basic_example.cpp`](main/tmp117_basic_example.cpp) | The example app |
| [`main/esp32_tmp117_port.hpp`](main/esp32_tmp117_port.hpp) | The ESP-IDF port: bus, ALERT pin, delay, log |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root, which is the component |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |
