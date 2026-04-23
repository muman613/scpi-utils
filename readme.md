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

## Library

The SCPI serial library lives in `libs/scpi-device`. Its public interface is
documented in `libs/scpi-device/doc/readme.md`.
