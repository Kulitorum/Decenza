# Proposal

## Why

The recipe editor can select a water vessel but cannot add or edit one without a detour to Hot Water. A fixed water amount also makes users adjust two settings whenever they change an Americano or long black's espresso target; an editable water-to-espresso ratio can keep the dilution consistent.

## What Changes

- Add visible Add vessel and Edit actions to the recipe's Hot Water window, using a shared vessel editor with the standalone Hot Water page.
- In recipes with an espresso target, offer fixed added water or an editable multiple of target espresso yield. New vessels start at 2.5×; existing snapshots keep their saved amount specification.
- Reuse a common ratio chooser with the espresso editor. Water starts with 2×, 2.5× and 3× choices and supports custom values; its picks never change espresso presets or the live espresso ratio.
- Store the water ratio on the recipe's by-value vessel snapshot. Shared vessel presets remain fixed amounts, and standalone Hot Water editing remains fixed-only, with 100 g as the default for newly added vessels.
- Resolve ratio water on recipe activation and when the current effective espresso target changes, including dose changes that move a ratio-based target. Repeated drinks at the same target and actual shot output do not cause recalculation.
- Show the ratio and derived added-water grams together. Preserve recipe snapshots, fixed-volume support, and request-scoped conflict handling across native and Wasm clients.

## Capabilities

### New Capabilities

- `water-vessel-editing`: Shared vessel creation/editing, context-specific amount choices and defaults, an editable water-ratio chooser, and explicit save/cancel behavior.

### Modified Capabilities

- `recipe-model`: Extend the hot-water vessel snapshot with a mutually exclusive fixed amount or water-to-espresso-yield ratio, preserved through recipe and shot round trips.
- `recipe-activation`: Resolve recipe water against the effective espresso target and refresh on target changes without mutating the stored ratio or shared vessel preset.

## Impact

- QML: `RecipeWizardPage.qml`, `HotWaterPage.qml`, `RatioPresetDialog.qml`, shared vessel/ratio controls, and recipe summaries.
- Host: hot-water activation, active-recipe refresh, target-change notifications, preset selection, and shot setup capture in `MainController`, `ProfileManager`, and recipe wiring.
- Shared services: `RecipeContext` preset operations and `RecipeService` recipe edits; existing revision/precondition protections apply in both shells.
- Persistence/API: additive fields inside `hot_water_json`; MCP conversion/schema, HTTP recipe JSON, promotion and backup/transfer must preserve the amount specification. No new table or column is expected. Legacy HTML editing must preserve fields, without a second new UI implementation.
- Tests: extend existing recipe storage, wiring, promotion, machine-control, context-service and profile/target tests; verify native/Wasm interactions. Update `RECIPES.md` and the wiki manual.
- Existing fixed recipes and presets retain their behavior. The separate one-tap drink-sequence change remains responsible for sequencing; this change supplies its water target.
