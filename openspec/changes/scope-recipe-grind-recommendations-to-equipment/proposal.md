## Why

The recipe wizard can suggest a recent grind from an unrelated grinder or basket even after the user selected the recipe's equipment. Because those components materially change extraction behavior, that can offer a completely unsuitable starting point.

## What Changes

- Scope the recipe wizard's coffee grind-history recommendation to the selected equipment package as well as the bean identity (with same-roast fallback), then fall back only to another package with the same grinder and basket.
- Treat a grinder as its full brand/model/burr identity. Puck-preparation differences may be ignored only by the fallback; a different grinder or basket shall never supply a recommendation.
- Refresh and stale-guard the asynchronous recommendation when the wizard's equipment selection changes.
- Keep the existing equipment-agnostic behavior only when the recipe deliberately has no selected package.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `recipe-wizard`: Grind-history hints must prioritize complete selected-equipment matches, with a grinder-and-basket fallback that ignores puck preparation.

## Impact

- `qml/pages/RecipeWizardPage.qml` recommendation request and reply handling.
- `ShotHistoryStorage` grind-history lookup APIs and their SQLite query predicates.
- Existing coffee-bag/recipe-wizard history tests, including full-package priority, grinder-and-basket fallback, and rejection of different grinders or baskets.
