## Context

`TemperatureDisplay::format(stepTemps, anchorTemp, hasOverride, overrideTemp, fahrenheit, baselineShiftC)` ([src/profile/temperaturedisplay.cpp](src/profile/temperaturedisplay.cpp)) is the single formatter behind every temperature readout. It shifts each frame by `baselineShiftC`, and — only when `hasOverride` is true and the delta rounds ≥ 0.05 — appends a signed tag (`+2°`). The two-arg QML wrappers are `ProfileManager.temperatureDisplay(...)` (uses the active profile's frames) and `temperatureDisplayForSteps(...)` (explicit frames). The recipe cards already render the resulting value with `hasOverride=false` (`recipe-quick-switch`); the Brew Dialog and live Shot Plan did not.

## Goals / Non-Goals

**Goals:**
- Brew Dialog sub-indicator and live Shot Plan temperature show the resulting temperature, highlighted on deviation, with no signed delta tag.

**Non-Goals:**
- No change to the Temp Delta stepper (it still shows the offset being edited), the override-highlight color scheme, or the Clear/Update semantics (`recipe-aware-brew-settings`).
- No change to the recipe cards (already resulting-temp) or the frozen shot-detail/review readouts (already resulting-temp).

## Decisions

**Reuse the one formatter; flip `hasOverride` off and drive the shift from the effective offset.** No new formatting code, no per-frame string-building in QML.
- **BrewDialog** ([BrewDialog.qml](qml/components/BrewDialog.qml)): `temperatureDisplay(temperatureValue, false, temperatureValue, temperatureValue - profileTemperature)` — anchor and empty-frames fallback are the dialed value; frames shift by the full dialed offset; `hasOverride=false` suppresses the tag. The existing `deviatesFromBaseline` still drives the highlight color.
- **ShotPlanText** ([ShotPlanText.qml](qml/components/ShotPlanText.qml)) live-plan branch: `eff = tempOverridden ? overrideTemp : _tempHlBaseline`, then `temperatureDisplay(eff, false, eff, eff - profileTemp)`. The existing per-item `_tempOverride` flag drives the highlight.

**Highlight, not a tag, signals deviation.** The signed tag is redundant beside the editing stepper and forces mental math elsewhere; the app already has a per-item highlight channel for "differs from baseline", so the deviation signal moves entirely to color.

## Risks / Trade-offs

- **A recipe/profile at offset 0 shows the same string as before** (resulting == baseline), so the only visible change is when an offset/override is active — low regression surface. → Verified against the `brew-overrides` scenarios (updated in the delta).
- **The signed offset is no longer visible in the sub-indicator/plan**, only in the stepper (Brew Dialog) or not at all (Shot Plan). → Intended: the resulting temperature is the decision-relevant value; the offset remains on the stepper and in the recipe's stored `tempOffsetC`.
- **QML-only presentation; no test harness.** → Verified visually and by the updated spec scenarios; the C++ formatter is unchanged and already covered.
