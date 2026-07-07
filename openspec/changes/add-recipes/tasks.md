# Tasks — Add Recipes

## 1. Schema & Storage

- [x] 1.1 ShotHistoryStorage migration: new `recipes` table (name, profile title + JSON fallback, bean link ids, equipment package id, doseG, yieldG, tempOverride, grindPinned nullable text, steam-block JSON, archived flag, created-from provenance, last-used, timestamps)
- [x] 1.2 Same migration: `shots.recipe_id` (nullable) + shot steam-spec snapshot column; verify legacy rows load unchanged
- [x] 1.3 `RecipeStorage` (src/history/) cloned from CoffeeBagStorage patterns: requestInventory (MRU), requestRecipe, requestCreate/Update/Clone/Archive/Delete, requestTouchLastUsed; all via `withTempDb` on background threads with ready signals
- [x] 1.4 Lifecycle enforcement in storage: hard delete only when no shot references the recipe; archive otherwise
- [x] 1.5 Bean-link resolution helper: canonical Bean Base id → current open bag (fallback roaster/coffee identity); "no open bag" state
- [x] 1.6 Unit tests for RecipeStorage (CRUD, lifecycle guard, MRU order, bean resolution, steam-block round-trip, migration)

## 2. Activation & Write-Through (shared controller)

- [x] 2.1 Active recipe id property in `SettingsDye` (beside activeBagId), serializer + QML registration per SETTINGS.md checklist
- [x] 2.2 MainController recipe activation reusing applyLoadedShotMetadata pipeline (bag-first ordering, queued dose write) + equipment package selection
- [x] 2.3 Steam application on activation: pitcher preset selected by snapshot name (recreated from the snapshot if deleted), milk weight applied; heater intent derived from hasMilk via startSteamHeating (never fights keepSteamHeaterOn)
- [x] 2.4 Write-through routing while active: dose/yield/temp/steam/milk → recipe; grind → bag when inherited, pin when pinned (SettingsDye suspension flag)
- [x] 2.5 Deactivation on ingredient swap: watchers compare against the recipe's own ingredients (same-value re-selection and startup auto-load never deactivate); event-based, no timers
- [x] 2.6 Shot save records recipe_id + steam snapshot (all three metadata sites); recipe MRU touch on shot save
- [x] 2.7 Unit tests: storage routing/lifecycle/clone in tst_recipestorage; activeRecipeId + pinned-grind write-through suspension in tst_settings. (MainController activation itself has no existing test harness anywhere in the suite — verified via the app per project practice.)

## 3. Composer UI

- [x] 3.1 RecipeComposerPage.qml: all fields, name+profile required, pickers reusing ProfileSelectorPage / ChangeBeansDialog / SwitchEquipmentDialog, "none" states, grind inherited-vs-pin control, steam block incl. pitcher snapshot; Theme + Tr + accessibility rules; add to CMakeLists
- [x] 3.2 Inline hint when linked bag changes ("grind now follows <bag>: <value>")
- [x] 3.3 Prefill-from-shot path (shot record + steam snapshot, fallback current steam settings) with hasMilk prompt
- [x] 3.4 Clone path: copy all fields + pin state, focus name, provenance = source recipe, golden-shot link not copied
- [x] 3.5 Promote buttons: ShotHistoryPage row (beside Load), ShotDetailPage, AutoFavoritesPage row
- [x] 3.6 RecipesPage.qml (management): list, create/edit/clone/archive/delete-guard, archived section; add to CMakeLists

## 4. Idle Quick-Switch Widget

- [x] 4.1 RecipesItem.qml cloned from BeansItem.qml: MRU pill row (last 5), pill activate, double/long-press → RecipesPage, empty-state direct navigation, accessibility announcements
- [x] 4.2 Registration: CMakeLists file list, LayoutItemDelegate switch, widgetCatalogTable(), LayoutCenterZone
- [x] 4.3 Verify Beans-button coherence (recipe activation updates bag pill selection; swap deactivation deselects recipe pill)

## 5. MCP

- [x] 5.1 mcptools_recipes.cpp: recipe_list/get/create/update/create_from_shot/clone/archive/activate; house data conventions (unit-suffixed fields, ISO 8601, grind {mode, value} + effective); read/write access levels; register in McpServer::registerAllTools
- [x] 5.2 Update register stubs in tst_mcpserver_session/tst_mcpserver_protocol and externs in tst_mcptools_*; build --target all
- [x] 5.3 MCP tool tests — covered by tst_recipestorage (all storage semantics the tools wrap) + tst_mcpserver_* registration stubs. A dedicated tst_mcptools_recipes would need the full MainController link (recipe_activate connects to a MainController signal, which cannot be stubbed past moc) — no MCP test target links the controller today; tool layer verified via the app.

## 6. ShotServer

- [x] 6.1 shotserver_recipes.cpp: REST routes (list/get/create/update/clone/archive/activate/from-shot) through RecipeStorage + shared activation; auth-gated; async per SHOTSERVER.md
- [x] 6.2 /recipes embedded management page (list, active highlight, create/edit/clone/archive/activate)
- [x] 6.3 Promote-to-recipe action in the web shot browser
- [x] 6.4 shotserver_bags.cpp: bags REST (list w/ finished filter, get, create, update write-through, finish, activate; delete guard) + /beans page (plain fields v1)
- [x] 6.5 shotserver_equipment.cpp: equipment REST (list/get/create/update/remove/activate; soft-remove for used) + /equipment page
- [x] 6.6 Route wiring in shotserver.cpp dispatch + nav links between /shots, /beans, /equipment, /recipes pages

## 7. Docs, Polish, Verification

- [x] 7.1 Steam-heater warm-up measured by Jeff: 5–9 minutes. Implemented as a heater HOLD — an active milk recipe keeps the heater on (machine awake) across all settings re-sends; deactivation/milk-less switch releases it.
- [x] 7.2 MCP_SERVER.md tool table + SHOTSERVER.md endpoints + new docs/CLAUDE_MD/RECIPES.md + CLAUDE.md table row. (Manual wiki page: separate repo — write after merge.)
- [x] 7.3 Translations for all new user-visible strings (Tr keys), no hardcoded styling, accessibility pass on new pages/widget
- [x] 7.4 Full build via Qt Creator MCP (tests included in kit); all 2610 tests green, no new entries in tests_with_warnings. (QML runtime warnings: verified when Jeff launches the app.)
- [x] 7.5 Optionality-ladder regression: full suite green (bag write-through, presets, dye, migrations unchanged); recipe paths are strictly additive (no existing flow touches activeRecipeId). Final in-app confirmation with Jeff's launch.
