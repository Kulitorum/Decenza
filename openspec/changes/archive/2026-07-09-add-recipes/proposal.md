# Add Recipes

## Why

A profile tells the machine how to push water; it says nothing about the rest of the drink. Users juggling several open bags — each dialed differently, plus family milk drinks with their own steam — currently recreate a known-good setup by hunting through shot history and pressing Load, and steam settings are global so they must be re-adjusted by hand for every drink change. A **Recipe** names the whole drink (profile, bean, equipment, dose, yield, grind routing, steam) and makes it one tap to switch, one click to save from a good shot, and one object the AI advisor, the web UI, and the app can all share. (Concept validated by the OverDose community skin; this design is Decenza-native, built on the existing bag/equipment/DYE systems.)

## What Changes

- **New Recipe entity + storage**: a named bundle of profile (required) + bean link + equipment package + dose/yield/temp + grind (inherited from bag by default, pin optional) + steam spec (hasMilk, milk weight, pitcher snapshot, temp/flow/timeout). New SQLite table alongside `coffee_bags`; shots gain a `recipe_id` provenance column.
- **Governing principle — the optionality ladder**: profile alone always pulls a shot; beans, equipment, and recipes are strictly additive rungs. No flow ever requires a recipe; no nagging to create one; a recipe works with whatever rungs the user has (bean-less and equipment-less recipes are valid).
- **Activation semantics**: activating a recipe applies profile, bag, equipment, DYE fields, brew overrides, and steam settings (reusing the existing shot-load pipeline), and derives steam-heater warm/cold from `hasMilk`. Parameter tweaks while active write through to the recipe (bag-style); swapping an ingredient (profile/bag/equipment) deactivates the recipe.
- **Grind routing**: grind stays a bag property. Recipes inherit the bag's grind by default (re-dials update all sibling recipes); a recipe may pin its own grind for profile styles that need it. Without a linked bean, grind lives on the recipe.
- **Lifecycle mirrors bags**: a recipe with no shots is deletable; a used recipe can only be archived/retired so history provenance never dangles.
- **Recipe composer window** with three entry points: blank (from the Recipes page), prefilled from a shot (new button beside Load on Shot History, Shot Detail, and Auto-Favorites), and clone-and-edit. Reuses the existing bag/equipment/profile pickers.
- **Idle-screen quick switch**: a Recipes layout widget cloning the Beans button pattern — tap shows the last-5-used recipes as pills (MRU, no favorite flag), pill tap activates, double-tap/long-press opens the Recipes management page, empty state goes straight to the page.
- **MCP full control**: new recipe tool family (list/get/create/update/create_from_shot/clone/archive/activate) following house data conventions.
- **ShotServer web surfaces**: new REST APIs + management pages for recipes (`/recipes`), bags (`/beans` — no bag web interface exists today), and equipment (`/equipment`), giving the web UI parity with the optionality ladder. Web shot browser gains the same promote-to-recipe action.
- **Single controller**: QML, MCP, and ShotServer all route through one shared recipe controller so activation semantics cannot drift between surfaces.

## Capabilities

### New Capabilities
- `recipe-model`: Recipe entity, SQLite storage, field semantics (grind inherit/pin, steam block with pitcher snapshot), bean-level linking with current-open-bag resolution, lifecycle (delete vs archive), shot provenance.
- `recipe-activation`: What activating a recipe applies (profile, bag, equipment, DYE, overrides, steam, heater state), write-through routing for tweaks, deactivation on ingredient swaps, behavior when referenced rungs are absent.
- `recipe-composer`: The create/edit window, its three entry points (blank, promote-from-shot, clone), picker reuse, required-vs-optional fields, prefill rules.
- `recipe-quick-switch`: Idle-screen Recipes layout widget — MRU pill row, activation, long-press to management page, empty-state behavior, widget registration.
- `shotserver-recipes`: Recipes REST API + `/recipes` web management page + promote action in the web shot browser.
- `shotserver-bags`: Bags REST API + `/beans` web management page (create/edit/finish/activate, lifecycle rules).
- `shotserver-equipment`: Equipment REST API + `/equipment` web management page mapping the EquipmentStorage package API.

### Modified Capabilities
- `mcp-server`: Gains the recipe tool family (full CRUD + activate + create-from-shot) at the established read/write access levels.
- `layout-widget-catalog`: Catalog gains the Recipes widget entry (3-place registration + center-zone allowance).

## Impact

- **Schema**: new `recipes` table; `shots` table gains `recipe_id` (nullable) and a steam-spec snapshot so promote-from-shot round-trips; migration in `ShotHistoryStorage`.
- **C++**: new `RecipeStorage` (src/history/, following `CoffeeBagStorage` patterns — background-thread I/O via `withTempDb`), recipe controller logic in/alongside `MainController` (extends `applyLoadedShotMetadata`), steam-settings write + heater derivation in the brew/steam settings path, new `mcptools_recipes.cpp`, new `shotserver_recipes.cpp` / `shotserver_bags.cpp` / `shotserver_equipment.cpp`.
- **QML**: recipe composer + Recipes page, promote buttons on ShotHistoryPage/ShotDetailPage/AutoFavoritesPage, `RecipesItem` layout widget (CMakeLists + LayoutItemDelegate + widgetCatalogTable + LayoutCenterZone registration), all new files added to `qt_add_qml_module`.
- **Tests**: RecipeStorage unit tests; MCP register-stub updates in `tst_mcpserver_session`/`tst_mcpserver_protocol` and `tst_mcptools_*` externs; ShotServer route tests where present.
- **Docs**: `MCP_SERVER.md` tool table, `SHOTSERVER.md` endpoints, manual wiki page for Recipes.
- **Unchanged**: all existing flows — profile-only brewing, bag write-through without recipes, global steam behavior when no recipe is active, existing MCP/web surfaces.
