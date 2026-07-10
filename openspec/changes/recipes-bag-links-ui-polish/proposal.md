# Recipes: bag-level links + production UI polish

## Why

Recipes debut in the next release, and two things keep the feature from being production-grade. First, the UI is uneven: step 1 of the wizard got a designed tile language, but the bean/profile pickers are bare text lists, the details step stretches a `− 0° +` stepper across 700px, the summary hides a latte's milk entirely, and the management cards can't distinguish two recipes on the same bean — users are already working around it by stuffing profile names into recipe names. Second, the bean-level bag resolution picks the most-recently-used open bag at activation (`resolveOpenBagStatic`, `ORDER BY last_used DESC`): for users running multiple bags of the same bean at different ages (e.g. frozen vs. counter), activation and grind inheritance silently land on the wrong bag.

## What Changes

- **Recipes link a specific bag, not a bean** (**BREAKING** — reverses the promoted "bean link is bean-level, not bag-level" rule from `add-recipes`). New `bag_id` column; bean identity fields (beanBaseId, roaster, coffee) are retained for display fallback and relink matching. One-time migration resolves existing recipes to their current open bag via the existing resolver logic.
- **Automatic relink lifecycle, zero options** (new capability):
  - *Roll-on-finish*: marking a bag empty relinks its recipes to the newest open bag of the same bean — except where the roll would duplicate an existing recipe on the target bag (same profile + same drink type); those stay on the finished bag.
  - *Wake-on-restock*: adding a new bag relinks stale recipes matching that bean identity (same dup-guard; twins wake MRU-first).
  - *Stale is a display state, never a lock*: a recipe whose bag is finished shows "bag finished" and still activates fully (the finished bag's grind is used). Toasts announce every automatic move; no dialogs, no settings.
  - *Manual swap*: the summary's Bag row reopens the bag step; a stale management card offers "Bag finished — tap to choose beans".
- **RecipesPage card redesign**: name stays the top-line anchor; new second line = drink-type icon + short label + profile (always shown) + milk weight when stored (bare "milk" word dropped); third line = bag + shot count; plan line unchanged. Lines wrap rather than elide. Profile-less hot-water tea cards show "Tea · Hot water" and the vessel snapshot (`200ml · 80°C`) instead of an empty plan line.
- **Short drink-type labels** (`Latte`, `Tea`, distinct `Americano`/`Long black`) used on cards, pills, and in auto-naming — fixes the shared water icon ambiguity by pairing icon with text, and the "Gran Bar Latte / Cappuccino" auto-name. `suggestName()` also stops duplicating the type word ("Milk Blend Espresso Espresso").
- **Empty-state upgrade**: two large tiles ("Start from a good shot" → history, "Build from scratch" → wizard) replace the gray sentence.
- **Wizard polish**: bean step becomes a bag-tile grid (photo, roaster, coffee, roast date/age — two bags of one bean are distinguishable choices; Add/No-bean as ghost tiles); profile step renders the ranked tiers as metadata tiles with reason chips, long tail stays a searchable compact list; details step right-sizes controls and fits one landscape screen with a two-column card grid, grind KB hint becomes a callout; summary step gets the recipe card as its WYSIWYG hero, single-glyph edit rows, and a visible Steam/milk row; pitcher/vessel/equipment pickers show preset metadata on rows.
- **Surfaces**: MCP recipe tools and the web `/recipes` API gain `bagId`; promote-from-shot carries the shot's bag. Idle recipe pills dim/badge stale recipes.

## Capabilities

### New Capabilities
- `recipe-bag-lifecycle`: automatic maintenance of the recipe→bag link across bag inventory changes — roll-on-finish, wake-on-restock, dup-guard, stale display state, manual re-point.

### Modified Capabilities
- `recipe-model`: "Bean linking resolves to the current open bag" is replaced by a hard bag link (`bag_id`) with bean identity retained as fallback; grind inherit targets the linked bag; migration requirement added.
- `recipe-activation`: activation applies the linked bag deterministically (no MRU resolution); stale recipes activate fully with the finished bag's data.
- `recipe-wizard`: bean step → bag-tile step; profile step tier tiles; details step layout constraints; summary hero card + edit rows + visible steam row; sub-picker metadata rows; short-label auto-naming.
- `recipe-quick-switch`: management-page card layout redesign; empty-state tiles; stale pill indication.
- `shotserver-recipes`: recipe payloads round-trip `bagId`.
- `mcp-server`: recipe tools expose/accept `bagId`.

## Impact

- **Schema**: `recipes` table gains `bag_id` (kCols row + CREATE TABLE + migration step, per the bags rule); one-time data migration.
- **C++**: `RecipeStorage` (link, resolver removal/repurpose, relink queries), `MainController` (activation, relink orchestration + toasts), `CoffeeBagStorage` finish/add event hooks, `mcptools_recipes.cpp`, `shotserver_recipes.cpp`.
- **QML**: `RecipesPage.qml` (cards, empty state), `RecipeWizardPage.qml` (all five steps + sub-pickers), `RecipesItem.qml` (stale pills), possibly a shared recipe-card component for the WYSIWYG summary hero.
- **Docs/specs**: `docs/CLAUDE_MD/RECIPES.md` (bean-level → bag-level, lifecycle), promoted specs under `openspec/specs/`.
- **Tests**: recipe storage/migration/relink unit tests; MCP register-stub gotcha (`tst_mcpserver_session/protocol`) applies when tool signatures change.
- **Out of scope**: web editor visual polish (data field only), the shared "Add a new coffee…" Bean Base dialog, any new user-facing settings.
