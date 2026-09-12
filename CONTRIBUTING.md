# Contributing

## The most valuable contribution

**A report from real hardware.** No board has run this code yet ([the full list of what that leaves unverified](docs/HARDWARE-VALIDATION.md)). If you flash it, you have done something the maintainers have not, and a report either way — including "it compiled and did nothing useful" — is worth more than any feature.

Please include the board, the ESP-IDF version, the target, and what you expected versus what you saw.

## Also welcome

- A collector for a signal that is unambiguous on the platform (flash wear, PSRAM, CPU load).
- A port of the portable core to Zephyr, pico-sdk or bare metal — see [PORTING.md](docs/PORTING.md).
- Corrections to the [semantic-convention proposal](semconv/RATIONALE.md), especially from anyone who has taken a proposal through the OpenTelemetry SIG before.
- Dashboard panels that answer a question the current ones do not.
- Measured flash and RAM numbers to replace the README's estimates.

## Will be declined

- **A collector that has to guess.** Battery divider ratios, ADC channels, board-specific calibration. telltale asks the application for those — see [ADR 0003](docs/adr/0003-no-hidden-event-handlers.md).
- **Anything that installs an event handler behind the application's back.** Same ADR.
- **Runtime allocation after init.** The registry and the render buffer are sized once, on purpose.
- **A dependency.** The core has none beyond libc, and the ESP-IDF layer uses only components already in IDF.
- **Reporting zero for "unknown".** An absent metric is honest; a zero RSSI is a lie that graphs well.

## The rules the code follows

1. **Anything computable without hardware goes in `portable/`, with a test.** That is the whole reason the suite runs on a laptop — see [ADR 0002](docs/adr/0002-portable-core-boundary.md).
2. **Serialisers follow the snprintf contract:** at most `out_size` bytes including the terminator, and return what *would* have been written.
3. **Units are seconds, bytes, volts and 0..1 ratios.** Convert at the collector, never at the dashboard.
4. **`-Werror`, with `-Wconversion` and `-Wsign-conversion` on.** A truncated `size_t` is the bug class this code is most exposed to.
5. **The public API is C with `extern "C"` and an opaque handle.** It stays consumable from C, C++ and Arduino, and the internals stay replaceable.
6. **No `printf` in the library.** `ESP_LOGx` on the device side, nothing at all in the portable core.

## Setup

```bash
make -C host          # build and run the suite
make -C host asan     # again, under sanitizers
idf.py -C examples/basic build
```

Adding a test is three lines: write the function in `host/test_core.c`, add the `TT_RUN` call in `main()`, run `make`. There is nothing to install — the harness is a single header.

## Commits and pull requests

- One concern per PR.
- Say what you tested, and on what.
- CI must be green: host tests on gcc and clang, sanitizers, a 32-bit build, cppcheck, and ESP-IDF builds for `esp32` and `esp32c3` on two IDF releases.
- A new metric name needs a matching entry in `semconv/iot-device.yaml` and `docs/METRICS.md`. A name that exists in code but not in the convention is how a convention dies.

## Maintenance expectations

**2–4 hours a month**, issues answered within **5 business days**. If a PR sits, it is bandwidth rather than disinterest — a nudge is fine.

## Licensing

Contributions are accepted under Apache-2.0. There is deliberately **no CLA**: without one, relicensing would require every contributor's agreement, which is the strongest available guarantee that this library will not be closed later. See [OPEN-CORE.md](docs/OPEN-CORE.md).
