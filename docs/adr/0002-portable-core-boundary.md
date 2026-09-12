# ADR 0002 — A portable core, so the risky code is testable on a laptop

**Status:** accepted · **Date:** 2026-09-12

## Context

Embedded libraries are usually tested in one of three ways: not at all, on hardware in a lab, or through a mock layer that replaces the vendor SDK. The first is how most of them ship. The second is slow and needs a board per contributor. The third tends to test the mock.

But look at where the bugs in a metrics exporter actually are. Not in `esp_get_free_heap_size()` — that either compiles and works or does not exist. They are in: the Prometheus naming rules, label escaping, float formatting, JSON escaping, buffer truncation, and the identity of a metric with labels. Every one of those is pure computation that has nothing to do with a microcontroller.

## Decision

Split the library at exactly that line.

`portable/` contains the registry, the naming rules, the escaping, both serialisers and the domain helpers. It includes no ESP-IDF header, allocates nothing, and keeps no global state. It is compiled by the host suite with plain gcc and clang, and by the ESP-IDF component as ordinary sources.

`src/` contains the hardware reads, the RTOS task, the HTTP handler and the OTLP client. It has no string handling to speak of, because all formatting lives on the other side of the line.

## Consequences

**Good.** The 47-test suite runs in under a second with no board, no toolchain and no network, and covers the code where mistakes are both likely and invisible. Sanitizers and a 32-bit build come for free and catch the pointer-width and conversion bugs an x86-64 host would otherwise hide. The core is reusable on any platform, which turns a porting request into a collector-writing exercise rather than a rewrite.

**Bad.** Two include directories and a slightly unusual layout for an ESP-IDF component. The ESP-IDF layer itself has no automated test — a deliberate trade: what remains there is hardware access, where a mock would prove nothing and only a real board or a compile can. That gap is recorded in [HARDWARE-VALIDATION.md](../HARDWARE-VALIDATION.md) rather than papered over with coverage theatre.

**The line moves only one way.** Anything that can be computed without hardware belongs in the core. If a future collector needs a calculation, the calculation goes in `portable/` with a test, and the collector calls it.
