# Design — Add Recipes

## Context

Today the app has three optional layers on top of profiles: **bags** (`CoffeeBagStorage`, write-through DYE fields, MRU inventory, delete-vs-finished lifecycle), **equipment packages** (`EquipmentStorage`, soft-remove lifecycle), and **shot history** with a Load action (`MainController::loadShotWithMetadata` → `applyLoadedShotMetadata`) that already restores profile (installed or embedded JSON), matched bag, bean/grinder DYE fields, dose, and temperature/yield overrides. Steam settings (`SettingsBrew`: temperature, flow, timeout, pitcher presets, `steamDisabled`, `keepSteamHeaterOn`) are global. The idle screen's Beans button (`BeansItem.qml`) shows the 5 most-recently-used bags as pills with double/long-press opening the management page. MCP tools are registered per-domain (`mcptools_*.cpp`, read/write access levels); the ShotServer is split per-domain (`shotserver_*.cpp`) behind shared auth, and has **no** bag or equipment surface.

A recipe is therefore mostly a *re-scoping and naming* of data the app already captures per-shot or globally — plus one genuinely new axis: per-drink steam.

## Goals / Non-Goals

**Goals:**
- One-tap switching between named whole-drink setups, including steam.
- Promotion of a proven shot into a recipe from every surface that shows shots (app, web, MCP).
- Strict optionality: zero behavior change for users who never create a recipe.
- One shared controller so QML, MCP, and web activation can't diverge.
- Web management parity for bags and equipment (needed by the recipe web page anyway).

**Non-Goals:**
- Hot-water and rinse/flush steps in the recipe (deferrable; schema leaves room).
- Reference-curve live overlay, per-recipe SAW learning, ProfileExpectation integration (follow-ups).
- Any new "favorite" flag or user-facing eco/steam-mode setting.
- Changing profile file formats or the profile pipeline.

## Decisions

### D1. Recipe is a row, not a file
`recipes` table in the shot-history DB, managed by a new `RecipeStorage` (src/history/), cloned structurally from `CoffeeBagStorage`: async request/ready signal API, background threads via `withTempDb`, `QVariantMap` payloads to QML. Rationale: recipes join against bags and shots (provenance, shot counts) — same DB, same patterns, same test approach. Flexible/optional sub-fields (steam block, pinned grind) stored as JSON text columns to keep migrations rare.

### D2. Grind: inherit by default, pin by exception
Grind remains a bag property (aging/re-dials are bean events; sibling recipes follow automatically). A recipe stores `grindPinned` (nullable string — `dyeGrinderSetting` is free-form text, so deltas are impossible). Resolution order: pin → linked bean's current open bag → recipe-local value (bean-less recipes). Write-through routing while a recipe is active: grind changes go to the **bag** when inherited (conservative default; pinning is a deliberate act in the composer), to the **pin** when pinned.

### D3. Steam block with snapshot pitcher
Recipe steam spec: `hasMilk`, `milkWeightG`, pitcher **snapshot** (name + volume, not a preset index — presets are a mutable global list; snapshot-not-reference per the Bean Base precedent), steam temperature/flow/timeout. On activation these write into the live `SettingsBrew` values (which propagate to the DE1 as today). `hasMilk` **derives** heater behavior: the DE1 steam heater takes 5–9 minutes to warm (measured on hardware), so an active milk recipe HOLDS the heater on — sendMachineSettings treats it like `keepSteamHeaterOn`, surviving every re-send while the recipe is active and the machine awake; deactivation releases the hold. No new user-facing steam-mode setting. `keepSteamHeaterOn` remains the baseline when no recipe is active.

### D4. Activation = extended shot-load, in one controller
`MainController` gains recipe activation that reuses the `applyLoadedShotMetadata` pipeline (profile by name with stored-JSON fallback, bag-first-then-fields ordering, queued dose write) plus the steam write and heater decision. Active recipe id lives in `SettingsDye` (beside `activeBagId`). QML pill taps, MCP `recipe_activate`, and the web `/activate` route all call this one path. Steam settings write on **recipe switch** (not shot start) so heater warm-up time is hidden.

### D5. Tweaks write through; swaps deactivate
While a recipe is active: dose/yield/temp/steam/milk changes write through to the recipe (bag-style, no dirty state). Changing the *profile*, *bag/bean*, or *equipment* clears the active recipe (event-based, in the respective setters/controllers — no timers). Rationale: tuning refines the recipe; swapping an ingredient means the user has left it. Prevents browsing profiles from silently rewriting a named drink.

### D6. Lifecycle mirrors bags; shots record provenance
`shots.recipe_id` (nullable) + a steam-spec snapshot on the shot (so promote-from-shot round-trips steam). Recipe with 0 shots → hard-deletable; with shots → archive only (hidden from pickers, retained for history). Same rule and wording as bags. Bean linking is by Bean Base canonical id when present (else brand+name identity), resolving to the current open bag at activation; "no open bag of this bean" is a display state, never an error.

### D7. One composer, three entry points
A single create/edit window (new QML page) reused for: blank creation (Recipes page), promotion (prefilled from a shot's record + current steam settings; button placed beside Load on ShotHistoryPage, on ShotDetailPage, and on AutoFavoritesPage rows), and clone (copy fields, focus name, provenance = source recipe, golden-shot link not copied). Pickers reuse `ChangeBeansDialog`, `SwitchEquipmentDialog`, `ProfileSelectorPage`. Only name + profile required. Swapping the linked bag shows an inline hint that inherited grind now follows the new bag.

### D8. Idle widget is a BeansItem clone
`RecipesItem.qml` copies `BeansItem.qml` structurally: tap → pill row of last-5-used recipes (MRU from `RecipeStorage`), pill → activate, double/long-press → Recipes page, empty → straight to page. Registration in CMakeLists, `LayoutItemDelegate`, `widgetCatalogTable()`, `LayoutCenterZone`. MRU replaces any favorite flag. Activating a recipe sets `activeBagId`, so the Beans button's selection follows automatically.

### D9. MCP and ShotServer are thin adapters
`mcptools_recipes.cpp`: `recipe_list/get/create/update/create_from_shot/clone/archive/activate`; house conventions (`doseG`, `milkWeightG`, ISO 8601, grind as `{"mode": "inherited"|"pinned", "value": ...}`); read tools at read level, mutations at write level (same as preset tools). `shotserver_recipes.cpp`, `shotserver_bags.cpp`, `shotserver_equipment.cpp`: REST + embedded management pages following the existing per-domain split, async handler and `fetch()` rules from SHOTSERVER.md, behind existing auth. All mutations funnel through `RecipeStorage`/`CoffeeBagStorage`/`EquipmentStorage` and the MainController activation path — no parallel logic.

## Risks / Trade-offs

- [Heater warm-up is 5–9 minutes (measured)] → An active milk recipe holds the heater on continuously (not a one-time warm), so the drink is ready whenever the user steams; `keepSteamHeaterOn=true` users are unaffected either way (the hold only ever turns the heater ON, never fights an explicit keep-on).
- [Write-through surprises: a user tweaks dose "just once" and the recipe changes] → Same semantics users already know from bags ("no dirty state"); composer always shows current values; acceptable by precedent.
- [Deactivate-on-swap feels abrupt] → Pill visibly deselects (same affordance as bag pills); no data is lost — the recipe is unchanged, the user is simply free-styling.
- [Steam settings BLE write on switch adds traffic] → One settings write per recipe switch, well within the 50ms write spacing and command queue.
- [Three new web surfaces expand the ShotServer attack/maintenance surface] → All behind existing auth; pages follow the established embedded-page pattern; endpoints are thin wrappers over storage classes already covered by tests.
- [Free-form grind strings make pins non-comparable across grinders] → Accepted; pins are per-recipe opaque text, resolution never interprets them.
- [MCP register-signature changes break test stubs] → Known gotcha; tasks include updating `tst_mcpserver_session`/`tst_mcpserver_protocol` stubs and `tst_mcptools_*` externs and building `--target all`.

## Migration Plan

Single forward SQLite migration in `ShotHistoryStorage` (new `recipes` table; `shots.recipe_id` + steam-snapshot columns, nullable → old rows unaffected). No settings migration: absence of an active recipe reproduces today's behavior exactly. Feature is invisible until a user creates a recipe or places the widget; rollback = don't use it (schema additions are inert).

## Open Questions

- Whether the web pages get the AI "Get info from page" bag niceties from the current bag-detail-editing branch in v1 (default: no — fields only).
- Recipes page sort: MRU vs alphabetical with active-first (default: MRU, matching the pills).
