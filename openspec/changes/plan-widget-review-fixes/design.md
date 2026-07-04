# Design: plan-widget-review-fixes

## Context

PR #1396 (branch `pr/presentation-and-steam`, head `241f21a0`) rewrote `ShotPlanText.qml` into a sentence renderer, added `SteamPlanText.qml` and the page-aware `PlanItem.qml`, and reworked the idle steam preset area. Two review rounds fixed most findings; seven verified issues remain (see the 2026-07-04 review comment on the PR). The maintainer is fixing these directly on the PR branch. The sentence renderer already has the right structure for the fixes: per-toggle segment strings (`_roasterStr`, `_grindStr`, …) feeding a single `_build(fmt, sep)` used by both the plain `text` and rich `_rich`.

## Goals / Non-Goals

**Goals**
- Restore pre-#1396 toggle semantics (Roaster = brand, Coffee = bean name) and add a Grind (+RPM) toggle.
- Restore the yield-override arrow; keep the temperature-only highlight.
- Make the pitcher prompt honest about the beep; keep the beep gated on `doseCaptureSoundEnabled`.
- Fix the regressions: `ScaleDevice` guards, steam-sentence pitcher duplication, stale displayed steam duration.
- Collapse page-state to the `Theme` singleton; delete the window-root duplicates and the `Connections` mirroring.
- Clear the two small leftovers: `ShotPlanItem` a11y i18n, `BeanSummary` header comment.

**Non-Goals**
- Restoring the live per-pill milk-weight suffix on idle steam pills (explicitly deferred in review).
- Any change to the beep's own gating or defaults (no new settings — per project preference for fewer settings).
- Renaming/migrating existing persisted option keys.

## Decisions

**D1 — Toggle keys: keep `shotPlanShowGrind` for grind, add `shotPlanShowCoffee` for the coffee name.**
The alternative (keeping `shotPlanShowGrind` meaning "coffee" and adding a grind key) preserves one more edge case of saved configs but permanently leaves a key named "Grind" controlling the coffee name — a trap for every future reader. With this decision the only behavior shift for saved configs is that users who had "Coffee (grind)" OFF now see the coffee name until they toggle the new Coffee option off (its default ON matches main's default rendering). Editor labels become "Coffee" and "Grind" — each label finally states exactly what it controls.

**D2 — Grind segment renders "grind {setting} · {rpm} rpm"** using `Settings.dye.dyeGrinderSetting` and `dyeGrinderRpm` (0 = unset → omitted). Mirrors `EquipmentSummary.lastDialLine`'s wording so grind+rpm reads the same everywhere.

**D3 — Yield arrow returns under the deliberate-override flag.** `_yieldStr` renders `profileYield.toFixed(1) + " → " + targetWeight.toFixed(1) + "g"` when `Settings.brew.hasBrewYieldOverride && profileYield > 0 && |targetWeight − profileYield| > 0.1` — the exact condition main used. The highlight stays temperature-only (the PR's documented reasoning is sound for the highlight; it never justified dropping the arrow).

**D4 — Prompt honesty via conditional hint line, not sound changes.** The second line of the pitcher prompt binds on `Settings.brew.doseCaptureSoundEnabled`: beep wording when true, "hold still until the weight registers" wording when false. Alternatives rejected: always playing the ding for milk capture (overrides an explicit user setting), auto-enabling the sound at calibration (changes a setting behind the user's back).

**D5 — Pitcher dedupe via a second translation template.** When the preset name contains "pitcher" (case-insensitive, same test as `pillLabelFn`), use `"Steam %1 of milk, using the %2 for %3"`; otherwise keep the existing template with the word. Two full templates keep translators in control of word order; string-surgery on one template does not.

**D6 — Displayed steam duration derives from the selected preset, via one shared helper.** A new `SettingsBrew::effectiveSteamDurationSec(pitcherIndex, milkG)` returns the scaled time when > 0, else the preset's base `duration`. `SteamPlanText` displays it (feeding its session/calibration/last-milk fallback chain as `milkG`), and IdlePage's pill-tap handler uses it for the `steamTimeout` write — the "scaled-or-base" ternary currently duplicated in QML moves next to `scaledSteamTime`, honoring the weight-timed-steaming spec's rule that the scaling math lives in exactly one place. `steamTimeout` semantics (what the machine uses) are unchanged.

**D7 — Page state moves to `Theme`.** `Theme.currentPageObjectName` already exists and is updated by main.qml's page-change handler; add `Theme.currentOperationMode` beside it, published by IdlePage's existing `_publishOperationMode()`. `PlanItem` binds `Theme.*` directly (singleton properties are fully reactive), deleting the two window-root properties, `winRoot` mirroring, `_refreshPage()`, and the `Connections` block. `SteamPlanText`'s milk mirroring via `Window.window.sessionMeasuredMilkG` stays as-is (that property has no Theme equivalent and moving it is out of scope).

**D8 — `ScaleDevice &&` guards restored verbatim** at the three sites (`idlePitcherDetect.rawWeight`, `idlePitcherDetect.active`, steam `onPresetSelected`), matching every other reference in the file (#1399 convention).

**D9 — Styled-dot sweep is selective and gated.** The remaining *data rows* (BagCard, EquipmentSummary, BeanBaseDetailsRow, ChangeBeansDialog) convert to `Theme.joinWithBullet` + `Text.StyledText` so the whole app uses one separator treatment; incidental captions (FlowCalibration axis title, SettingsAITab status, ShotHistory filter caption, WeatherItem credit) stay plain — a bolder dot buys nothing there. The sweep is gated on an empirical check that `elide: ElideRight` still works with StyledText in Qt 6.11 (BeanSummary already ships that combination unverified against overlong names); if eliding is broken the sweep stops and BeanSummary itself gets flagged. A graphic (SVG `<img>`) separator was rejected: indistinguishable from the bold `·` at body sizes, baseline-alignment risk across platforms, no color-following, and unusable in the plain-`Text` sites anyway. Conversion also routes user data through the helper's HTML escaping — hand-rolled `<b>`/`<font>` markup at these sites without escaping would be a regression.

## Risks / Trade-offs

- [Saved configs with "Coffee (grind)" OFF start showing the coffee name] → Accepted in D1; one-tap fix by the user, and the new default matches main's default rendering. Called out in the PR comment.
- [New option key must be whitelisted everywhere] → `shotPlanShowCoffee` added to `settings_network.cpp`'s option whitelist, both editors, and `tst_settings.cpp`; the existing test pins the whitelist so a miss fails the build's test run.
- [`plan`/`shotPlan` share the option set] → Both widget types read the same modelData keys; `PlanItem` and `ShotPlanItem` both gain the `showCoffee` pass-through, so a missed wrapper would silently default ON — covered by a scenario in `plan-widgets`.
- [Theme gains UI-flow state (`currentOperationMode`)] → `Theme.currentPageObjectName` set the precedent; keeping both on one singleton is the lesser evil vs. a second mechanism (the exact problem being fixed).

## Migration Plan

All work lands as commits on `pr/presentation-and-steam` so PR #1396 merges as one squash. No data migration; option keys are additive. Rollback = drop the commits from the branch.

## Open Questions

None — the three judgment calls (beep wording, yield arrow, Theme migration) were decided with the maintainer in session.
