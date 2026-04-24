# scpi-utils

`scpi-utils` provides command-line tools and a small C++ library for working
with USB serial SCPI instruments. The project currently supports discovering
SCPI devices, registering stable serial-port aliases, querying instrument
identity, controlling DVM-style measurements such as configuring and reading
voltage, resistance, capacitance, continuity, and diode modes, and exposing a
new D-Bus service for registry and live device control.

This project is also the basic framework for supporting an engineering bench
full of SCPI equipment. The current DVM support establishes the device registry,
serial transport, command/query helpers, and CLI shape that can be extended to
other common bench instruments such as oscilloscopes and programmable power
supplies.

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Install

Install the library, headers, CMake package files, and `pkg-config` metadata:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

The install provides:

- `lib/libscpi-device.a`
- `include/scpi_device/scpi_device.h`
- `lib/cmake/scpi-device/scpi-deviceConfig.cmake`
- `lib/pkgconfig/scpi-device.pc`

## D-Bus Service

The repository now includes a `scpi-service` executable that exposes two
interfaces on D-Bus:

- `org.scpi.Registry`
- `org.scpi.DeviceControl`

The introspection XML for the service contract lives at:

```text
dbus/scpi-service.xml
```

For development, the service defaults to the session bus:

```sh
scpi-service --session
```

To use the system bus instead:

```sh
scpi-service --system
```

The service contract is the XML file above. The build uses
`sdbus-c++-xml2cpp` to generate adaptor and proxy headers from that XML, and
`scpi-service` implements the generated adaptor interfaces.

## CLI

The CLI executable is `scpi-util`. It now acts as a D-Bus client for
`scpi-service` rather than opening serial devices directly, so the service must
be running on the selected bus before most commands will succeed.

```sh
scpi-util [--session|--system] add <device-name> <serial-port-device> [options]
scpi-util [--session|--system] rm <device-name>
scpi-util [--session|--system] info <device-name>
scpi-util [--session|--system] devices
scpi-util [--session|--system] list
scpi-util [--session|--system] scan
scpi-util [--session|--system] -v scan
scpi-util [--session|--system] dvm <device-name> configure <function> [--range <value|AUTO|MIN|MAX|DEF>]
scpi-util [--session|--system] dvm <device-name> display <main|secondary> <function|none>
scpi-util [--session|--system] dvm <device-name> capture <function> [--range <value|AUTO|MIN|MAX|DEF>]
scpi-util [--session|--system] dvm <device-name> read [main|secondary|both]
```

Bus selection defaults to the session bus. Use `--system` if `scpi-service`
was started on the system bus.

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

## Serial Smoke Test

`scpi-smoke` opens a serial SCPI device directly and repeatedly sends `*IDN?`
at a fixed rate. It is intended for bench investigation of intermittent serial
timeouts and is not part of the D-Bus service path. The default baud rate is
115200.

Example:

```sh
scpi-smoke --port /dev/serial/by-id/usb-example \
  --baud 115200 \
  --read-timeout 1000 \
  --write-timeout 1000 \
  --settle-ms 500 \
  --interval-ms 1000 \
  --count 1000
```

Use `--duration-ms <milliseconds>` instead of, or together with, `--count` for
time-bounded runs. `--reopen-on-error` closes and reopens the port after a
failed query, which can help compare persistent-open behavior with recovery by
reopen. If a query runs past the requested interval, the next query is delayed
by one full interval instead of being sent immediately to catch up. The smoke
test disables first-query-after-open retries by default so transient reopen
failures remain visible; use `--open-retries <retries>` to enable them. Serial
queries use a blocking `read(2)` loop by default, reading until newline or the
termios read timeout. Use `--nonblocking-io` to compare against the older
poll/nonblocking serial path.

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
