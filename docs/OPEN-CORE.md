# What is open, what is not, and why

Opsvex sells operations work. This library is free software, permanently. Both of those are true, and the line between them is worth stating plainly so nobody has to guess which side a contribution lands on — or discover later that the useful half was behind a wall.

## The rule

**The piece that creates friction is open. The piece that creates value is not.**

That is not a phrase we invented; it is what the two businesses in this exact space already do:

| Company | Open | Commercial |
|---|---|---|
| **Memfault** | `memfault-firmware-sdk` — the library that runs on the device | The fleet observability backend, sold as SaaS |
| **Golioth** | `golioth-firmware-sdk`, Apache-2.0 | The fleet management platform, publicly priced per device per month |
| **Gruntwork** | Terragrunt, Terratest | Managed modules and support |
| **Fairwinds** | Polaris, Goldilocks, Pluto | Fairwinds Insights |

None of them gives away the valuable half. All of them give away the friction, because friction is what stops adoption, and adoption is what makes the valuable half worth buying.

## Applied here

**telltale is the friction.** Instrumenting a device is nobody's competitive advantage; it is a chore every firmware team pays for separately and none of them enjoys. A library that is closed, or crippled, or license-keyed would simply not be used — and a component you cannot read is a component you should not run on hardware you cannot easily reach.

So the whole library is Apache-2.0, with no feature gates, no telemetry back to us, no "community edition" asterisk. That includes:

- every collector and both exporters,
- the semantic-convention proposal,
- the Grafana dashboard,
- the test suite and the portable core, which is the most reusable part of all.

**The fleet layer is the value.** What is hard is not making one device report; it is what happens when there are four thousand of them:

- provisioning and identity at scale, with certificate rotation in the field;
- OTA orchestration by cohort, with automatic rollback on a rise in `device_reset_fault`;
- retention and aggregation of fleet-wide history, at a cost that does not scale linearly with devices;
- alerting that knows the difference between one device rebooting and a firmware version rebooting;
- the operational judgement about what to do when a cohort starts browning out at 04:00.

That is what Opsvex sells, as a service or as a platform we run. It is a different product with a different trust model, not a flag on this one.

## What that means for you

**If you are a firmware team:** take it. Apache-2.0, patent grant included, no strings. You do not owe us anything, including attribution beyond the license file. If it saves you a week, that is the point.

**If you are a competitor:** also take it. The library is not the moat, and pretending it is would only make it worse software.

**If you are a contributor:** nothing you contribute here will be moved behind a paywall later. The license is Apache-2.0 and relicensing would require every contributor's agreement, which is a promise with teeth rather than a statement of intent. There is no CLA: we are not collecting the rights that a relicense would need.

**If you want the fleet layer:** [talk to us](https://opsvex.com). If you would rather build it yourself on top of telltale, that is a legitimate outcome and the library is designed for it — `telltale_registry_of()` exists precisely so you can drive the pipeline from your own code.

## What would make us close something

Nothing in this repository. If we build the fleet console, it starts as a separate, closed product; it does not begin as telltale features that later disappear.

If that ever changes, it will be announced here before it happens, not discovered in a release note.

## The commercial anchor, stated once

The README links to opsvex.com and this document explains what we sell. That is the extent of it: no tracking pixels in the docs, no UTM parameters in the badge links, no "sponsored by" banner in the console output, and nothing phoning home from a device. A library that advertises at runtime is a library that gets vendored and stripped.
