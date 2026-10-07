## Context

Weight-timed steaming decides between a scaled steam duration and the recipe/preset's fixed duration in `SettingsBrew::scaledSteamTime(index, milkG)` (`src/core/settings_brew.cpp:614-624`):

```cpp
int SettingsBrew::scaledSteamTime(int index, double milkG) const {
    if (!milkAutoCaptureEnabled()) return 0;  // toggle gates ALL weight scaling, not just auto-capture
    QVariantMap p = getSteamPitcherPreset(index);
    if (p.isEmpty() || p.value("disabled").toBool()) return 0;
    if (milkG <= 0.0) return 0;
    double secPerGram = steamSecondsPerGram();
    if (secPerGram <= 0.0) return 0;  // uncalibrated
    return qBound(5, qRound(secPerGram * milkG), 120);
}
```

Note this reads a single **global** `steamSecondsPerGram` rate, not a per-pitcher `calibMilkG`/`duration` pair — the comment at `settings_brew.cpp:619` says so explicitly ("No longer reads the per-preset calibMilkG/duration for scaling"). `openspec/specs/weight-timed-steaming/spec.md`'s "Per-pitcher calibration" requirement describes the old per-pitcher ratio math and is stale against the current implementation; reconciling that spec is a separate follow-up, noted here so it isn't mistaken for settled truth while investigating this bug.

`SteamPage.qml` calls this via two thin wrappers (`scaledSteamTimeout()` at `SteamPage.qml:346-348`, `steamTimeForMilk()` at `SteamPage.qml:351-353`), consumed at the steam-start decision point (`onIsSteamingChanged`, `SteamPage.qml:152-176`) and the page-activation sync (`syncSteamTimeout()`, `SteamPage.qml:434-448`). Both already fire for a machine-driven (GHC) entry into Steam exactly as they would for a user-tapped one — confirmed by two rounds of static investigation that found no code path clearing `AppShell.sessionMeasuredMilkG` during an espresso shot, in `PostShotReviewPage.qml`, or (for this user's confirmed "straight through, no reselection" workflow) via recipe-driven pitcher reassignment.

That leaves a fourth, unconfirmed gate any of which returns `0` and forces the fixed-duration fallback, silently, with no distinguishing signal in the current debug output:
1. `milkAutoCaptureEnabled()` false (master toggle off).
2. Selected pitcher preset missing/disabled.
3. `milkG <= 0` — no milk actually reached the scaling call (this is where a capture lost somewhere upstream would show up).
4. `steamSecondsPerGram() <= 0` — global rate never calibrated.

None of `SteamPage.qml`'s existing debug lines (`SteamPage.qml:119-132`, phase/state/isSteaming changes) log which of these four gates fired, or the actual milk/rate values at decision time. The live settings surface exposed over MCP (`steam_pitcher_list`, `settings_get category=steam`) does not expose `milkAutoCaptureEnabled` or `steamSecondsPerGram` either, so the cause can't be confirmed retroactively from outside the app.

## Goals / Non-Goals

**Goals:**
- Make the fixed-vs-scaled decision fully diagnosable from the debug log: which of the four gates (if any) blocked scaling, and what the actual captured-milk/rate/pitcher values were at that instant.
- Cover both call sites that can apply the fallback (steam-start in `onIsSteamingChanged`, and `syncSteamTimeout()` on page activation/pitcher-lift), since either could be the one that ran for this user's GHC-from-Shot-Review sequence.
- Keep it QML-side `console.log`, matching `SteamPage.qml`'s existing debug-logging style (`SteamPage.qml:119-132`) — this is UI/session logic, not a device/radio/transport subsystem, so it is out of scope for the C++ marker-macro gate in `docs/CLAUDE_MD/LOGGING.md` (that document's scope is explicitly "device, radio or transport subsystem").

**Non-Goals:**
- No behavior change. Nothing about which duration gets applied changes in this pass — only what gets logged.
- Not fixing `openspec/specs/weight-timed-steaming/spec.md`'s stale per-pitcher-calibration description — flagged above, deferred.
- Not adding new persisted settings, new MCP surface, or new Settings properties.

## Decisions

**Decision: log the four gate values and the outcome at both call sites, once per steam-start/activation, not per-frame.**

At `SteamPage.qml:152-176` (`onIsSteamingChanged`, when `isSteaming` becomes true) and `SteamPage.qml:434-448` (`syncSteamTimeout()`), add one `console.log` each, right where the scaled-vs-fallback decision is made, reporting:
- `AppShell.sessionMeasuredMilkG` and `steamPage.lastOnScaleMilk` (the two candidate milk sources `capturedMilkForScaling()` chooses between).
- `Settings.brew.milkAutoCaptureEnabled`, `Settings.brew.steamSecondsPerGram`, and the selected pitcher's name/disabled state.
- The `scaledSteamTimeout()`/`steamTimeForMilk()` result and, if it's `0`, which of the four `scaledSteamTime()` gates is the likely cause (inferred client-side from the same values being logged — no new C++ accessor needed since every value read by `scaledSteamTime()` already has a QML-visible property).
- The final duration actually applied (`Settings.brew.steamTimeout`) and its source (`"scaled"` vs `"fixed-fallback"` vs `"user-adjusted, unchanged"`).

Log once per `isSteaming` transition to `true` and once per `syncSteamTimeout()` call (already only called from `StackView.onActivated` and a couple of explicit triggers per the existing code, not per-frame) — no new throttling/collapsing needed, this isn't a periodic source.

Alternatives considered:
- *Add a C++ accessor that returns which gate failed, instead of inferring it in QML.* Slightly cleaner, but every value `scaledSteamTime()` reads is already a plain `Q_PROPERTY` (`milkAutoCaptureEnabled`, `steamSecondsPerGram`, `getSteamPitcherPreset`), so a QML-side log line can reconstruct the same answer without a new C++ surface. Revisit if the C++ logic grows more branches.
- *Wait for a repro with `qWarning` set to trace level instead of adding permanent log lines.* Rejected — this is a one-time GHC-driven repro the user can't easily force on demand; a standing, always-on debug line (matching the page's existing style) is what actually catches it the next time it happens organically.
- *Fix the openspec spec drift (per-pitcher calibration text) in this same change.* Rejected — unrelated to the reported bug and would blur what this change is actually verifying; tracked as a follow-up instead.

## Risks / Trade-offs

- **[Risk]** Logging every steam-start doesn't help if the real cause is something upstream of `scaledSteamTime()` entirely (e.g. `capturedMilkForScaling()` never seeing a value that was in fact captured, for a reason neither investigation round found). **Mitigation**: logging both `AppShell.sessionMeasuredMilkG` directly and the four gate values together will still show "milk was there, everything was enabled/calibrated, yet result was fixed" versus "milk was already 0 by steam-start" — either outcome narrows the next investigation immediately instead of requiring a third blind round.
- **[Risk]** `console.log` output on a real device may not always be captured unless the user is on a debug build or has debug logging enabled. **Mitigation**: this matches the existing convention already in the file (`SteamPage.qml:119-132`), which the user must already be relying on for other diagnostics; no new capture mechanism needed.

## Migration Plan

None — logging only, no schema/data changes, no rollback concerns beyond reverting the diff.
