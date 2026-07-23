## Why

Wherever a recipe/profile temperature offset is shown next to (or downstream of) the control that edits it, the UI showed the **baseline temperature plus a signed offset tag** (e.g. `88 · 93°C +2°`), forcing the reader to add the two together to know what the machine will actually brew — and, in the recipe wizard and brew settings, duplicating the `+2°` already shown in the adjacent stepper. The standard should be: **show the resulting temperature and highlight it when it differs from the baseline; never a baseline plus a signed delta tag.** The recipe cards already worked this way (`recipe-quick-switch`); this brings the Brew Dialog subtext and the live Shot Plan into line.

## What Changes

- **Brew Dialog temperature sub-indicator** now shows the **resulting** temperature(s) — the frames shifted by the dialed offset — updating live as the Temp Delta stepper moves, instead of the unshifted baseline. No signed delta tag (the stepper already shows the offset). Highlight still marks a deviation from the baseline.
- **Live Shot Plan** temperature portion now shows the resulting (effective) temperature(s) with the existing per-item override highlight signalling a deviation, instead of the unshifted baseline plus a signed delta tag.
- Presentation-only follow-ons already shipped in the same PR and not requiring spec changes: the recipe wizard's Temp offset readout and summary card (drop the redundant tag; `recipe-wizard`'s requirements don't pin the tag), and the ShotServer web recipe card (shows the resulting temp like the app card; mirrors `recipe-quick-switch`).

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `brew-overrides`: the **Brew Dialog** temperature sub-indicator and the **Shot Plan Display** temperature portion change from "unshifted baseline + signed delta tag" to "resulting temperature, highlighted on deviation, no tag."

## Impact

- **Code (already implemented in PR #1616):** `qml/components/BrewDialog.qml`, `qml/components/ShotPlanText.qml` (plus non-spec presentation tweaks in `qml/pages/RecipeWizardPage.qml`, `src/controllers/profilemanager.{h,cpp}`, `src/network/shotserver_recipes.cpp`).
- **Specs:** `brew-overrides` (two requirements modified). `recipe-quick-switch` already specifies resulting-temp for recipe cards and is unchanged. `recipe-aware-brew-settings` (the override-highlight color scheme, the Temp Delta stepper reading `0°` at the recipe baseline) is unchanged — only the sub-indicator's *content* moves from baseline+tag to resulting.
