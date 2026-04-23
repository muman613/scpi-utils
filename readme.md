# scpi-utils

Utilities for USB serial SCPI instruments.

## Build

```sh
cmake -S . -B build
cmake --build build
```

## CLI

The CLI executable is `scpi-util`.

```sh
scpi-util add <device-name> <serial-port-device> [options]
scpi-util rm <device-name>
scpi-util info <device-name>
scpi-util devices
scpi-util list
scpi-util scan
scpi-util -v scan
scpi-util dvm <device-name> configure <function> [--range <value|AUTO|MIN|MAX|DEF>]
scpi-util dvm <device-name> display <main|secondary> <function|none>
scpi-util dvm <device-name> capture <function> [--range <value|AUTO|MIN|MAX|DEF>]
scpi-util dvm <device-name> read [main|secondary|both]
```

Add options:

```sh
--baud <rate>
--read-timeout <milliseconds>
--write-timeout <milliseconds>
--line-ending <lf|crlf|cr|literal>
```

Example:

```sh
scpi-util add bench-dvm /dev/serial/by-id/usb-example \
  --baud 115200 \
  --read-timeout 1500 \
  --write-timeout 1500 \
  --line-ending lf
```

`scan` suppresses per-device errors by default and only prints devices that
respond with a valid 4-field or 5-field comma-separated identity. Use `-v` or
`--verbose` to show failed scan attempts.

`devices` lists configured registry entries, including serial port settings.
`list` opens each configured device, queries `*IDN?`, and prints parsed identity
fields:

```text
manufacturer: OWON
model: XDM1041
software-version: 24152470
firmware-version: V4.3.0
```

Device registrations are stored as JSON in:

```text
$XDG_CONFIG_HOME/scpi-utils/devices.json
```

or, if `XDG_CONFIG_HOME` is not set:

```text
$HOME/.config/scpi-utils/devices.json
```

Prefer stable `/dev/serial/by-id/...` symlinks when registering devices.

## DVM Capture

The `dvm` commands follow the OWON XDM1000/XDM1041/XDM2041 SCPI manual:

- `configure` sends `CONF:<function>` and selects the same function on the
  main display with `FUNC "<function>"`.
- `display` sends `FUNC "<function>"` or `FUNC2 "<function>"` to set the main
  or secondary display.
- `capture` configures the function, sets it on the main display, then prints
  `MEAS1?`.
- `read` prints the existing displayed measurement using `MEAS1?`, `MEAS2?`,
  or `MEAS?`.

Supported function names include:

```text
vdc, vac, resistance, capacitance, continuity, diode
```

Additional aliases are available for the manual's other modes: `idc`, `iac`,
`fres`, `frequency`, and `period`.

Examples:

```sh
scpi-util dvm bench-dvm capture vdc --range AUTO
scpi-util dvm bench-dvm configure resistance --range 5E3
scpi-util dvm bench-dvm display main capacitance
scpi-util dvm bench-dvm read main
scpi-util dvm bench-dvm display secondary none
```

Manual range values for the requested DVM modes:

```text
vdc:          500E-3, 5, 50, 500, 1000, AUTO, MIN, MAX, DEF
vac:          500E-3, 5, 50, 500, 750, AUTO, MIN, MAX, DEF
resistance:   500, 5E3, 50E3, 500E3, 5E6, 50E6, 500E6, AUTO, MIN, MAX, DEF
capacitance:  50E-9, 500E-9, 5E-6, 50E-6, 500E-6, 5E-3, 50E-3, AUTO, MIN, MAX, DEF
continuity:   no range argument
diode:        no range argument
```

## Library

The SCPI serial library lives in `libs/scpi-device`. Its public interface is
documented in `libs/scpi-device/doc/readme.md`.
