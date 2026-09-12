# Testing

```bash
make -C host          # build and run the suite (under a second)
make -C host asan     # again, under address and UB sanitizers
make -C host clean
```

No ESP32, no toolchain, no network, no package manager. If a test ever needs one of those, that is a bug in the test.

## The split that makes this possible

The library is two layers, and the boundary is the point:

| Layer | Where | Depends on | Tested by |
|---|---|---|---|
| **Portable core** | `portable/` | C11 and libc | 47 host tests |
| **ESP-IDF layer** | `src/`, `include/` | ESP-IDF, FreeRTOS | Compiled in CI; validated on hardware |

Everything that is easy to get subtly wrong lives in the core: Prometheus naming rules, label escaping, float formatting, JSON escaping, buffer truncation, registry identity. None of it needs a microcontroller to be wrong, so none of it needs a microcontroller to be tested.

What is left in the ESP-IDF layer is hardware reads and task plumbing — code where a compile failure or a hardware run are the only meaningful checks, and where a mock would test the mock.

## What the host suite covers

- **Registry:** registration, idempotence (a collector called 100 times consumes one slot), labels as part of identity, counter accumulation, capacity exhaustion, and every argument-validation path.
- **Naming:** dots to underscores, unit suffixes, `_total` on counters, `_info` on info metrics, illegal characters, a leading digit, an already-suffixed name, and the reported length when the buffer is too small.
- **Value formatting:** integers without a decimal point, fractions with enough precision, and `NaN`/`+Inf`/`-Inf` spelled the way Prometheus spells them.
- **Escaping:** quotes, backslashes, newlines and other control characters, in both JSON and the Prometheus label grammar — including the control characters the exposition format has no escape for, which are dropped rather than emitted raw.
- **Serialisers:** `# HELP` and `# TYPE` lines, counter typing, the reset reason as a label on a 1, negative and fractional values, balanced-JSON verification of the OTLP payload, omission of empty resource attributes, and truncation safety on both.
- **Domain:** reset-reason names and the fault/not-fault split, heap fragmentation including its degenerate inputs.
- **Hostile input:** a device whose name or label contains quotes and backslashes cannot break either format.

## What CI adds

| Job | Why it exists |
|---|---|
| gcc and clang | Different warnings, different UB. Both are errors here (`-Werror`) |
| Address + UB sanitizers | Where buffer arithmetic bugs actually surface |
| **32-bit build** (`-m32`) | Catches `size_t` and pointer-width assumptions that an x86-64 host hides and an ESP32 finds at 3 a.m. |
| cppcheck | A second opinion on the core |
| ESP-IDF 5.1 and 5.3, `esp32` and `esp32c3` | The Xtensa and RISC-V targets do not fail in the same places, and IDF minor versions move APIs |
| Both examples built | An example that does not compile is worse than no example |
| `size-components` | Prints what telltale actually costs in flash, so the README's figures can stop being estimates |

## Warnings are errors

The host build runs with `-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith`.

`-Wconversion` and `-Wsign-conversion` are the ones that matter for this code: a `size_t` truncated to an `int`, or a signed comparison against a length, is exactly the bug class that makes a serialiser write past a buffer on one platform and not another.

## The contract every serialiser follows

Both `telltale_core_render_prometheus` and `telltale_core_render_otlp_json` behave like `snprintf`:

- they write at most `out_size` bytes **including** the terminator;
- they return the number of bytes they *would* have written;
- `returned >= out_size` means the output was truncated.

There is a test for each of these on both serialisers, plus one that passes a NULL buffer to size the output before allocating. A caller can therefore never read a half-written buffer, and the ESP-IDF layer turns a truncated render into an explicit `ESP_ERR_INVALID_SIZE` and a 500 — never a partial scrape a dashboard would happily plot.

## Adding a test

`host/test_core.c` is one file with a `main()` that lists every test. Add the function, add the `TT_RUN` line, run `make -C host`. The harness is `host/tinytest.h`; there is nothing to install.

```c
TT_TEST(my_new_behaviour) {
    telltale_registry_t registry = make_registry();
    TT_ASSERT_EQ_INT(TELLTALE_OK, telltale_set(&registry, "device.x", TELLTALE_GAUGE,
                                               TELLTALE_UNIT_NONE, NULL, 1));
    TT_ASSERT_CONTAINS(rendered, "device_x");
}
```

Assertions available: `TT_ASSERT`, `TT_ASSERT_EQ_INT`, `TT_ASSERT_NEAR`, `TT_ASSERT_STR_EQ`, `TT_ASSERT_CONTAINS`, `TT_ASSERT_NOT_CONTAINS`.

Rules the suite follows:

1. **Name the behaviour, not the function.** `set_is_idempotent_so_a_collector_cannot_leak_slots` explains why the test exists; `test_set_2` does not.
2. **Test the degenerate input.** Zero free bytes, a NULL registry, a name that is one character too long, a label count over the maximum. Embedded code meets these in the field, not in the demo.
3. **Cover the "we cannot tell" case.** A shadow without metadata, a battery with no provider, a disassociated radio. Absence must not render as zero.
4. **Assert on the exact rendered line** when testing a serialiser. `TT_ASSERT_CONTAINS(buffer, "device_heap_free_bytes{device_id=\"esp32-ab12cd\"} 41264\n")` catches a spacing or escaping regression that a substring match on the name alone would miss.

## On-target tests

The ESP-IDF layer has no automated on-target suite yet. When it gets one it will use Unity, which ESP-IDF already ships, under `test/` with `idf.py -T telltale build flash monitor` — not a second harness.

Until then, what has and has not been verified on hardware is written down in [HARDWARE-VALIDATION.md](HARDWARE-VALIDATION.md) rather than left to the reader's assumption.
