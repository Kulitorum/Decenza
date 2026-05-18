## Context

Merged `scale-ble-priority-backoff` (#1185) ships: a `BlePriorityDetector` on `QtScaleBleTransport` (DE1-fault primary + scale-feed-stall backstop), an in-memory app-run skip-HIGH latch on the `BLEManager` singleton, and a self-reconnect-at-BALANCED. Field log (#1176 reporter, build 3388, weak Tab A8) confirms it works:

- Scale connected at session start, streamed ~7 Hz fine through ~15 min idle (DE1 link near-silent → no contention).
- Shot prep began `[896.6 s]` (`SCALE TIMER: Reset (espresso cycle started, waiting for extraction)`, `phase=EspressoPreheating`). The scale feed died during this preheat prep — the DE1 BLE traffic surge (profile verify, ShotSettings, MMR, then telemetry) starves the scale pipe on the weak shared radio.
- `[903.4 s] EXTRACTION STARTED`; stall detected `[903.418]` (already >2 s stale — the gate is `m_active` only, so we didn't look during preheat); BACKOFF + DISCONNECT `[903.45]`; reconnect at BALANCED `[909.77]` (~6.3 s); infuse then exited correctly at 2.3 g (vs the original bug's 9.6 g skip).
- Preheating was ~6.8 s (`896.6 → 903.4`); reconnect ~6.3 s. So the disruptive window was *in the shot* only because we didn't evaluate liveness during the ~7 s preheat that preceded it.

Two residual costs: the ~6 s mid-shot "no scale — estimating" window on the triggering shot, and (because the latch is in-memory) re-paying it on the first shot of every app run for a known-weak device.

## Goals / Non-Goals

**Goals (this change):**
- Detect the stall during the pre-shot EspressoPreheating phase so the backoff+reconnect can begin during warm-up — the **guaranteed net**, independent of the probe.
- Add a startup connection-priority probe that opportunistically provokes the contention at idle (post scale-connect) so the whole detect→backoff→reconnect completes *before any shot* — the "early win". It MUST be strictly additive: a no-op when it doesn't provoke, never harmful, never machine-misconfiguring, capable-HW-safe; correctness never depends on it.
- Provide an MCP read + reset of the in-memory app-run latch (field diagnostic + in-session escape hatch + probe-outcome visibility, in lieu of a UI).
- Stay scale-agnostic; capable hardware unaffected.

**Non-Goals:**
- Persisting the latch across app restarts (deferred — see D2; revisit only if field data shows the per-run window is still painful).
- A Settings/QML UI to view or reset the latch (explicitly deferred; revisit later).
- Per-driver idle-cadence modelling or full-idle (non-shot) liveness detection.
- Changing the DE1-fault primary detector or the reconnect mechanism.
- Making correctness depend on the probe — preheat detection (D1) is the net whether or not the probe ever provokes.

## Decisions

### D1: Liveness gate extends to EspressoPreheating, not full idle
Add a "shot cycle active" input to `WeightProcessor` set when the espresso cycle starts (machine enters EspressoPreheating) and cleared at extraction end. The stall check in `setCurrentFrame()` evaluates when `(m_active || m_preheatActive) && m_tareComplete`. Detection still rides the existing DE1 sample cadence; only the gate widens.

*Why:* The feed dies during preheat prep, not at the pour — the log proves it. Preheat is the earliest phase where (a) the scale is connected + tared and continuously streaming (so a gap is a real fault, not idle quiet) and (b) the DE1 traffic surge that causes the contention is happening. *Alternative — full continuous idle detection:* rejected; not all 14 scale drivers stream continuously at idle, so it needs per-driver expected-cadence knowledge, violating the scale-agnostic design. Preheat gating gets ~all the benefit with none of that risk.

### D2: Persistence — deferred, NOT in this change
Keep the existing in-memory app-run `BLEManager` latch from #1185 unchanged. Do **not** persist across app restarts in this scope.

*Why:* The prior change rejected persistence (D5) for sound reasons — sticky misclassification and removing the "non-sticky, self-corrects next run" safety valve. Once preheat detection (D1) lands, the residual per-run cost on a weak device is a brief, often-invisible warm-up blip, not a ruined shot — so persistence would optimize away a small cost while reintroducing a worse failure mode (a mis-latched *capable* device degraded permanently with no end-user recovery, since there is deliberately no UI). The prior change's own open question said revisit persistence "only if the per-run window proves painful in practice"; we don't have that evidence yet. Park it; reassess after D1 is field-validated. *Alternative — persist now (reverse D5):* rejected for this iteration. *If later pursued,* prefer auto-clear on app-version change (every beta re-probes) over an indefinitely-sticky latch with MCP-only recovery.

### D3: MCP read + reset of the in-memory latch
Expose, via the MCP server, (a) a read of the current scale connection-priority state — is the device latched to BALANCED this run, the trigger kind (`de1-fault-cluster` / `scale-feed-stall`), and how long after app start it latched — and (b) a reset that clears the in-memory `BLEManager` latch so the next scale (re)connect re-enters detection at HIGH, without an app restart. Follow MCP data conventions (human-readable strings, units/scale in field names, ISO-8601 timestamps with offset, no raw Unix time).

*Why:* This is exactly the field-diagnostic surface we just wished we had while triaging the reporter's logs (we had to pull debug.zip and grep). Read is pure diagnostics. Reset is an in-session escape hatch for a false latch (or to re-test) without forcing the user to restart the app. Scoped to the in-memory latch — no persisted store to manage (consistent with D2). *Alternative — no MCP surface, rely on app restart:* rejected; restart is a blunt instrument and gives zero diagnostic visibility.

### D4: Latch state is reportable without persistence
The in-memory latch already carries (or trivially can) the trigger kind and a monotonic set-time; the MCP read derives "minutes since app start when latched" and the kind. No persisted metadata is added (that would be D2's concern).

*Why:* Keeps the MCP read actionable for field triage with zero new persisted state. Cheap.

### D5: Startup connection-priority probe — IN SCOPE, engineered to a strict-safety bar
Once a scale is **confirmed correctly streaming weight** (DE1 also connected, machine idle), run a **bounded** burst of read-only DE1 BLE traffic for a few seconds to provoke the dual-HIGH contention while the scale-feed-liveness detector is armed. D1 generalises: the "weight should be flowing" gate becomes `probe-active ∨ preheat ∨ extraction`, so a scale-feed stall during the probe drives the *existing* backoff → reconnect-at-BALANCED — during idle, before any shot. If the probe provokes nothing, it ends silently with zero effect.

**Trigger gate — only after the scale is confirmed streaming (not on bare connect):** the probe MUST NOT fire merely because the scale controller reached "connected". It fires only after the scale has delivered a short, healthy run of weight notifications at its expected cadence — i.e. the *known-good streaming baseline is established first*. Rationale: (a) a stall is only meaningful against a proven-live feed — probing a feed that never started would conflate "never streamed" with "stalled under contention"; (b) it guarantees the contention we provoke is genuinely the dual-HIGH starvation of a working pipe, exactly the shot-time failure mode; (c) it naturally sequences after BLE notification subscriptions settle. Gate inputs: scale-connected ∧ DE1-connected ∧ machine-idle ∧ *scale-streaming-confirmed* ∧ not-yet-probed-this-connect ∧ latch-not-already-set. The probe yields immediately (ends, no trip) if an espresso cycle starts mid-probe.

**Stressor — read-only, concrete (invariant D5.2 by construction):** maximise DE1→tablet bytes without commanding the machine. Two cooperating reads, enqueued normally (invariant D5.3):
- a burst of **MMR block reads** (`ReadFromMMR`, char `a005`) across the known-safe readable address range, and
- a tight **GATT read-poll loop** over the safe readable characteristics only: Versions `a001`, Temperatures `a00a`, StateInfo `a00e`, WaterLevels `a011`.

The probe SHALL NOT write or even read-poll any state-affecting / DANGER characteristic: RequestedState `a002`, WriteToMMR `a006`, FWMapRequest `a009`, ShotSettings `a00b`, HeaderWrite `a00f`, FrameWrite `a010`, Calibration `a012`. Reads cannot mutate machine state, so there is no corrupt-state failure path by construction; no profile re-upload or verification is required because nothing was written. (If a future device proves immune to a pure read burst, a write-based escalation is gated by invariant D5.2 and is explicitly out of this change's default path — see Open Questions.)

**Safety invariants (acceptance criteria — the probe ships only if all hold):**
1. **No-op on non-provocation.** If no stall occurs, the probe changes nothing observable; correctness is identical to D1-only.
2. **Never misconfigures the DE1.** Stressor is **read-only by construction** — MMR block reads (`a005`) over the known-safe address range plus a GATT read-poll of safe readable characteristics only (`a001`/`a00a`/`a00e`/`a011`); it never touches a write/DANGER characteristic (`a002`/`a006`/`a009`/`a00b`/`a00f`/`a010`/`a012`). Reads cannot alter machine state, so there is no corrupt-state failure path and no profile re-upload/verification is needed. A write-based stressor is explicitly NOT in this change's default path; if a future device proves immune to reads, any escalation must re-assert *only the already-active profile's identical frames* with profile-uploaded/SAW/UI side effects suppressed and a guaranteed verified profile upload before any shot — and is gated by this same invariant.
3. **Respects the BLE command queue / 50 ms write spacing** (CLAUDE.md rule) — enqueue normally, do not bypass.
4. **Idle/once/streaming gated.** Fires at most once per scale connect, only while machine-idle (never during preheat/extraction), only **after the scale is confirmed correctly streaming weight**, bounded duration; yields immediately if a shot starts; the app-run latch prevents re-probing after a backoff.
5. **Capable-HW-safe.** A brief idle read burst is sustained fine by capable radios → no stall → no trip; negligible extra startup traffic; the "capable never backs off" invariant is preserved.
6. **Falls through to D1.** If the probe fails to reproduce the bidirectional shot-time contention on a given weak device (a real possibility — a read burst ≠ writes+telemetry), preheat detection still catches it. The probe never weakens the net.

*Why now, not staged:* The user's explicit goal is to avoid a slow bad-coffee feedback loop with the reporter. Because the probe is strictly additive and strictly safe (invariants above), shipping it alongside D1 carries no regression risk: worst case it does nothing and D1 handles it exactly as without the probe; best case the reporter gets a clean pre-shot outcome immediately with no extra round trip. *Alternative — read-only-only vs. allow write re-assert:* start read-only (maximally safe); only escalate to the suppressed profile re-assert if device logs show reads don't provoke — that escalation is itself gated by invariant 2.

### D6: Probe is observable
The probe start/end and any resulting backoff are logged at the existing WARN markers (`Scale connection-priority BACKOFF triggered …`) plus a distinct probe start/end line, so a field debug.log shows definitively whether the probe ran, whether it provoked, and the outcome — closing the diagnostic loop without another user round trip.

*Why:* This whole change exists because we could only diagnose via after-the-fact debug.zip grepping. The probe must be self-evidencing in the log (and via the D3 MCP read) so we can confirm it works from one reporter log.

### D7: Review hardening — folded in, not deferred (one-and-done)
A multi-agent review of the PR surfaced correctness/observability gaps. Per the "one-and-done unless more bug reports" directive, all genuine items land in this same change/PR rather than as follow-ups. Decisions:

- **Probe must also start on the *become-idle* transition, not only on the streaming-confirm edge.** If streaming is confirmed while the machine is busy (scale connected during warm-up/heating — a common case), the single `onScaleWeight()` confirm-edge call to `maybeStartProbe()` is gated out by the idle check and never retried, so the probe silently never runs that whole connect. `onMachinePhaseChanged()` also calls the (fully-guarded, idempotent) `maybeStartProbe()` when the machine becomes idle. *This is the headline functional fix — without it the "early win" does not happen for an entire common scenario.*
- **`ScalePriorityProbe` gets a destructor** calling the idempotent `endProbe()`, so `m_probeActive` cannot strand `true` on the worker on any teardown path. (Shutdown-only worker-queue drop is acknowledged benign — process is exiting.)
- **`setProbeActive(false)` only clears `m_scaleFeedStale` when no other "weight expected" context is active**, consistent with `setShotCycleActive()`, removing a timing-dependent swallow of a concurrent extraction/preheat stall.
- **Swallowed-stall observability:** when a stall is observed but the detector is disarmed (`onScaleStall()` returns false — already latched / not at HIGH), log it. The single field debug.log must never silently drop a stall signal.
- **H2 re-scoped by evidence:** `DE1Device::errorOccurred` has **no UI/QML consumer anywhere** in the codebase (verified) — it is re-emitted from the transport and dropped (log-only). The reviewer's "probe spews user-facing error toasts" premise is therefore false for the DE1 path. The correct, minimal resolution is log *attribution*, not error-channel suppression: the probe counts DE1 link faults observed during its window and reports the count in its end line (strengthening D6's self-evidencing goal). No suppression machinery is added (it would risk masking real faults for a non-existent user surface).
- **MCP reset response honesty:** the reset queues `clearScaleSkipHighPriority()` and cannot verify it ran, so the response reports it as *accepted/queued, applies on next (re)connect* rather than asserting a completed state change.
- **Type-design hardening (invariant expression):** the `BLEManager` skip-HIGH latch becomes a small `ScaleSkipHighLatch` value type with set/clear mutators enforcing "kind+time set iff latched" and a mandatory (non-defaulted) trigger kind — removing the silent-`"unknown"` footgun. `ScalePriorityProbe`'s implicit bool lifecycle becomes an explicit `enum class State { Idle, Confirming, Probing, Done }`, collapsing the representable-but-illegal bool tuples to four named states. Both are contained, single-file refactors with no public-behaviour change; the gate predicate in `WeightProcessor` is left as-is (at, but not over, its complexity ceiling — flagged as the watch-point for the *next* added context).
- **Test coverage restored/extended:** a regression test pins the original in-shot `m_active` extraction-stall path (the refactor changed its gate with no test); a negative test pins the preheat-without-tare-complete D5 invariant; a probe-active-then-extraction interaction test; and a pure value-type test (`tst_scaleskiphighlatch`) covers the `ScaleSkipHighLatch` set/clear/empty-kind-salvage invariant. The probe *controller* state machine is NOT given a dedicated unit-test target — see "accepted as-is".

*Why:* Each item is either a real correctness gap (probe-never-starts, stranded `m_probeActive`, swallowed stall), an evidence-corrected scope reduction (H2), honesty in an operator-facing response (reset), or invariant-expression hardening the review explicitly recommended. None is speculative; bundling them now is cheaper and safer than a second round-trip and matches the one-and-done directive. *Explicitly NOT done, accepted as-is with rationale (not silently deferred):* (1) aboutToQuit teardown reorder — shutdown-only, benign (process exiting). (2) the unlogged disconnected-transport probe no-op — the probe end-line already records the outcome reason. (3) a dedicated `ScalePriorityProbe` controller unit-test target — the new `enum class State` makes the illegal bool tuples *unrepresentable by construction* (removing the review's core concern), the stall→backoff decision logic it drives is unit-tested (`tst_weightprocessor` gate tests + `tst_scaleblepriority`), and a target linking `blemanager.cpp`/`de1device.cpp`/`machinestate.cpp`/`weightprocessor.cpp` (≈half the app, incl. every scale driver via `scalefactory`) purely to exercise null-pointer state transitions is materially higher risk than value and contradicts the minimal-change principle; the controller's integration is covered by on-device validation (task 4.2).

## Risks / Trade-offs

- **Probe stresses a weak radio** → could it cause a problem it wouldn't otherwise have? Read-only stressor (invariant D5.2) cannot misconfigure the machine; provoking the stall *at idle* is the intended outcome and is strictly better than provoking it mid-shot. Net: the probe surfaces a fault that would otherwise have hit a real shot — earlier and harmlessly.
- **Probe fails to provoke on a given device** (read burst ≠ shot-time writes+telemetry) → not a regression: preheat detection (D1) remains the guaranteed net, identical to probe-absent behavior. The probe is best-effort acceleration only (invariant D5.6).
- **Probe false-positive on capable HW** (briefly trips a capable device into BALANCED for the app run) → capable radios sustain a short idle read burst (invariant D5.5); if an exceptional transient still trips it, the cost is one app run at BALANCED (the existing non-sticky behavior, unchanged — persistence is parked) plus an MCP reset available. Acceptable and self-correcting on restart.
- **Preheat shorter than reconnect (~6 s)** → on a hot group / very short preheat the reconnect may not finish before the pour, so a brief early "estimating" blip can remain *if the probe didn't already catch it at idle*. The probe is specifically what closes this gap for most weak devices; D1 is the fallback. Acceptable; not persisted (parked).
- **Preheat-gate false-positive** (flagging a stall before the scale is actually expected to stream) → gate also requires `m_tareComplete`; auto-tare completes early in the flow-before/preheat phase, so "tared + preheating/probe + connected" reliably means the scale should be streaming.
- **MCP reset races a live connection** → reset clears the in-memory latch; takes effect on the next scale (re)connect's detection pass, not mid-connection. Document as eventually-consistent.

## Migration Plan

- No persistence, no settings key, no data migration — purely in-memory + behavioral. Builds on merged #1185's in-memory latch.
- Rollback: revert restores #1185 behavior (preheat gate reverts to extraction-only; probe + MCP surface removed). No schema/profile/settings impact.
- Sequencing: single change/PR implementing D1, D3, D5, D6 together. Cross-reference #1093/#1176; relates to merged #1185. Verify on the reporter's weak Tab A8 (probe provokes + reconnects at idle before any shot; if not, preheat catch) and a capable device (probe runs, no trip, MCP read shows not-latched). The probe + WARN logging (D6) is designed so a single reporter debug.log confirms the outcome — no back-and-forth.

## Open Questions

- Probe stressor: **decided — read-only** (MMR block-read burst + GATT read-poll of `a001`/`a00a`/`a00e`/`a011`; never write/DANGER chars). Open only: the exact MMR address set + burst size/duration + read-poll interval — tune against the reporter's Tab A8 device log. Write-based escalation stays out of scope unless reads demonstrably fail to provoke on that device (then a separate change, gated by D5.2).
- Probe trigger timing: **decided — gate on scale-streaming-confirmed** (a healthy run of weight notifications) ∧ DE1-connected ∧ machine-idle, not bare connect. Open only: how many notifications / what cadence-stable interval counts as "confirmed streaming" (smallest reliable baseline that also lets BLE subscriptions settle) — tune at implementation. The probe must yield immediately if an espresso cycle starts mid-probe (already an invariant).
- MCP surface shape: a dedicated `ble`/scale-priority tool vs. extending an existing devices tool — pick whichever fits the existing MCP taxonomy with least added surface.
- Probe kill-switch: whether to add an internal (non-UI) way to disable the probe if a field issue emerges — likely yes (cheap insurance), decide during specs/tasks.
