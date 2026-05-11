## Context

The DiFluid R2 BLE refractometer communicates via standard BLE characteristic notifications. Once Decenza subscribes to the result characteristic on connect, **the R2 pushes a result packet whenever it completes a measurement** — regardless of who initiated it. Measurements can be initiated by:

1. Decenza calling `DiFluidR2::requestMeasurement()` (app-initiated, via on-screen button)
2. The user pressing the physical button on the R2 device (device-initiated)
3. R2 firmware idle/wake behavior (device-initiated)

Today, `MainController::setRefractometer()` connects `DiFluidR2::tdsChanged` directly to `Settings::dye::setDyeDrinkTds()` with no filtering. The next shot to end then captures that value into the shot record via `MainController` shot-end metadata assembly. Path-2 and path-3 readings produce ghost TDS values; analysis of 50 v3-era shots found 11 such corruptions (TDS 0.02–0.64%, EY proportionally bad) traceable to between-shot R2 emissions.

PR #814 fixed an unrelated leak (PostShotReviewPage was writing edited values back to Settings, causing them to appear on the next shot). The device-initiated path is a different bug with the same symptom and survived #814.

The desired behavior is: R2 readings should populate a shot's TDS **only** when the user is actively reviewing that shot (PostShotReviewPage is the top of the navigation stack), and only when the reading is in a plausible espresso TDS range.

## Goals / Non-Goals

**Goals:**
- Eliminate the path from device-initiated R2 readings into shot records when no review page is up.
- Reject obvious calibration / empty-cuvette / rinse readings via a value threshold even when the review page is up.
- Preserve the user workflow: pull shot → press R2 button with the cup on the sensor → see the value appear in the review page.
- Keep the existing on-screen "Take Reading" button working (app-initiated path) unchanged from the user's perspective.

**Non-Goals:**
- No changes to the BLE protocol, R2 driver state machine, or the way the R2 emits results.
- No new persistent setting for the validation threshold (hardcoded constant for now; revisit if users report rejected legitimate readings).
- No retroactive cleanup of corrupted historical shots — that's a separate one-time data operation.
- No changes to how scales report weight or how the Decent scale auto-populates dose/yield. Different code path; not the bug we're fixing.
- No changes to the shot metadata schema or any database column.

## Decisions

### Decision: Move TDS-capture ownership from MainController into PostShotReviewPage

**Choice:** Delete the unconditional `tdsChanged → setDyeDrinkTds` connection in `MainController::setRefractometer()`. Have `PostShotReviewPage.qml` subscribe to `MainController.refractometer.tdsChanged` when it becomes visible (`Component.onCompleted` / `onVisibleChanged`) and unsubscribe when it loses visibility. The slot validates the value and writes directly to the page's local `editDrinkTds` (and recomputes `editDrinkEy` from current dose/yield).

**Alternatives considered:**
- **Add a "review page active" flag to a controller and gate the existing MainController connection on it.** Simpler diff but keeps the global `Settings.dyeDrinkTds` write-through, which is itself a hazard (it was the root cause of PR #814's bug too). Concentrating R2 readings in the one place they're consumed is cleaner long-term.
- **Use a `BoolSetting` like `Settings.refractometer.autoPopulateEnabled`.** Adds a user-visible knob the user has to manage. Not justified without evidence the auto behavior is unwanted in some scenarios.

**Rationale:** The page is the single owner of the editable TDS/EY fields. Having the BLE signal land directly on the page mirrors how a user thinks about the action ("I'm reviewing this shot and now I'm taking its TDS"). It also removes the cross-page hazard entirely — there's no global state to leak between shots.

### Decision: Reject TDS < 3.0% as a calibration/accident filter

**Choice:** Hardcoded constant `kMinimumPlausibleTds = 3.0` (a `double` in `PostShotReviewPage.qml` or a `static constexpr` accessible to it). Any `tdsChanged` payload below this value is logged and dropped without touching the edit fields.

**Alternatives considered:**
- **Lower bound 5.0%:** safer for filtering low-extraction edge cases but starts to risk rejecting legitimate very-long shots; 3% is universally below real espresso and well above the observed corruption range (0.64% max).
- **Upper bound (e.g., reject > 25%):** considered, but the R2's "Beyond range" error path (`errClass==2, errCode==4`) already emits `errorOccurred` instead of `tdsChanged`, so the in-range pathway is already bounded by hardware.
- **Settings-tunable threshold:** premature flexibility; no current evidence the default is wrong.

**Rationale:** Every corrupted reading in the observed data was below 1.0%; 3.0% gives a 3× safety margin while sitting well below any plausible real espresso TDS.

### Decision: Keep MainController's reset-to-0 writes in place

**Choice:** The existing `setDyeDrinkTds(0)` / `setDyeDrinkEy(0)` writes in `MainController` at lines 1875–1876 and 2142–2143 (after each shot save) remain.

**Alternatives considered:**
- **Delete the Settings field entirely.** Tempting (it has no purpose after this change), but the field is also read by `MainController` at shot-end metadata capture and modified by MCP write tools. Keeping it as a vestigial channel is harmless; removing it requires a wider refactor.

**Rationale:** Defense in depth. If something else ever writes to `dyeDrinkTds` (the MCP path still does), the existing reset ensures it doesn't leak forward.

## Risks / Trade-offs

- **Risk:** A user presses the R2 button before the review page opens (e.g., immediately after shot ends, before navigation completes). → **Mitigation:** PostShotReviewPage opens within a few hundred ms of shot completion; the user would have to be extremely fast. Acceptable failure mode — the reading is dropped, the user can press again. Document in the user manual if needed.
- **Risk:** Tests that simulated R2 readings by directly invoking `Settings.dyeDrinkTds` setter will still work, but tests that relied on `tdsChanged` to indirectly populate Settings will break. → **Mitigation:** Update integration tests as part of this change; the new flow is to drive the QML page's `editDrinkTds` directly or to emit `tdsChanged` only when the page is visible.
- **Risk:** Future refactor adds a second consumer of `tdsChanged` (e.g., a TDS history view) and that consumer also needs gating. → **Mitigation:** Each consumer is responsible for its own context — the signal is intentionally not pre-filtered at emission, only at the consumption boundary. Document this rule in `difluidr2.h`'s header comment.
- **Trade-off:** The page becomes a slightly heavier consumer (binds to `MainController.refractometer` lifetime). Acceptable — the binding is short-lived (per-page-instance) and refractometer-null cases are already handled.

## Migration Plan

This is a behavior change, not a data change, so deployment is just shipping a new build.

1. Implement the fix on a feature branch.
2. Unit test the threshold and the page-visibility gating.
3. Manual test on a real device with an R2: confirm (a) reading appears when button is pressed during review, (b) reading is silently dropped when button is pressed outside review, (c) sub-3% readings are dropped with a log line.
4. Squash-merge to main (project standard).
5. Tag a release; CI handles the rest.

**Rollback:** Revert the squash commit. No persisted state to migrate back.

**One-time data cleanup:** Already done out-of-band (22 carry-over duplicates cleared via MCP; 11 implausible originators preserved as evidence per user request). Not part of this code change.

## Open Questions

- None blocking implementation. The 3% threshold is a judgment call; tightening or loosening it later is a one-line constant change.
