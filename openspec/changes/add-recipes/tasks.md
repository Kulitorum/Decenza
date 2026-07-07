# Tasks — Add Recipes

## 1. Schema & Storage

- [x] 1.1 ShotHistoryStorage migration: new `recipes` table (name, profile title + JSON fallback, bean link ids, equipment package id, doseG, yieldG, tempOverride, grindPinned nullable text, steam-block JSON, archived flag, created-from provenance, last-used, timestamps)
- [x] 1.2 Same migration: `shots.recipe_id` (nullable) + shot steam-spec snapshot column; verify legacy rows load unchanged
- [x] 1.3 `RecipeStorage` (src/history/) cloned from CoffeeBagStorage patterns: requestInventory (MRU), requestRecipe, requestCreate/Update/Clone/Archive/Delete, requestTouchLastUsed; all via `withTempDb` on background threads with ready signals
- [x] 1.4 Lifecycle enforcement in storage: hard delete only when no shot references the recipe; archive otherwise
- [x] 1.5 Bean-link resolution helper: canonical Bean Base id → current open bag (fallback roaster/coffee identity); "no open bag" state
- [x] 1.6 Unit tests for RecipeStorage (CRUD, lifecycle guard, MRU order, bean resolution, steam-block round-trip, migration)

## 2. Activation & Write-Through (shared controller)

- [ ] 2.1 Active recipe id property in `SettingsDye` (beside activeBagId), serializer + QML registration per SETTINGS.md checklist
- [ ] 2.2 MainController recipe activation reusing applyLoadedShotMetadata pipeline (bag-first ordering, queued dose write) + equipment package selection
- [ ] 2.3 Steam application on activation: write steam block into SettingsBrew (propagates to DE1), pitcher/milk weight applied; heater state derived from hasMilk with keepSteamHeaterOn never overridden to off
- [ ] 2.4 Write-through routing while active: dose/yield/temp/steam/milk → recipe; grind → bag when inherited, pin when pinned
- [ ] 2.5 Deactivation on ingredient swap: profile change, bag/bean change, equipment change clear active recipe (event-based flags, no timers)
- [ ] 2.6 Shot save records recipe_id + steam snapshot; MRU touch on shot start with recipe active
- [ ] 2.7 Unit tests for activation semantics, write-through routing, deactivation triggers

## 3. Composer UI

- [ ] 3.1 RecipeComposerPage.qml: all fields, name+profile required, pickers reusing ProfileSelectorPage / ChangeBeansDialog / SwitchEquipmentDialog, "none" states, grind inherited-vs-pin control, steam block incl. pitcher snapshot; Theme + Tr + accessibility rules; add to CMakeLists
- [ ] 3.2 Inline hint when linked bag changes ("grind now follows <bag>: <value>")
- [ ] 3.3 Prefill-from-shot path (shot record + steam snapshot, fallback current steam settings) with hasMilk prompt
- [ ] 3.4 Clone path: copy all fields + pin state, focus name, provenance = source recipe, golden-shot link not copied
- [ ] 3.5 Promote buttons: ShotHistoryPage row (beside Load), ShotDetailPage, AutoFavoritesPage row
- [ ] 3.6 RecipesPage.qml (management): list, create/edit/clone/archive/delete-guard, archived section; add to CMakeLists

## 4. Idle Quick-Switch Widget

- [ ] 4.1 RecipesItem.qml cloned from BeansItem.qml: MRU pill row (last 5), pill activate, double/long-press → RecipesPage, empty-state direct navigation, accessibility announcements
- [ ] 4.2 Registration: CMakeLists file list, LayoutItemDelegate switch, widgetCatalogTable(), LayoutCenterZone
- [ ] 4.3 Verify Beans-button coherence (recipe activation updates bag pill selection; swap deactivation deselects recipe pill)

## 5. MCP

- [ ] 5.1 mcptools_recipes.cpp: recipe_list/get/create/update/create_from_shot/clone/archive/activate; house data conventions (unit-suffixed fields, ISO 8601, grind {mode, value} + effective); read/write access levels; register in McpServer::registerAllTools
- [ ] 5.2 Update register stubs in tst_mcpserver_session/tst_mcpserver_protocol and externs in tst_mcptools_*; build --target all
- [ ] 5.3 MCP tool tests (tst_mcptools_recipes): CRUD, activation, lifecycle guard, convention compliance

## 6. ShotServer

- [ ] 6.1 shotserver_recipes.cpp: REST routes (list/get/create/update/clone/archive/activate/from-shot) through RecipeStorage + shared activation; auth-gated; async per SHOTSERVER.md
- [ ] 6.2 /recipes embedded management page (list, active highlight, create/edit/clone/archive/activate)
- [ ] 6.3 Promote-to-recipe action in the web shot browser
- [ ] 6.4 shotserver_bags.cpp: bags REST (list w/ finished filter, get, create, update write-through, finish, activate; delete guard) + /beans page (plain fields v1)
- [ ] 6.5 shotserver_equipment.cpp: equipment REST (list/get/create/update/remove/activate; soft-remove for used) + /equipment page
- [ ] 6.6 Route wiring in shotserver.cpp dispatch + nav links between /shots, /beans, /equipment, /recipes pages

## 7. Docs, Polish, Verification

- [ ] 7.1 Measure DE1 steam-heater warm-up on hardware; tune heater-off aggressiveness (ask Jeff to run on-machine check)
- [ ] 7.2 MCP_SERVER.md tool table + SHOTSERVER.md endpoint docs + new BEAN/recipes notes; manual wiki page for Recipes
- [ ] 7.3 Translations for all new user-visible strings (Tr keys), no hardcoded styling, accessibility pass on new pages/widget
- [ ] 7.4 Full build with -DBUILD_TESTS=ON via Qt Creator MCP; all tests green, no new QML runtime warnings
- [ ] 7.5 Optionality-ladder regression check: recipe-less flows unchanged (profile-only brew, bag write-through, global steam behavior)
