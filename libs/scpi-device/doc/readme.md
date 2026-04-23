# scpi-device

`scpi-device` is a small C++17 library for communicating with SCPI instruments
over USB serial ports. It is intended for common DVM-style instruments that
accept line-oriented SCPI commands and return newline-terminated responses.

## Public header

Include:

```cpp
#include "scpi_device/scpi_device.h"
```

Namespace:

```cpp
namespace scpi
```

## SerialOptions

```cpp
struct SerialOptions {
    int baudRate = 9600;
    std::chrono::milliseconds readTimeout{1000};
    std::chrono::milliseconds writeTimeout{1000};
    std::string lineEnding = "\n";
};
```

The serial link is configured as 8 data bits, no parity, 1 stop bit, no hardware
flow control, raw mode.

## ScpiDevice

`ScpiDevice` owns one serial file descriptor and provides command/query helpers:

```cpp
scpi::ScpiDevice dvm("/dev/serial/by-id/usb-example");
dvm.open();

auto idn = dvm.identity();             // *IDN?
dvm.configureDvm(scpi::DvmFunction::DcVoltage);
auto value = dvm.read();               // READ?
dvm.setDvmDisplay(scpi::DvmDisplay::Main, scpi::DvmFunction::DcVoltage);
auto displayed = dvm.measureMainDisplay(); // MEAS1?
```

When using a registered device, the constructor accepts the registry record and
opens the serial port immediately with the stored options:

```cpp
scpi::DeviceRegistry registry;
auto registeredDevice = registry.getDevice("bench-dvm");
if (registeredDevice) {
    scpi::ScpiDevice dvm(*registeredDevice);
    auto idn = dvm.identity();
}
```

### Core methods

- `open()` opens the configured serial port.
- `open(port, options)` updates the port/options and opens the port.
- `close()` closes the serial port.
- `isOpen()` reports whether the descriptor is open.
- `writeCommand(command)` sends `command` plus `SerialOptions::lineEnding`.
- `query(command)` sends a command and reads one newline-terminated response.
- `identity()` sends `*IDN?`.
- `reset()` sends `*RST`.
- `clearStatus()` sends `*CLS`.

On a fresh `open()`, the library waits briefly before the first command and will
automatically reopen-and-retry the first query a small number of times if the
USB serial link times out or drops. The defaults can be overridden with
`SCPI_DEVICE_OPEN_SETTLE_MS` and `SCPI_DEVICE_OPEN_RETRIES`.

### DVM helpers

- `configureDvm(function)` sends `CONF:<function>`.
- `configureDvm(function, range)` sends `CONF:<function> <range>`, where
  `range` may be a manual range value or `AUTO`, `MIN`, `MAX`, or `DEF`.
- `configureDvm(function, range, resolution)` sends
  `CONF:<function> <range>,<resolution>`.
- `setDvmDisplay(display, function)` sends `FUNC "<function>"` for the main
  display or `FUNC2 "<function>"` for the secondary display.
- `disableSecondaryDvmDisplay()` sends `FUNC2 "NONE"`.
- `read()` sends `READ?`.
- `measure(function)` sends `MEAS:<function>?`.
- `measureDisplays()` sends `MEAS?`.
- `measureMainDisplay()` sends `MEAS1?`.
- `measureSecondaryDisplay()` sends `MEAS2?`.
- `initiate()` sends `INIT`.
- `fetch()` sends `FETC?`.

Supported `DvmFunction` values include DC/AC voltage, DC/AC current,
2-wire/4-wire resistance, frequency, period, capacitance, continuity, and
diode.

For the OWON XDM1000/XDM1041/XDM2041 family, display function names are mapped
to the manual's `FUNCtion[1|2]` values, such as `VOLT`, `VOLT AC`, `RES`,
`CAP`, `CONT`, and `DIOD`.

## Device discovery

```cpp
auto ports = scpi::ScpiDevice::listSerialByIdDevices();
auto identities = scpi::ScpiDevice::identifySerialByIdDevices();
```

`listSerialByIdDevices()` returns all entries found in `/dev/serial/by-id` as
sorted strings. `identifySerialByIdDevices()` opens each entry, tries `*IDN?`
at 115200 baud first and then 9600 baud, validates the identity response, and
returns a vector of `IdentityResult`:

```cpp
struct IdentityResult {
    std::string port;
    int baudRate;
    bool success;
    std::string identity;
    std::string error;
};
```

When an instrument cannot be opened, does not respond before the read timeout,
or returns an invalid identity, `success` is false and `error` contains the
exception message.

`isValidScpiIdentity(identity)` requires exactly 4 or 5 non-empty
comma-separated fields. Responses that do not match this format are treated as
failed discovery attempts.

`parseScpiIdentity(identity)` returns the parsed identity fields. For a response
such as:

```text
OWON,XDM1041,24152470,V4.3.0,3
```

the first field is `manufacturer`, the second is `model`, the third is
`softwareVersion`, and the fourth is `firmwareVersion`.

## Device registry

`DeviceRegistry` stores the translation from a user-facing device name to the
serial port and serial configuration. The backing file is JSON.

Default path:

```text
$XDG_CONFIG_HOME/scpi-utils/devices.json
```

or, if `XDG_CONFIG_HOME` is not set:

```text
$HOME/.config/scpi-utils/devices.json
```

Example:

```cpp
scpi::DeviceRegistry registry;

registry.setDevice(
    "bench-dvm",
    "/dev/serial/by-id/usb-example",
    scpi::SerialOptions{});

auto device = registry.getDevice("bench-dvm");
if (device) {
    scpi::ScpiDevice dvm(device->port, device->options);
    dvm.open();
}
```

The CLI exposes those serial options when registering a device:

```sh
scpi-util add bench-dvm /dev/serial/by-id/usb-example \
  --baud 115200 \
  --read-timeout 1500 \
  --write-timeout 1500 \
  --line-ending lf
```

### Registry methods

- `devices()` returns all registered devices sorted by name.
- `getDevice(name)` returns an optional `RegisteredDevice`.
- `getPort(name)` returns only the serial port or throws when missing.
- `getOptions(name)` returns only the serial options or throws when missing.
- `setDevice(name, port, options)` adds or replaces a device and saves JSON.
- `removeDevice(name)` removes a device, saves JSON, and reports whether it
  existed.
- `load()` reloads the JSON backing file.
- `save()` writes the current registry to disk.
- `defaultStoragePath()` returns the default JSON path.

The JSON schema is:

```json
{
    "version": 1,
    "devices": {
        "bench-dvm": {
            "port": "/dev/serial/by-id/usb-example",
            "serial": {
                "baudRate": 9600,
                "readTimeoutMs": 1000,
                "writeTimeoutMs": 1000,
                "lineEnding": "\n"
            }
        }
    }
}
```
