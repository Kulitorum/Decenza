## Why

Opening a new bag leaves you guessing a starting point, while Visualizer holds thousands of shots other people pulled with the same bean, and your own history for the bean is scattered across shot history. Visualizer's API can now search community shots by bean (`GET /api/shots?community=1&full=1`), so Decenza can show both for any bag on the Beans page, on request, and turn the community's best result into a recipe.

## What Changes

- A **Suggestions** button on each coffee bag card on the Beans page, opening a dialog with:
  - **Your history:** your profiles for the bean (with when each was last used) and the last grind dialed for it, labelled when it comes from a similar roast.
  - **Community:** shot and people counts, the most liked profile with median dose, yield, ratio and time, its grind when pulled on your grinder, and a short list of shots when there are too few to rank.
  - **Apply:** after a confirmation sheet, saves the suggestion as a new recipe for that bag (named after the bean and profile, numbered on a repeat) and selects it. Your current recipe is not changed.
  - **Create recipe:** opens the recipe wizard prefilled with the suggestion.
  - **Open on Visualizer:** the community page filtered to the bean.
  - **From the web:** opt-in research by your selected AI provider, shown as cited advice, never applied.
- The same card in the **new-bag window**, behind a Suggestions button once a roaster and coffee are entered. There Apply and Create recipe are a single choice, carried out when the bag is saved.
- Settings: community search on/off, minimum enjoyment (with a control in Settings → Visualizer, default 70), an API address for a local Visualizer, and cross-grinder grind (off; no grinder data yet).
- MCP `bean_search` gains `action=community`; the web Beans page shows the top community profiles and the Visualizer link.

## Capabilities

### New Capabilities

- `bean-community-search`: your history and the community's for a coffee bean, with Apply and Create recipe, over the app, MCP and the web.

## Impact

- New: `CommunityAggregator`, `CommunitySettingsClient`, `CommunitySuggestionsCard.qml`.
- Changed: `BeanInfoPage.qml`, `SettingsVisualizerTab.qml`, `SettingsVisualizer` (+ backup of the floor), `SettingsDye::dyeEffectiveGrinder`, `MainController` (client, Apply), `RecipeStorage` (opt-in name numbering), `ProfileManager::beverageBucketForTitle`, `AIManager::researchBean`, `GrinderAliases` (micron scale, unused), `mcptools_beansearch.cpp`, `shotserver_bags.cpp`, MCP surface 1.20.0.
