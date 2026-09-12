# ADR 0001 — The portable core is C11, not C++17

**Status:** accepted, with a noted deviation · **Date:** 2026-09-12

## Context

The house stack policy for firmware says: public interfaces in C with `extern "C"`, drivers and RTOS glue in C, **domain logic in C++17** with `-fno-exceptions`, `-fno-rtti`, no dynamic allocation in hot paths and RAII for resources.

telltale follows the first two clauses. It does not follow the third, and rather than quietly diverge, the reason is written down.

## Decision

The portable core is plain C11.

## Why

**There are no resources to manage.** RAII pays for itself where something must be released — a file, a lock, a peripheral handle. The core owns a fixed-size struct the caller supplies and writes into a buffer the caller supplies. There is nothing to acquire and nothing to release, so the main argument for C++ does not apply.

**The host harness must be trivial to run.** The suite compiles with `cc file.c -o test`. Adding C++ adds a second toolchain requirement for contributors, a second set of warning flags, and name mangling to think about the day someone wants to link the core into a C project that is not ours.

**Portability is the point of this layer.** The core is meant to be copied into a Zephyr, pico-sdk or bare-metal project (see [PORTING.md](../PORTING.md)). C11 goes everywhere; C++17 on a toolchain someone else picked is a negotiation.

**The policy's target is different.** "Domain logic in C++17" was written for components with real domain modelling — state machines, protocol decoders, device abstractions with variants. A metric registry and two text serialisers are neither.

## Consequences

**Good.** One compiler, one language, a header any project can include, and a test suite that runs anywhere with no build system at all.

**Bad.** A genuine divergence from a decided house policy, and the pattern this repository sets as the first public component may be copied by the next one. Mitigated by this document: the deviation is scoped to "leaf components with no resource ownership and no domain model", and the next component with either should follow the policy.

**Revisit if** the core grows a device abstraction with variants, or acquires anything with a lifetime. At that point the policy's reasoning starts applying and this decision should be reversed rather than defended.
