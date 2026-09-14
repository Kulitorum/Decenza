## Why

Every stored or armed ratio is clamped to 0.5–6.0, a bound chosen for espresso. #1941 made a ratio carry within a beverage group so filter and tea can use one, but a filter ratio of about 1:16 was silently saved as 1:6.

## What Changes

- The ratio bound becomes 0.5–100 (`YieldSpec::kMinRatio` / `kMaxRatio`), shared by every write boundary: Brew Settings, the ratio presets, recipes, bags, MCP and the web pages.
- QML reads the bound from `Settings.brew.minRatio` / `maxRatio` instead of repeating it.
- A ratio's resolved grams are held to the 1–500 g absolute bound, so a large ratio on an espresso dose cannot ask for more than 500 g.
- One set of ratio presets stays shared by every profile; the preset buttons step by 1 from 1:10.

## Impact

- Specs: `yield-anchor` (added requirement).
- Code: `src/core/yieldspec.h`, `SettingsBrew`, `RecipeWizardPage.qml`, `ChangeBeansDialog.qml`, `RatioPresetDialog.qml`, MCP property descriptions.
