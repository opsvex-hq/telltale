# ADR 0003 — telltale asks the application; it does not reach behind it

**Status:** accepted · **Date:** 2026-09-12

## Context

Two metrics are tempting to collect automatically, and both would require telltale to take something over.

**Disconnection counts.** The obvious implementation registers a Wi-Fi event handler and counts `WIFI_EVENT_STA_DISCONNECTED`. Easy, and invisible: the application now has two handlers, one of which it did not install and will not find when it goes looking for why its own reconnection logic behaves oddly.

**Battery voltage.** The obvious implementation configures an ADC channel and reads it. But the divider ratio, the channel, the attenuation and the calibration are board decisions. A wrong guess does not produce a missing metric; it produces a *plausible wrong number*, and a dashboard showing 3.9 V on a device that is about to shut down is worse than a dashboard showing nothing.

## Decision

telltale collects only what it can read unambiguously from the platform: heap, clock, reset cause, task stacks, and the radio's own RSSI (which has exactly one meaning).

For the rest it exposes a seam:

- `telltale_note_disconnect(handle)` — call it from your event handler.
- `config.battery_provider` — a callback returning volts, or `false` for "no reading", which leaves the metric absent.

## Consequences

**Good.** No hidden global state and no surprise handler. The application keeps one event path. A battery metric, when present, means something specific to that board. "No reading available" is representable, which matters: absence is honest, and zero is a lie that graphs well.

**Bad.** Two lines of integration the library could have hidden, and a disconnect counter that is zero on a device whose author never called the function. The second is a real failure mode — a metric that is silently always zero — and it is mitigated only by documentation: [METRICS.md](../METRICS.md) says where the number comes from, and the OTLP example shows the call in context.

We took that trade because a wrong number is worse than an absent one, and a component that installs handlers behind an application's back is a component that gets removed the first time it is suspected.

**Consistent with the wider design.** The same principle appears in the exporters: a truncated render is an explicit error rather than a shortened body, and RSSI is absent rather than 0 while disassociated. The rule throughout is that telltale would rather report nothing than report something untrue.
