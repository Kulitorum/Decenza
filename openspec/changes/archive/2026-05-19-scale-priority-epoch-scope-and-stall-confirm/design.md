## Context

`scale-ble-priority-backoff` (#1185) persists the "dual-HIGH-incapable" classification build-scoped: `SettingsHardware::setConnectionPriorityLatch()` writes `connectionPriority/{latched,triggerKind,setTimeIso,buildCode}`; `BLEManager::setSettings()` rehydrates the in-memory `ScaleSkipHighLatch` only when `cpBuildCode() == versionCode()`, otherwise it calls `clearConnectionPriorityLatch()` and re-detects ("build-scoped safety valve"). #1219 narrowed `clearConnectionPriorityLatch()` to the four latch keys and added a sibling `policyMode` key + the `scaleFeedResumed` recovery signal + observe mode.

Two problems, both pre-existing in #1185 (not introduced by #1219):

1. **Per-build re-detection has an inverted cost/benefit.** `versionCode` is bumped by CI on every release, so a genuinely incapable device re-runs detection on essentially every update. Detection is not free — it requires an actual fault during preheat/shot (disrupted preheat at best, off-target SAW shot at worst). The benefit it buys ("a new build may have fixed it / it may be mis-classified") is weak: BLE dual-HIGH contention is a property of the device radio + OS BLE stack + DE1-link timing, not the Decenza build; and a *false* latch sits at BALANCED, which is proven safe (#1097) and produces no bad coffee. So the design imposes a recurring real cost on true positives to auto-recover false positives from a not-broken state.

2. **The scale-feed-stall backstop has no confirmation.** `BlePriorityDetector::onScaleStall()` latches on the *first* >2 s gap (`kScaleStaleMs`), unlike `onDe1Fault()` which already requires ≥2 within `kDe1FaultWindowMs`. A transient blip that self-recovers can still latch — that is precisely the false-positive class whose per-build auto-recovery problem #1 was paying for.

## Goals / Non-Goals

**Goals:**
- Persist the classification across builds; make re-classification a *deliberate* per-release decision, not an accident of CI versioning.
- An already-classified user upgrading from the current release pays **zero** additional detection.
- The scale-feed-stall backstop only latches when the stall is *confirmed* (did not self-recover), eliminating the dominant false-positive shape.
- Capable hardware byte-identical (it never latches, so neither change is reachable for it).

**Non-Goals:**
- No change to the DE1-fault-cluster trigger (≥2/`kDe1FaultWindowMs`) — it already has confirmation.
- No new user-facing setting; both knobs are source constants.
- Not removing the MCP reset (the in-session escape hatch) or observe mode (#1219) — both unchanged.
- Not auto-detecting "BLE behavior changed" — the epoch is a human decision, by design.

## Decisions

### D1 — Epoch constant, decoupled from `versionCode`
Introduce `kBleDetectionEpoch` (int, starts at 1) in a deliberately-edited BLE constants header — **not** `versioncode.txt`, **not** derived from `versionCode`, **not** touched by CI. The gate in `setSettings()` changes from `storedBuild == versionCode()` to `storedEpoch == kBleDetectionEpoch`; everything else about the safety-valve mechanics (rehydrate vs `clearConnectionPriorityLatch()` + re-detect) is unchanged. Bumping the constant in a commit is the single, auditable "re-classify every device once on this release" lever. *Alternative considered:* a settings-level "BLE revision" written by CI only on BLE-touching builds — rejected: CI can't know a build changed BLE behavior, and it reintroduces an implicit coupling; a human bump is the honest signal. *Alternative:* persist forever, no auto-reset (rely solely on MCP reset) — rejected: keeps no global recovery path for a future genuine BLE fix or a bad-classification wave, and the MCP reset is per-device/operator-only.

### D2 — `buildCode` demoted, not removed
Keep writing `connectionPriority/buildCode` (the `versionCode` at set-time) but only as a **diagnostic** ("last classified by build X", surfaced in the MCP read). It no longer gates. Removing it would lose useful field-triage signal and force a wider migration; demoting is lower-risk and strictly additive in information.

### D3 — Legacy-record forward migration (the backward-compat guarantee)
On `setSettings()`, a persisted record that is `latched` and has a `buildCode` but **no `detectionEpoch` key** is a legacy (pre-epoch) record. Treat it as valid for the current epoch: rehydrate the in-memory latch *and* write `detectionEpoch = kBleDetectionEpoch` (one-time forward migration), and log it. Net: a user already classified on the current release keeps the finding with **zero** extra detection across the upgrade. *Alternative considered:* treat "no epoch" as epoch 0 ⇒ mismatch ⇒ discard+re-detect once — rejected: it still costs the exact bad-shot this change exists to remove, on the very upgrade that ships the fix. *Trade-off (accepted, documented):* a stale legacy latch from an old build is carried forward; mitigated because BALANCED is safe (no bad coffee) and both the epoch bump and the MCP reset clear it. Detection of a *fresh* (non-legacy) record with a mismatched epoch is unchanged from today's build-mismatch path (discard + re-detect).

### D4 — Stall confirmation via persistence, gated by the recovery edge
Split the stall into two states on the **existing DE1 shot-sample cadence** (`setCurrentFrame` → `checkScaleFeedStall`, the same tick that already detects the stall — no new timer):
- **Suspected**: gap > `kScaleStaleMs` (unchanged 2 s). Still emits `scaleFeedStalled(gapMs)` exactly as today — observe mode logging is unchanged, and this remains the diagnostic "feed went quiet" signal.
- **Confirmed**: the feed is *still* stalled and the gap has grown past `kScaleStallConfirmMs` (design start ~5–6 s, a single tunable constant) **and** no `scaleFeedResumed` fired since the suspected edge. Emit a new `scaleFeedStallConfirmed(gapMs)`.

`scaleFeedResumed` (from #1219) firing between suspected and confirmed **cancels** the pending confirmation (the stall self-recovered ⇒ it was the transient/false shape ⇒ never confirms ⇒ never latches). Enforce wires the *backoff* to `scaleFeedStallConfirmed` only; `onScaleStall()` is called solely on confirmation. Observe logs suspected (existing), recovered (existing #1219), and confirmed (new "would back off"). *Alternative considered:* require two separate stalls in one cycle — rejected: a genuinely-dead feed often stalls once and stays dead; "two stalls" could fail to ever confirm a real fault. Persistence-past-threshold-unless-recovered directly encodes "did not self-recover", which is exactly the true/false discriminator and reuses the recovery signal already built. *No timer:* both the confirm threshold and the recovery cancel are evaluated on the DE1 tick / the `scaleFeedResumed` edge — consistent with the existing event-based `checkScaleFeedStall` and CLAUDE.md's "no timers as guards".

### D5 — DE1-fault path and capable hardware untouched
`onDe1Fault()` keeps its ≥2/`kDe1FaultWindowMs` clustering. Capable hardware never reaches a latch (no sustained stall, no fault cluster), so neither the epoch gate nor the confirmation is reachable for it — the capable path is byte-identical, asserted by an explicit test.

### D6 — Layering on #1219
The confirmation arm consumes `scaleFeedResumed` and the detector/transport observe plumbing from #1219 (`scale-priority-backoff-observe-mode`). Implement stacked on that branch (or rebase after merge). The epoch arm touches only `settings_hardware` + `blemanager::setSettings` + the MCP read and is independent of #1219, so it can land first if the stack is split.

## Risks / Trade-offs

- **[Stale legacy latch carried forward forever]** → Accepted (D3): BALANCED is safe (#1097); recovery is the deliberate epoch bump or the existing MCP reset. The alternative (discard) costs the very bad-shot this change removes, on the fix's own upgrade.
- **[Epoch never bumped ⇒ a real future BLE fix doesn't reach already-latched devices]** → By design the epoch *is* the lever; bumping it on the fix release is the intended action and is now a single auditable constant change with a documented contract next to it. Far less error-prone than the prior implicit per-build behavior.
- **[`kScaleStallConfirmMs` too long ⇒ a genuinely-bad device takes longer to latch / one more disrupted prep]** → It only delays latching by the confirm window on a *real* sustained stall (which by definition isn't recovering); the suspected signal still fires for observability, and the value is a single tunable constant validated against the #1219 observe evidence before tightening.
- **[`kScaleStallConfirmMs` too short ⇒ confirmation doesn't actually filter transients]** → The #1219 observe-mode field data (suspected vs recovered timings) is the calibration input; the constant is chosen from real recovery-gap distributions, not guessed.
- **[Confirmation diverges from the suspected detection logic over time]** → Both are computed in the one `checkScaleFeedStall` path on the same tick; a single new threshold + the existing recovery flag, no parallel detector. A test asserts suspected always precedes confirmed and recovery cancels.

## Migration Plan

- Additive. New key `connectionPriority/detectionEpoch`; `buildCode` retained (diagnostic). First epoch-build launch: legacy latched records (no epoch key) are honored and stamped forward (D3) — no user-visible event, logged for triage. Fresh records carry the epoch from the start.
- Deploy: ship normally. To re-classify everyone on a release that changes BLE handling, bump `kBleDetectionEpoch` in that commit (documented next to the constant).
- Rollback: revert. A reverted build reading an epoch-stamped record falls back to the build-scoped comparison (epoch key simply unread) — degrades to "re-detect once", safe. The confirmation arm reverts to immediate-latch (prior behavior), also safe.

## Open Questions

- Exact `kScaleStallConfirmMs` value: start ~5–6 s; **finalized from #1219 observe-mode field data** (the recovered-gap distribution) before this ships its confirmation arm — recorded in tasks as a calibration gate, not guessed here.
