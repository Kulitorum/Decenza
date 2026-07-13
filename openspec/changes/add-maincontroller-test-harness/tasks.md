## 1. Investigate seams — DONE (spike)

- [x] 1.1 DB path resolution: `MainController` calls `ShotHistoryStorage::initialize()` with no arg → defaults to `QStandardPaths::writableLocation(AppDataLocation)/shots.db`; all other storages take `m_shotHistory->databasePath()`. **`QStandardPaths::setTestModeEnabled(true)` isolates all four — zero production seam needed.**
- [x] 1.2 Constructor inertness: ShotServer only `start()`s if `shotServerEnabled()` (**defaults false**); MqttClient `connectToBroker()` is guarded (off by default); ProfileManager ctor only wires signals + arms single-shot timers (no disk scan); network clients are pointer-storing (auto-fired requests fail harmlessly). **Construction is inert headless.**
- [x] 1.3 **NEW finding — the real cost is the build graph.** `MainController`'s constructor unconditionally `new`s every collaborator (ProfileManager, Visualizer, BeanBase, AIManager, LiveSteamCoach, ShotServer ×12 split files, MqttClient, all storages, models, UpdateChecker, …), so the test target must link ~150 of the app's 192 sources. No production seam avoids this; it is inherent to the god-object constructor.

## 2. No production changes required

- [x] 2.1 DB isolation needs no seam (1.1). Constructor injection is unnecessary — a test constructs `MainController` under test mode and pre-seeds recipes by inserting into the same temp DB via `RecipeStorage::insertRecipeStatic`. **This change is test-only + build wiring; production C++ is untouched.**

## 3. Stand up the harness

- [ ] 3.1 Wire the build target. Prefer the **curated-source-list** approach (list the ~150 app sources minus `main.cpp`/QML in `add_decenza_test(tst_maincontroller ...)`) — it touches zero app-build config, so the user's Qt Creator app build is unaffected. Fall back to a `Decenza_core` OBJECT library only if the curated list proves unmaintainable. Derive the list from the root `SOURCES` variable.
- [ ] 3.2 Create `tests/tst_maincontroller.cpp` with `initTestCase` enabling `QStandardPaths` test mode + a clean temp data dir; construct `MainController` with headless collaborators (real inert `Settings`/`DE1Device`/`MachineState`/`ShotDataModel`/`QNetworkAccessManager`, per existing headless tests).
- [ ] 3.3 Prove the harness end-to-end: one `QTRY_VERIFY` test that editing the active recipe's grind reaches `Settings.dye.dyeGrinderSetting`. Get this green before writing the rest.

## 4. Cover the recipe-wiring contract

- [ ] 4.1 Active-recipe grind edit pushes to the dial; inactive-recipe edit does not.
- [ ] 4.2 Startup restore does NOT re-apply grind to the live dial.
- [ ] 4.3 Relink refresh and QML editor-prefill reads do NOT push.
- [ ] 4.4 Grind-less drink type (tea) and empty `grindPinned` leave the dial untouched.
- [ ] 4.5 Recipe switch/deactivate before the re-read lands does not leak a stale grind (flag cleared).
- [ ] 4.6 Dial→recipe stamp echo-guard does not loop and does not drift `m_pendingRecipeSelfWrites` (assert via observable effects / `QSignalSpy`).
- [ ] 4.7 Deactivate-on-ingredient-swap fires on a profile/bag/equipment change to a non-recipe value.
- [ ] 4.8 Recorded limitation: editing the active recipe to zero `rpmPinned` leaves `Settings.dye.dyeGrinderRpm` unchanged (pin current parity behavior).

## 5. Proxy-test audit — DONE (no proxies)

- [x] 5.1 Audited every recipe test (`tst_recipeselectionmodel`, `tst_recipestorage`, `tst_recipegenerator`, `tst_recipeparams`, `tst_recipepromotion`). Grep for `activateRecipe`/`deactivateRecipe`/`stampActiveRecipe`/`dyeGrinderSetting`/`m_activeRecipe`/`new MainController` returned **zero** matches — every test is a genuine `RecipeStorage`/`RecipeSelectionModel`/generator unit test. **No proxies to migrate; coverage is 100% net-new.**

## 6. Verify

- [ ] 6.1 Build all test targets (`--target all`) and run `tst_maincontroller` + the touched recipe tests; confirm green and non-flaky (repeat runs).
- [ ] 6.2 Confirm the production `Decenza` target builds unchanged and the constructor default path is behaviorally identical.
