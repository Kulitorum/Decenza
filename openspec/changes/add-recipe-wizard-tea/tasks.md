# Tasks: add-recipe-wizard-tea

## 1. Schema and data model

- [x] 1.1 Migration + kCols row for `recipes.drink_type`; derivation function (blocks + profile beverage_type → type) with unit tests covering all seven values and the ambiguous milk+water case
- [x] 1.2 Migration + kCols row for `coffee_bags.kind` ("coffee" default); verify transfer/backup import carries both new columns (extend existing import tests)
- [x] 1.3 Relax `RecipeStorage` save validation to "profile required unless hot-water block with hasWater"; unit test the three validation cases
- [x] 1.4 Tea blob vocabulary: teaType/garden/cultivar/flush/brewTempC/leafGramsPer100Ml/steepTime read/write helpers on the bag blob; unit tests

## 2. Profile ranking and prefill queries

- [x] 2.1 `ShotHistoryStorage` ranked-profiles read: (profile_name, MAX(timestamp)) grouped by bean identity, plus similar-bean tier (roast_level / teaType); background thread, request/ready signals; unit tests against a seeded shots DB
- [x] 2.2 Most-recent-shot-for-(bean,profile) prefill lookup (reuse shot projection read); unit test
- [x] 2.3 Last-package-for-drink-type equipment default query on `RecipeStorage`; unit test
- [x] 2.4 Tea profile type-match keyword table (stock title → teaType) + temp-proximity ordering helper; unit tests including no-brewTempC fallback to alphabetical

## 3. Profile-less activation

- [x] 3.1 `applyActivatedRecipe` early branch: skip profile/dose/yield stages when profile_title empty; apply bag + equipment + hot-water block; no heater hold; `recipeActivated`/`activeRecipeId` semantics unchanged
- [x] 3.2 Unit-test the profile-less gate helpers (`Recipe::hotWaterActive` / `saveValidationPasses` in tst_recipestorage; `activeRecipeHasMilk` profile-less rule noted in code). NOTE: scope adjusted — MainController has no unit-test harness (no tst_maincontroller exists), so the full activation path (profile untouched, vessel snapshot applied) is exercised via the in-app verification at 8.3 instead

## 4. Recipe wizard UI

- [ ] 4.1 Drink-type template table (profile filter set, bag kind, block pre-seeds, details field list, tea default temps) as a QML/C++ single source
- [ ] 4.2 Wizard page skeleton: step host, breadcrumb chips, auto-advance picker steps, StackView registration, CMakeLists entries for all new QML files
- [ ] 4.3 Drink-type step (6 tiles, SVG icons added to resources)
- [ ] 4.4 Bean step: kind-filtered open bags + "No bean" row
- [ ] 4.5 Profile step: tiered ranked list with headers and reason chips, search within filter set, "Just hot water" fixed row for tea
- [ ] 4.6 Details step: per-type field sets; prefill priority (history → bag brewing data → profile defaults); portafilter-tea temp-correction rule; never overwrite user edits
- [ ] 4.7 Summary step: name + component rows, row-tap → step → return, add/remove milk/water block affordances, per-drink-type equipment row, save path (create + update)
- [ ] 4.8 Name auto-suggestion ("<Bean> <DrinkType>", user-edit wins) — port composer's `_autoName` guard
- [ ] 4.9 Entry-point rerouting: RecipesPage add/edit/clone, promote buttons (ShotHistoryPage, ShotDetailPage, AutoFavoritesPage) → wizard summary with prefill/derived drink type; delete `RecipeComposerPage.qml` (file + CMakeLists)
- [ ] 4.10 `RecipesItem` pill drink-type icons (SVG, derived fallback for legacy rows)
- [ ] 4.11 Accessibility pass on all new pages (roles, names, focus order, AccessibleButton/AccessibleMouseArea) and translations for every new string

## 5. Tea bags

- [ ] 5.1 BeanInfoPage header: "Add Coffee" (primary) + "Add Tea" (secondary); narrow-width behavior
- [ ] 5.2 ChangeBeansDialog tea mode: suppress Visualizer lane, past-tea-bags-only search, straight-to-form when none; tea form labels + hidden fields (roast level, grind/rpm, canonical link)
- [ ] 5.3 BagCard + details popup: hide coffee-only rows for tea bags; show teaType/brewing fields
- [ ] 5.4 Unified bean search model kind awareness (wizard bean-step filter)
- [ ] 5.5 Grinder-less packages: relax EquipmentStorage create precondition, display-name fallback to basket, audit `flattenPackage`/QML consumers for invalid grinder; unit test create/read of basket-only package

## 6. Extraction

- [ ] 6.1 Tea extraction system prompt in AIManager, selected by bag kind; parse whitelist for tea keys; °F/boiling→Celsius and g-per-cup→per-100ml normalization asserted in parser unit tests
- [ ] 6.2 Raise fetchPageText cap 20k → 48k
- [ ] 6.3 Spike: confirm Anthropic web_fetch / OpenAI web-search request shapes for the two providers shipped in #1445
- [ ] 6.4 Stage-2 fallback: on emptyPage/blocked, re-issue extraction as a provider web-fetch request (same JSON contract + `imageUrl`); feature-detect per provider; stage-1 error surfaces unchanged when unavailable
- [ ] 6.5 `imageUrl` consumption: bag image download/cache accepts an explicit URL alongside the og:image path
- [ ] 6.6 Verify end-to-end against the three vendor archetypes (Harney stage 1, Yunnan Sourcing stage 1 post-cap-raise, Fortnum & Mason stage 2 text + image)

## 7. MCP and web surfaces

- [ ] 7.1 MCP recipe tools: drinkType on list/get/create/update; profile-less validation; update register stubs in tst_mcpserver_session/protocol + tst_mcptools_* externs; build --target all before pushing
- [ ] 7.2 MCP bag tools: kind exposed (immutable), tea brewing fields per data conventions
- [ ] 7.3 shotserver_recipes: drinkType round-trip + relaxed validation; web /recipes form drink-type field
- [ ] 7.4 shotserver_bags / web bag surfaces: kind on bag payloads

## 8. Docs and finalization

- [ ] 8.1 Update RECIPES.md (wizard, drink_type, profile-less activation), BEAN_BASE.md (two-stage extraction, tea prompt, imageUrl), MCP_SERVER.md (tool field changes), SETTINGS/QML docs only if touched
- [ ] 8.2 Update wiki Manual pages (Recipes, Bag Inventory) for the wizard and Add Tea
- [ ] 8.3 Full test suite + app-log QML TypeError check (clear-warnings bar); Jeff launches the app for manual verification
- [ ] 8.4 Archive this OpenSpec change on the feature branch before merge (/opsx:archive)
