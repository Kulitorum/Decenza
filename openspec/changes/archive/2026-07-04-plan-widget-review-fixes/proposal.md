# Proposal: plan-widget-review-fixes

## Why

PR #1396 (shot-plan sentence, steam plan, page-aware Plan widget) went through two review rounds; the maintainer is taking over the branch to fix what remains. Seven verified review findings are unaddressed — including a silent remap of the Shot Plan display toggles that changes what existing users' saved widgets show, a dropped null-guard that reintroduces a fixed startup crash (#1399), and a prompt that promises a beep that never plays in the default configuration. While restoring the toggle semantics, the toggle set is also being extended with a proper Grind toggle (grind setting + RPM), which the sentence rewrite made room for.

## What Changes

- **Restore Shot Plan toggle semantics**: "Roaster" shows the roaster brand only; "Coffee" gets the coffee (bean) name back. Saved widget configurations render the same content they did before PR #1396.
- **New "Grind (+ RPM)" display toggle** for the Shot Plan / Plan widgets: shows the grinder setting and, when set, the grinder RPM (`Settings.dye.dyeGrinderSetting` / `dyeGrinderRpm`). The existing persisted `shotPlanShowGrind` key continues to gate it (so users who hid grind keep it hidden); the coffee name gets a new `shotPlanShowCoffee` key, default ON.
- **Restore the yield-override arrow**: when the user has deliberately dialed a yield override (`hasBrewYieldOverride`), the plan shows `36.0 → 40.0g`, matching the temperature-override treatment.
- **Honest pitcher prompt**: the "wait for the beep" hint appears only when the capture sound option (`doseCaptureSoundEnabled`) is on; otherwise the prompt asks the user to hold until the weight registers. The beep itself remains gated on the option (no behavior change to the sound).
- **Steam sentence pitcher-name dedupe**: a preset named "Large Pitcher" no longer renders "using the Large Pitcher pitcher".
- **Restore `ScaleDevice &&` null guards** in IdlePage's new steam code paths (regression of #1399's fix).
- **Single source of page state**: PlanItem binds to `Theme.currentPageObjectName` (existing) and a new `Theme.currentOperationMode`; the duplicate window-root properties and the rename-fragile `Connections` mirroring are removed.
- **Plan widget editor clarity**: the page-aware Plan widget's settings dialog is titled "Plan Settings" and notes that the display toggles apply to the shot-plan side (the steam plan has no options).
- Remaining small review items: `ShotPlanItem.qml` translated `Accessible.name`; `BeanSummary.qml` header comment corrected; steam-plan displayed duration derived from the selected preset rather than the last-tap `steamTimeout` residue.
- **Separator visibility sweep**: the remaining single-line data rows (BagCard, EquipmentSummary, BeanBaseDetailsRow, ChangeBeansDialog) adopt the styled bold-dot separator via `Theme.joinWithBullet`, completing PR #1396's original "bigger separators" goal with the safe glyph. Gated on verifying elide still works with StyledText in Qt 6.11; incidental captions (axis titles, status lines) keep the plain `·`.
- **Widget consolidation (maintainer decision, round 2)**: the separate `plan` and `steamPlan` widget types are removed before release. Instead the existing **Shot Plan** widget becomes page-aware behind a new **Steam plan** toggle (`shotPlanShowSteamPlan`, default ON): in steam context it shows the steam sentence, elsewhere the shot sentence. One widget, one settings dialog, no migration debt (the two types never shipped; saved layouts containing them render nothing and the widget is simply re-added). **BREAKING** only for branch testers.

Out of scope (explicitly deferred in review, unchanged): restoring the live per-pill milk-weight suffix on the idle steam pills.

## Capabilities

### New Capabilities
- `plan-widgets`: presentation requirements for the Shot Plan text, Steam Plan text, and page-aware Plan widget — sentence structure, per-toggle content mapping (including the new Grind toggle), override indicators (temperature highlight, yield arrow), steam pitcher-name dedupe, page-aware switching, and displayed steam duration. These widgets shipped in PR #1396 without a spec; this codifies their contract.

### Modified Capabilities
- `layout-widget-instance-config`: the Shot Plan / Plan widget option set gains the Grind toggle (six options total) and the "Coffee" option is renamed/re-scoped to the coffee name; the page-aware Plan widget's editor gets its own title and steam-mode note.
- `weight-timed-steaming`: the idle-page pitcher-placement prompt requirement — shown only while weight-timed steaming is enabled, steam is selected, a supported scale is connected, and nothing is on the scale; its beep wording appears only when the capture sound option is enabled.

## Impact

- **QML**: `ShotPlanText.qml` (toggle segments, yield arrow, new grind+rpm segment), `SteamPlanText.qml` (pitcher dedupe, duration source), `PlanItem.qml` (Theme-based page state), `SteamPlanItem.qml` (unchanged unless shared code moves), `ShotPlanItem.qml` (a11y i18n), `IdlePage.qml` (guards, prompt wording, operation-mode publishing to Theme), `Theme.qml` (`currentOperationMode`), `main.qml` (remove window-root duplicates), `ScreensaverEditorPopup.qml` + `BeanSummary.qml` (small fixes).
- **C++**: `src/network/shotserver_layout.cpp` and `src/core/settings_network.cpp` (new `shotPlanShowCoffee` option key in the web editor + option whitelist); `src/core/settings_brew.{h,cpp}` (new `effectiveSteamDurationSec()` helper).
- **Tests**: `tests/tst_settings.cpp` (option-key coverage for the new toggle).
- **Persistence/back-compat**: `shotPlanShowGrind` keeps gating the grind (now grind + RPM); the coffee name is gated by the new `shotPlanShowCoffee` (default ON, matching main's default-visible coffee name). No migration needed.
- Branch: fixes land on `pr/presentation-and-steam` (PR #1396) so the whole effort merges as one squash.
