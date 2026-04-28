# scpi-device-test

`scpi-device-test` is the automated test executable for the `scpi-device`
library. It exercises the serial transport layer without requiring a physical
SCPI instrument by creating a pseudo-terminal pair and using one side as a fake
device.

The test verifies that `ScpiDevice` can open a serial port, send newline
terminated SCPI commands, read newline terminated responses, handle both
blocking and non-blocking I/O modes, wait briefly after opening a port before
the first write, and retry the first query after an initial timeout.

## Requirements

The test uses POSIX pseudo-terminal APIs:

- `openpty(3)`
- `read(2)` / `write(2)`
- `termios`

On Linux, it links with `libutil`. It is not intended to run on platforms that
do not provide these Unix serial and pseudo-terminal interfaces.

## Build

Build the project with CMake from the repository root:

```sh
cmake -S . -B build
cmake --build build
```

CTest is enabled by the root `CMakeLists.txt`, so the executable is built when
`BUILD_TESTING` is enabled. That is CMake's default unless explicitly disabled.

To force test builds on:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
```

## Run

Run the test directly:

```sh
./build/libs/scpi-device/scpi-device-test
```

Or run it through CTest:

```sh
ctest --test-dir build --output-on-failure -R scpi-device-test
```

The test prints nothing on success and exits with status `0`. On failure it
prints:

```text
test failure: <reason>
```

and exits with status `1`.

## Test Coverage

The executable currently runs these checks:

- `testIdentityQueryRoundTrip`: sends `*IDN?` in non-blocking mode and reads a
  simulated instrument identity response.
- `testBlockingIdentityQueryRoundTrip`: repeats the identity query path with
  blocking serial I/O enabled.
- `testOpenSettlesBeforeFirstWrite`: confirms that the first command is delayed
  after opening the serial port, giving USB serial devices time to settle.
- `testFirstQueryTimeoutIsRetriedAfterOpen`: simulates a timeout on the first
  `*IDN?` query and verifies that the first-query retry path succeeds.

The simulated identity response is:

```text
OWON,XDM1041,24152470,V4.3.0,3
```

## Environment Overrides

The tested library behavior can be influenced by these environment variables:

- `SCPI_DEVICE_OPEN_SETTLE_MS`: overrides the post-open settle delay in
  milliseconds.
- `SCPI_DEVICE_OPEN_RETRIES`: overrides how many times the first query after
  open is retried.

Changing these values can affect the expected timing and retry behavior in this
test.
