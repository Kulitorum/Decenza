## Context

`src/ble/refractometers/difluidr2.cpp` implements the DF-DF framing, checksum, and a subset of the R2's command set. It handles Func 0 (device info, partially), Func 1 (temperature unit only, as of [#1656](https://github.com/Kulitorum/Decenza/pull/1656)), and Func 3 (device action). Result parsing is already complete for packs 0–4, including the averaged result in pack 3 and the averaged temperature/counter in pack 4 — code written against the spec but never exercised, because the driver only ever sends a single test.

Two independent references exist for the parts we do not implement: DiFluid's `protocolR2.md`, and Beanconqueror's `difluidR2Refractometer.ts` + `diFluid/protocol.ts`, which are a working implementation of the same device with a full enum table. Where they agree, the behaviour is settled and no reverse engineering is needed.

Constraints that shape this:

- **`RefractometerDevice` is an abstraction over two devices.** `DiFluidR1` shares the interface and has no averaging. Anything added to the base class must have a sane refusal for R1.
- **"Never use timers as guards" (CLAUDE.md).** The existing 15-second `m_measurementTimer` is a deliberate, documented exception — a device that goes silent emits no event, so no event-based mechanism can detect it. That justification covers a liveness watchdog; it does not cover using the same timer as a ceiling on run duration, which is what it currently is.
- **The project's bias is against new user-facing settings.** Smarter defaults first.
- **Field logs are consumed by AI assistants over MCP.** Log text is an interface, not decoration.

## Goals / Non-Goals

**Goals:**

- Averaged readings available to users, with the measurement lifecycle correct for runs longer than a single test.
- Status and error logging that identifies what the device is actually doing.
- Serial number captured, as input to the unresolved Brix-vs-TDS variant question.
- No change to what a default installation does when the user presses the existing TDS control.

**Non-Goals:**

- Calibration. The R2 supports it (Device Action Cmd 2); driving a calibration from the app is a separate, riskier feature with its own UX and failure modes.
- Loop test. Statuses 7–9 are named for log readability, but the driver does not initiate loop tests.
- Screen brightness, and the remaining device settings. Configuring someone's refractometer from Decenza is not a coffee problem.
- Resolving Brix-vs-TDS. This change gathers the serial number; deciding what to do with a Brix variant stays with [#1386](https://github.com/Kulitorum/Decenza/pull/1386).

## Decisions

### The test count is a fixed default, not a setting

Averaging exists to reduce scatter. Three tests captures most of that benefit; beyond that the user is waiting appreciably longer for diminishing returns. Rather than add a settings row and make every user decide, the averaged read uses a fixed count.

*Alternative — a 1–10 setting (Beanconqueror exposes one):* rejected as the first move. It puts a number in front of the user that they have no basis to choose, in a project that explicitly prefers better defaults over more knobs. The driver still clamps to 1–10, so the setting can be added later without protocol work if anyone actually asks for it.

*Alternative — always average, no single-test path:* rejected. It silently makes every existing TDS read several times slower, which is a behaviour change users did not ask for.

### The watchdog becomes a liveness timer, restarted by device traffic

Today `m_measurementTimer` is armed once at request time with a 15-second interval. For a single test that is indistinguishable from a liveness watchdog. For a 3-test average it is a hard ceiling that aborts a healthy device mid-run.

The fix is to restart the interval on any packet indicating progress — pack 0 statuses 4, 5 and 10, and per-test result packets. Status 10 matters specifically: the R2 emits it *because* an individual test is running long, which is exactly when a fixed deadline would fire. Silence still recovers, which is the property the exception to the no-timers rule was granted for.

*Alternative — a longer fixed interval scaled by test count:* rejected. It restores the same bug at a larger number, and picks a duration from guesswork rather than from what the device is telling us.

### Averaging is a parameter on the existing request, not a second device mode

`requestMeasurement()` gains an averaged variant rather than the driver holding a mode flag. A mode flag would be state that can disagree with what the user last pressed, and would have to be reconciled across disconnects. A parameter cannot drift.

`RefractometerDevice` gains the averaged entry point with a base implementation that falls back to a single measurement, so `DiFluidR1` needs no change and any future device gets correct-if-unoptimised behaviour for free.

### Status and error names live in one table, not scattered across the switch

A single translation function per table, rather than string literals at each branch. The tables are the part most likely to be extended when DiFluid publishes more codes, and keeping them together is what makes the "unknown code still logged" requirement natural instead of an afterthought at every call site.

### Serial number assembly indexes by part, and does not assume arrival order

Beanconqueror splices by `part * 5` into a fixed-width buffer, which tolerates out-of-order arrival. Same approach here, plus explicit tracking of which parts have arrived so a partial serial is never logged as the device's identity. BLE notification ordering is not something to assume.

### Auto Test is offered but not enabled by default

Auto Test makes the R2 measure on its own when the prism temperature shifts. That is genuinely nicer — the reading is there when you load the sample. It also means readings arrive unprompted, which interacts with the active-page gating in `refractometer-tds-capture`: a reading during a temperature drift with the review page open would populate the field without the user asking.

The existing gating already handles this correctly (device-initiated readings are an explicit scenario in that spec), so the capability is safe to add. Leaving it off by default keeps the change from altering behaviour for anyone who does not opt in.

## Risks / Trade-offs

**No R2 hardware is available to the author** → Every change is byte-level against two independent references, and covered by packet-level tests built from the spec's own worked examples. The averaged path in particular has never run against a device — it must be flagged as needing a hardware check before merge, not asserted as verified.

**Averaged runs make the driver's stateful window much longer** → A disconnect or an app-side navigation mid-run is now a realistic case rather than a 2-second window. The measuring state must clear on disconnect, not only on result or timeout.

**Restarting the watchdog on device traffic could mask a device that is emitting status but never finishing** → Bounded in practice: a device stuck emitting status 5 forever is a firmware fault the app cannot fix, and the user can navigate away. Accepting this is preferable to aborting healthy long runs, which is a certainty rather than a hypothetical.

**Naming more error codes tempts surfacing more of them** → The existing division exists because the R2 emits benign class/code pairs around successful reads; the spec pins that division explicitly so a later reader does not "fix" the asymmetry and reintroduce error-dialog spam.

**The serial number is device-identifying data** → It goes to the debug log, which users share when reporting problems. It identifies a refractometer, not a person, and the model and firmware strings are already logged the same way. No new class of data is being exposed.

## Open Questions

- Is a fixed count of 3 the right default, or should the averaged control offer a small explicit choice (e.g. 3 or 5) at the point of use rather than in settings? Point-of-use is not a settings row and may be the better compromise.
- Does the review page need a distinct visual treatment for an averaged reading versus a single one — is "this number is an average of 3" worth showing after the fact, or only during?
- Should Auto Test be surfaced in the UI at all in this change, or landed as driver capability plus MCP access only, with UI deferred until someone asks for it?
