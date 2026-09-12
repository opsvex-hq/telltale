# Why propose IoT device semantic conventions

**Status: draft. Not submitted upstream yet.** This document is the argument that would accompany a proposal to [open-telemetry/semantic-conventions](https://github.com/open-telemetry/semantic-conventions), and the record of what was checked before writing one.

## The gap

OpenTelemetry has semantic conventions for HTTP, database calls, messaging systems, RPC, FaaS, container runtimes, Kubernetes, cloud providers, CI/CD, GenAI and more. It has **no conventions for a constrained device** — nothing for heap, reset cause, task stacks, link quality or battery on microcontroller-class hardware.

What that means in practice: every firmware team that decides to export metrics invents its own names. One calls it `free_heap`, another `heap_free_bytes`, a third `mem.available`. A fleet with two firmware vendors cannot be graphed on one dashboard without a translation layer nobody wants to own.

The convention does not need to be brilliant. It needs to exist and be one obvious choice.

## What was checked before claiming a gap

Claiming "nobody has done this" is the easiest way to look careless, so the checks are written down:

| Check | Result |
|---|---|
| The `model/` directory of `open-telemetry/semantic-conventions`, read group by group | No IoT, embedded, firmware or device-health group |
| A code search across that repository for `heap`, `reset.reason`, `rssi`, `battery` | No metric definitions |
| Open issues and pull requests mentioning IoT or embedded conventions | The one tangential request — adding MQTT to `messaging.system` — carried **zero reactions and zero comments** |
| `esp32 prometheus` and `opentelemetry embedded` as repository searches | Single-author projects, the largest at 5 stars and last touched in 2022 |
| Espressif's own `esp-insights` | 147 stars against 18,951 for `esp-idf`, and it is a vendor SaaS agent rather than an open convention |

Two conclusions follow. The gap is real. And **demand is unproven** — the silence cuts both ways, and a proposal that claims a groundswell would be lying. The honest framing is: this is a small, well-specified convention with a working implementation behind it, offered because it costs the project little to accept and saves every firmware team the same argument.

## The design decisions worth defending

These are the choices a reviewer would question, and the answers.

**Why `device.*` and not a new top-level namespace?** `device.*` already exists in the resource conventions for mobile (`device.id`, `device.model.identifier`, `device.manufacturer`). Health metrics for a microcontroller belong under the same root; a new `embedded.*` or `mcu.*` namespace would fragment a namespace that already means "the physical thing running this code".

**Why is the reset reason an attribute on a constant-1 metric?** Because no metrics backend accepts a string as a sample value. Reporting a category as an info-style metric is the established idiom in both Prometheus (`*_info` with labels) and OTel, and the alternative — an integer enum — pushes the decoding onto every dashboard that reads it.

**Why both `heap.free` and `heap.largest_free_block`?** Because they diverge exactly when a device is in trouble. A device with 100 kB free and a 4 kB largest block is about to fail an allocation, and no dashboard built on free heap alone will show it. The derived `heap.fragmentation` exists so an alert can be written without arithmetic in the query.

**Why `min_free` at all?** Sampling at 30-second intervals misses the trough. The RTOS already tracks the minimum for free; exporting it turns a sampled metric into a guaranteed one for the value that matters.

**Why the *minimum* free stack per task, not the current one?** The current free stack of a sleeping task tells you nothing. The high-water mark is the only number that predicts an overflow, and every RTOS with stack checking already computes it.

**Why volts for the battery, not percent?** The conversion from voltage to remaining charge depends on cell chemistry, temperature and load. A device that reports a percentage has already thrown that context away, and two vendors' percentages are not comparable. Volts are.

**Why is RSSI absent rather than zero when disconnected?** 0 dBm reads as an exceptionally strong signal. A gap in the series is the truthful representation of "not associated", and `device.link.connected` carries that fact explicitly.

**Why `{boot}` and `{disconnect}` as units?** UCUM annotations for counted things, which is what the OTel specification asks for. `1` would be wrong; it means dimensionless ratio.

## How this would be proposed

The project's own process, followed rather than shortcut:

1. Open an **issue** describing the gap, with the checks above as evidence and this YAML linked as a strawman. Not a pull request: a convention proposal that arrives as a diff invites review of the diff instead of the idea.
2. Ask for a slot in the **Semantic Conventions SIG** call. Conventions are agreed by people, not merged by CI.
3. If there is interest, submit the YAML as a `development` stability group. Nothing here should claim stability it has not earned.
4. If there is no interest, **say so here and stop.** A convention nobody wants is not worth carrying, and a repository that pretends its proposal is pending when it has been ignored for a year is worse than one that records the outcome.

## Relationship to this library

telltale emits exactly these names, which is the only honest way to propose a convention: a proposal with no implementation is a wish, and an implementation with no proposal is one more dialect.

If the conventions change during review, **telltale changes with them** — that is what pre-1.0 is for. The library's own names are not the point; a shared vocabulary is.

## What this document will record later

Whatever actually happens. If the proposal is rejected, the reason goes here. If it is ignored, that goes here too, with the date. The point of writing the evidence down before proposing is to be able to tell the difference between "we were wrong about the gap" and "we were right and nobody cared" — and the second outcome is still worth knowing.
