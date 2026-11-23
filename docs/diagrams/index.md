# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## One register read on the I2C bus

<!-- figure: bus-frame -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="bus-frame.dark.svg">
  <img src="bus-frame.svg"
    alt="Sequence of one I2C register read: the host sends START, the write address and the pointer 00h of
    Temp_Result, repeats START with the read address, the TMP117 returns two bytes and the host ends
    with NACK and STOP.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/bus-frame.seq.yml`](../model/bus-frame.seq.yml)

Drawn from:

- [`docs/model/bus-frame.seq.yml`](../model/bus-frame.seq.yml)

## I2C timing of the address byte

<!-- figure: bus-timing -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="bus-timing.dark.svg">
  <img src="bus-timing.svg"
    alt="I2C timing of the TMP117: the start condition, the 7-bit address and the write bit, each held while
    SCL is high, and the acknowledge, with the start, setup and hold times of the datasheet.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/bus-timing.timing.yml`](../model/bus-timing.timing.yml)

Drawn from:

- [`docs/model/bus-timing.timing.yml`](../model/bus-timing.timing.yml)

## Up to four TMP117 on one I2C bus

<!-- figure: bus-topology -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="bus-topology.dark.svg">
  <img src="bus-topology.svg"
    alt="An I2C bus on which an MCU and up to four TMP117 devices share SDA and SCL, each at the address its
    ADD0 pin selects, with one pull-up resistor per line.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/bus-topology.topology.yml`](../model/bus-topology.topology.yml)

Drawn from:

- [`docs/model/bus-topology.topology.yml`](../model/bus-topology.topology.yml)

## Inside the TMP117

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the TMP117: its function blocks, how they connect and which package pins reach each
    block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/tmp117.hpp`](../../inc/tmp117.hpp)
- [`inc/tmp117_i2c_interface.hpp`](../../inc/tmp117_i2c_interface.hpp)

## What the TMP117 does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-capabilities.dark.svg">
  <img src="chip-capabilities.svg"
    alt="Capability map of the TMP117: Temperature (supported): A 16-bit result, 7.8125 m°C per count, never
    the 8000h value before a conversion; Averaging and cycle time (supported): 1, 8, 32 or 64
    conversions per result and eight cycle times, checked against Table 7-7; Continuous, shutdown, one-
    shot (supported): Converts continuously, sleeps at 0.15 µA, or takes one result on request; Alert
    window and therm (supported): ALERT outside a window (alert mode) or above a limit with hysteresis
    (therm mode), flags latched; Offset trim (supported): The chip adds a signed offset to every result,
    up to ±256 °C in 7.8125 m°C steps; Power-on defaults (guarded): Stores the configuration, limits and
    offset so the chip starts alone; every step checked and relocked; Identity and traceability
    (supported): Checks Device_ID before anything else and reads the two NIST traceability words; Soft
    and general-call reset (guarded): Soft reset by default; the bus-wide general-call reset only when
    you opt in; SMBus alert response (not covered): The bus-wide alert address read; the driver returns
    Unsupported and says why.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/tmp117.hpp`](../../inc/tmp117.hpp)
- [`inc/tmp117_i2c_interface.hpp`](../../inc/tmp117_i2c_interface.hpp)

## TMP117: Typical connections

<!-- figure: chip-circuit-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-typical.dark.svg">
  <img src="chip-circuit-typical.svg"
    alt="Schematic of the TMP117 typical connections: the chip with U2 MCU, C1 capacitor 0.1 µF, R1 resistor
    5 kΩ, R2 resistor 5 kΩ; the MCU wires carry the port hooks ReadAlertPin(bool&), Write() /
    WriteRead().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/tmp117.hpp`](../../inc/tmp117.hpp)
- [`inc/tmp117_i2c_interface.hpp`](../../inc/tmp117_i2c_interface.hpp)

## TMP117: I2C register word

<!-- figure: chip-frame-i2c -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-frame-i2c.dark.svg">
  <img src="chip-frame-i2c.svg"
    alt="I2C register word of the TMP117, 24 bits, most significant bit first. Write: 0000 (bits 23 to 20),
    pointer (bits 19 to 16), data high (bits 15 to 8), data low (bits 7 to 0); Read: 0000 (bits 23 to
    20), pointer (bits 19 to 16), reply high (bits 15 to 8), reply low (bits 7 to 0).">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/tmp117.hpp`](../../inc/tmp117.hpp)
- [`inc/tmp117_i2c_interface.hpp`](../../inc/tmp117_i2c_interface.hpp)

## TMP117

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-hero.dark.svg">
  <img src="chip-hero.svg"
    alt="Overview of the TMP117, a digital temperature sensor with a 16-bit result in steps of 1/128 °C,
    accurate to 0.1 °C from -20 °C to 50 °C, a programmable alert window and a small EEPROM, read over
    I2C or SMBus. A MCU talks to it over I2C; it drives ALERT line; it reads back Die temperature.
    Headline figures: ±0.1 °C maximum error, -20 to 50 °C; 1/128 °C per count, 16-bit result; 1.8 to 5.5
    V supply, 1.7 V up to 70 °C; 3.5 µA typical at one result per second; 400 kHz fastest SCL; 0.15 µA
    typical in shutdown.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/tmp117.hpp`](../../inc/tmp117.hpp)
- [`inc/tmp117_i2c_interface.hpp`](../../inc/tmp117_i2c_interface.hpp)

## TMP117 package, WSON-6

<!-- figure: chip-outline -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-outline.dark.svg">
  <img src="chip-outline.svg"
    alt="Outline of the TMP117 in WSON-6 drawn to scale from above: body 2.0 x 2.0 x 0.8 mm, pitch 0.65 mm, 6
    pins and an exposed pad.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/tmp117.hpp`](../../inc/tmp117.hpp)
- [`inc/tmp117_i2c_interface.hpp`](../../inc/tmp117_i2c_interface.hpp)

## TMP117 pinout, WSON-6

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the TMP117 in WSON-6, top view: 6 pins and an exposed pad, each with its name, its
    electrical type as a colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/tmp117.hpp`](../../inc/tmp117.hpp)
- [`inc/tmp117_i2c_interface.hpp`](../../inc/tmp117_i2c_interface.hpp)

## Wiring: Typical connections

<!-- figure: chip-wiring-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-typical.dark.svg">
  <img src="chip-wiring-typical.svg"
    alt="Wiring of the TMP117 for the Typical connections: MCU I2C SCL to SCL (Write() / WriteRead()); MCU
    I2C SDA to SDA (Write() / WriteRead()); MCU GPIO (IRQ) to ALERT (ReadAlertPin(bool&)). Supplies,
    grounds and the support parts are listed under the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/tmp117.hpp`](../../inc/tmp117.hpp)
- [`inc/tmp117_i2c_interface.hpp`](../../inc/tmp117_i2c_interface.hpp)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the TMP117 core, which sends register reads and writes through
    the I2C port contract to the port class you write, which drives the chip; the core and the contract
    are the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/tmp117.hpp`](../../inc/tmp117.hpp)
- `I2cInterface` in [`inc/tmp117_i2c_interface.hpp`](../../inc/tmp117_i2c_interface.hpp)

## One call through the layers

<!-- figure: read-call -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="read-call.dark.svg">
  <img src="read-call.svg"
    alt="Sequence of one temperature read: your code calls the TMP117 core, which reads Temp_Result through
    your port, refuses the 8000h value, converts counts and returns a Result.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/read-call.seq.yml`](../model/read-call.seq.yml)

Drawn from:

- [`docs/model/read-call.seq.yml`](../model/read-call.seq.yml)
