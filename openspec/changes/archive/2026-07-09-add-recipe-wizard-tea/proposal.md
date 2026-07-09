# Proposal: add-recipe-wizard-tea

## Why

The recipe composer (add-recipes) is a single-page form: four cards, all fields visible at once, flat alphabetical profile picker. It works, but creating a recipe feels like data entry rather than describing a drink, and nothing in the form uses what the app already knows (which profiles the user brews with which beans, what dose/yield actually worked). Replacing it with a drink-type-first wizard makes creation faster — each step narrows the next (drink type filters bags and profiles; bean + profile prefill the numbers from shot history) — and it opens the door to tea as a first-class drink: the DE1 ships 15 `tea_portafilter` profiles, but the app has no way to represent a bag of tea, no tea-aware extraction, and no way to compose a tea recipe.

## What Changes

- **Recipe wizard replaces the composer.** New multi-step creation flow: drink type (espresso, filter, americano, long black, latte/cap, tea) → bean (kind-filtered, skippable) → profile (drink-type-filtered, ranked by history with this bean, search retained) → drink-type-specific details (prefilled from the most recent shot with that bean+profile pair) → summary. The summary page IS the edit page: edit/clone/promote-from-shot land directly on it and dive into individual steps. Templates set defaults but never restrict — milk/water blocks can be added or removed on the summary. **BREAKING** (UI only): the one-window `RecipeComposerPage` is removed; all entry points route to the wizard.
- **`drink_type` stored on recipes.** New column recording user intent; drives pill icons, wizard re-entry, and MCP/web fields. Derived from blocks for pre-existing and promoted recipes.
- **Profile-less (hot-water-only) recipes.** Validation becomes "profile required unless a hot-water block is present". Activation skips the profile stage; the user starts Hot Water. Covers hot-water tea (the less common tea path; portafilter tea is primary).
- **Tea bags.** New `kind` column on `coffee_bags` (`"coffee"` default, `"tea"`), set at creation and never edited. Bag Inventory gains a secondary "Add Tea" button beside "Add Coffee"; the tea flow skips the Visualizer canonical search lane (verified live: tea queries return coffee false-positives) and shows a tea-shaped form (Brand/Tea labels; no roast level, grind, or canonical link; URL + photo kept). Visualizer shot upload and bag sync are unchanged for tea.
- **Tea extraction.** Second extraction system prompt selected by bag kind: teaType, origin/region, garden/estate, cultivar, flush/harvest, tasting notes, plus structured brewing fields (`brewTempC` normalized to Celsius, `leafGramsPer100Ml`, steep time) stored in the bag blob. These seed the wizard: teaType matches the stock tea profile names (bag → profile recommendation), and brewing numbers seed temp/dose (verbatim for hot-water tea; as overrides for portafilter tea only when the profile isn't type-matched).
- **URL extraction hardened (coffee and tea).** Two-stage: the existing local page fetch stays stage 1; on `emptyPage`/blocked, stage 2 sends the URL to the configured AI provider's web-fetch tool to retrieve and extract server-side (verified working on fortnumandmason.com, a JS SPA that yields 48 chars locally). Stage 2's JSON adds an `imageUrl` key so bag photos work where og:image is absent. The 20k page-text cap is raised (~48k) — verified case where nav cruft pushes product content against the cap.
- **Per-drink-type equipment defaults.** The wizard's equipment row prefills with the last package used on a recipe of that drink type (fallback: current active package). Equipment packages learn to be grinder-less (basket-only) for tea — a create-path change, no schema change.
- **Profile ranking query.** New shots-table read: profiles grouped by bean identity (recency-ordered), with a similar-bean tier (roast level for coffee, teaType for tea) and a temperature-proximity tier for tea.

## Capabilities

### New Capabilities

- `recipe-wizard`: the drink-type-first creation/edit flow — step sequence and auto-advance, drink-type templates, kind-filtered bean step, ranked+filtered profile step (including the "Just hot water" row), drink-type-specific details with history/bag-data prefill, summary-as-edit-page, per-drink-type equipment defaults, name auto-suggestion.

### Modified Capabilities

- `recipe-composer`: the single-window composer requirements are removed/superseded; promotion, clone, and edit entry points re-target the wizard summary.
- `recipe-model`: gains `drink_type` column; profile becomes optional when a hot-water block is present; hot-water-only recipes remain zero-shot (always hard-deletable).
- `recipe-activation`: activation of a profile-less recipe skips the profile/dose/yield stages and applies bag + hot-water block only.
- `recipe-quick-switch`: pills gain a drink-type icon.
- `coffee-bag-model`: gains `kind` column (creation-time, immutable, rides transfer/backup); bag blob vocabulary gains structured tea brewing fields.
- `bag-inventory-view`: header gains the "Add Tea" entry point beside "Add Coffee".
- `change-beans-dialog`: tea creation flow — Visualizer lane suppressed, past-tea-bags search, tea-shaped form.
- `bag-detail-editing`: kind-selected extraction prompt; structured tea brewing fields; two-stage URL extraction with provider web-fetch fallback and `imageUrl`; page-text cap raise.
- `equipment-package-model`: packages MAY be grinder-less (basket-only).
- `shotserver-recipes`: web recipe forms gain drink type; profile validation relaxed to match.
- `mcp-server`: `recipe_create`/`recipe_update`/`recipe_get`/`recipe_list` carry `drinkType` and accept profile-less hot-water recipes; bag tools expose `kind`.

## Impact

- **Schema**: `recipes.drink_type` (one migration), `coffee_bags.kind` (one migration). Bag blob fields are schemaless JSON (no migration).
- **C++**: `RecipeStorage`, `CoffeeBagStorage` (kCols + migrations), `ShotHistoryStorage` (ranking query), `MainController::applyActivatedRecipe` (profile-less path), `EquipmentStorage` (grinder-less create), `AIManager` (tea prompt, stage-2 extraction), `BeanBaseClient` (cap, imageUrl download), `AIProvider` subclasses (web-fetch tool request), MCP recipe/bag tools, `shotserver_recipes`/`shotserver_bags`.
- **QML**: new wizard pages (CMakeLists registration), `RecipesPage`/`ShotHistoryPage`/`ShotDetailPage`/`AutoFavoritesPage` entry-point rerouting, `BeanInfoPage` buttons, `ChangeBeansDialog` tea mode, `RecipesItem` pill icons. `RecipeComposerPage.qml` deleted.
- **Tests**: MCP register-stub duplication (`tst_mcpserver_session`/`protocol`, `tst_mcptools_*`) must be updated with any tool-signature change; new unit tests for ranking query, migrations, extraction parsing, profile-less activation.
- **Docs**: `RECIPES.md`, `BEAN_BASE.md`, `MCP_SERVER.md` sections; user manual (wiki) pages for recipes and bag inventory.
- **Translations**: all new wizard/tea strings via `TranslationManager`/`Tr`.
