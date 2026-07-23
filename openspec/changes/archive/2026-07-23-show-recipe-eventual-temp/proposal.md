## Why

In the recipe wizard's Dose/yield/temp/grind step, the temperature control edits an **offset** on the profile (`+2°`, `0°`, `-1°`) but never shows the temperature that offset resolves to. A user who wants to brew at a specific temperature — say 95 °C — cannot dial it in without already knowing the profile's default temperature, which the wizard does not display on that step (issue [#1604](https://github.com/Kulitorum/Decenza/issues/1604)). The brew-settings "Temp Delta" control already solved this exact problem by showing the resulting temperature beneath the offset stepper; the recipe wizard should read the same way.

## What Changes

- Add a single read-only subtext line beneath the "Temp offset" stepper on the recipe wizard's coffee/espresso details step, showing the **eventual brew temperature** the current offset produces (e.g. `→ 94°C`, `→ 96°C +2°`, `→ 88…94°C`).
- The line renders through the same formatter the brew-settings Temp Delta uses (`TemperatureDisplay::format`, via `ProfileManager.temperatureDisplayForSteps`), so the two surfaces read identically — at most two temperatures shown (single value, two distinct, or first…last for ramps), unit-aware (°C/°F), with a signed offset tag when non-zero.
- Espresso/coffee path only. Tea already edits an absolute temperature and needs no change. Hot-water tea has no profile anchor and is unaffected.
- Update the wiki manual's recipe-wizard section to note the resulting-temperature readout.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `recipe-wizard`: The details step's coffee/espresso temperature control gains a requirement to display the resulting (post-offset) brew temperature, not only the offset.

## Impact

- **Code**: `qml/pages/RecipeWizardPage.qml` only — one added `Text` element inside the existing "Temp offset" `ColumnLayout` (~lines 3028–3061). No C++ changes: `ProfileManager::temperatureDisplayForSteps` and the wizard's `fProfileStepTemps` / `fProfileTempC` / `fTempDeltaC` state already exist.
- **No new translation key** — the readout is an arrow prefix plus a formatter-produced value carrying no translatable words.
- **Docs**: Decenza wiki `Manual` recipe-wizard section.
- **Out of scope**: the summary hero (already shows the effective temperature via `summaryEffectiveTempStr()`), the tea absolute-temperature field, and the brew-settings dialog (unchanged).
