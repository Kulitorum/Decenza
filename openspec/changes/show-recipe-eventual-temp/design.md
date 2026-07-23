## Context

The recipe wizard's Dose/yield/temp/grind step edits temperature as a signed **offset** on the selected profile — stored as `tempOffsetC`, held in the page as `fTempDeltaC`, and shown through a `ValueInput` labeled "Temp offset" ([`RecipeWizardPage.qml:3028-3061`](qml/pages/RecipeWizardPage.qml#L3028)). The control displays only the offset (`+2°`), so the eventual temperature is invisible unless the user already knows the profile's default (issue #1604).

The brew-settings dialog solved the identical problem: its "Temp Delta" input is followed by a subtext line rendering the resulting temperature via `ProfileManager.temperatureDisplay(...)` → e.g. `Profile: 84 · 94°C` ([`BrewDialog.qml:938-968`](qml/components/BrewDialog.qml#L938)). Both surfaces should read the same way.

The formatter is shared C++ (`TemperatureDisplay::format`, [`temperaturedisplay.cpp:62`](src/profile/temperaturedisplay.cpp#L62)). It takes step temperatures, an anchor, an override flag/value, and a baseline shift, and returns at most two readings (single value, two distinct joined by `·`, or first…last for a ramp), unit-aware, with a signed offset tag.

The wizard already tracks everything the readout needs: `fProfileStepTemps` (the selected profile's per-frame temps), `fProfileTempC` (base `espresso_temperature`), and `fTempDeltaC` (the offset). No new state and no C++ changes are required.

## Goals / Non-Goals

**Goals:**
- Show the eventual brew temperature beside the recipe wizard's Temp offset control, reading identically to the brew-settings Temp Delta readout.
- Reuse the existing formatter and existing page state — zero C++, single-file QML change.

**Non-Goals:**
- No change to the tea absolute-temperature field or hot-water tea (no profile anchor).
- No change to the summary hero (already shows the effective temp via `summaryEffectiveTempStr()`).
- No change to the brew-settings dialog.
- No change to how the offset is stored or applied.

## Decisions

**Use `temperatureDisplayForSteps`, not `temperatureDisplay`.**
`ProfileManager::temperatureDisplay` reads `m_currentProfile` internally — the app's *active* profile. The wizard edits an **arbitrarily selected** profile that is generally not the active one, so that path would format the wrong profile's frames. The `*ForSteps` sibling ([`profilemanager.cpp:2974`](src/controllers/profilemanager.cpp#L2974)) takes explicit step temps, which the page already holds in `fProfileStepTemps`. Call:

```
ProfileManager.temperatureDisplayForSteps(
    fProfileStepTemps,             // selected profile's frame temps (unshifted °C)
    fProfileTempC,                 // anchor: base espresso_temperature (empty-frames fallback + tag)
    Math.abs(fTempDeltaC) > 0.05,  // hasOverride
    fProfileTempC + fTempDeltaC,   // overrideTemp
    fTempDeltaC)                   // baselineShiftC → shifts every frame to the eventual temp
```

With `baselineShiftC = fTempDeltaC`, the frame readings render as the **eventual** temperatures, and `delta = overrideTemp − anchor = fTempDeltaC` produces the signed tag. This is exactly the argument shape BrewDialog uses, differing only in passing wizard-owned step temps instead of the active profile's.

**Arrow prefix, no translation key.** The readout is `"→ " + <formatter output>`. The formatter output carries units but no translatable words, and `→` (U+2192) is covered by the bundled Noto Sans Math fallback, so no new `TranslationManager` key is added. (Chosen with the user over a "New temp:" label, which would crowd the narrow grid cell.)

**Unit reactivity.** `temperatureDisplayForSteps` reads the °C/°F setting in C++, which is not a QML-capturable dependency. As BrewDialog does at [`BrewDialog.qml:954`](qml/components/BrewDialog.qml#L954), touch `void(Settings.app.temperatureUnit)` inside the `text` binding so it re-renders on a unit switch.

**Placement and visibility.** Add one `Text` inside the existing Temp offset `ColumnLayout`, directly under the `ValueInput`, so it sits in the same grid cell beneath the stepper (`Layout.preferredWidth: 190`). `visible: fProfileTempC > 0` — the offset control is already disabled when the profile temp is unresolvable, so the readout simply hides there. Colour follows the adjusted/neutral convention already used by the stepper (`Theme.temperatureColor` when `|fTempDeltaC| > 0.1`, else `Theme.textSecondaryColor`). Accessibility: `Accessible.role: StaticText`, `Accessible.name: text`.

## Risks / Trade-offs

- **`fProfileStepTemps` empty on some entry path** → **Traced and cleared.** Every entry path (fresh-create via `selectProfile` at [`RecipeWizardPage.qml:1043`](qml/pages/RecipeWizardPage.qml#L1043); edit-existing, clone, and promote-from-shot all via `applyRecipeMap` → `refreshProfileTemp` at [`:536`](qml/pages/RecipeWizardPage.qml#L536)) populates `fProfileStepTemps` from the resolved profile's `steps[].temperature`. `refreshProfileTemp` ([`:754`](qml/pages/RecipeWizardPage.qml#L754)) resolves the profile by title, falling back to embedded/snapshot JSON, so it never leaves the array empty while `fProfileTempC > 0` — the per-frame form renders on all paths. The single-value fallback at [`:787`](qml/pages/RecipeWizardPage.qml#L787) fires only for a profile whose steps carry no per-step temperature, which is correct. **Pre-existing, out of scope:** on promote, `refreshProfileTemp` reads the *currently-installed* profile's frames (by title) while the offset conversion at [`:712-739`](qml/pages/RecipeWizardPage.qml#L712) anchors on the shot's *snapshot* temperature; if the installed profile changed since the shot, the displayed frames reflect today's profile. The readout stays correct and per-frame; this mismatch predates the change.
- **QML-only change, no test harness** → Per project convention QML is verified manually. Confirm in the running app across offset 0 / +N / −N, a single-temp profile, a multi-temp profile, a °C↔°F switch, and an uninstalled/unresolvable profile.
- **Divergence from brew settings over time** → Mitigated by both surfaces calling the one `TemperatureDisplay::format`; the only wizard-specific input is the step-temps source.
