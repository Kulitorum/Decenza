## 1. Investigate seams

- [ ] 1.1 Confirm how `RecipeStorage`, `ShotHistoryStorage`, and `CoffeeBagStorage` resolve their DB path (via `QStandardPaths` vs hard-coded/derived) — determines whether `QStandardPaths` test mode alone isolates them.
- [ ] 1.2 Audit the `MainController` constructor for collaborators that perform network or file I/O on construction (ProfileManager/Visualizer/BeanBase/AIManager/LiveSteamCoach); confirm they are inert headless or note what must be guarded.

## 2. Add test seams to MainController

- [ ] 2.1 If 1.1 shows the storages are `QStandardPaths`-derived, add nothing for isolation; otherwise add a minimal test-only DB-path override seam (smallest change that isolates the temp DB).
- [ ] 2.2 Add optional trailing constructor parameters to inject `RecipeStorage`/`ShotHistoryStorage`/`CoffeeBagStorage`, defaulting to `nullptr` → create-internally (mirrors the existing `profileStorage = nullptr` param).
- [ ] 2.3 Verify the single production call site compiles unchanged and the default path constructs storages exactly as before.

## 3. Stand up the harness

- [ ] 3.1 Create `tests/tst_maincontroller.cpp` with `initTestCase` enabling `QStandardPaths` test mode + a `QTemporaryDir` data dir; construct `MainController` with headless collaborators.
- [ ] 3.2 Register the `tst_maincontroller` target in `tests/CMakeLists.txt` alongside the existing `tst_*` targets.
- [ ] 3.3 Prove the harness end-to-end: one `QTRY_VERIFY` test that editing the active recipe's grind reaches `Settings.dye.dyeGrinderSetting`.

## 4. Cover the recipe-wiring contract

- [ ] 4.1 Active-recipe grind edit pushes to the dial; inactive-recipe edit does not.
- [ ] 4.2 Startup restore does NOT re-apply grind to the live dial.
- [ ] 4.3 Relink refresh and QML editor-prefill reads do NOT push.
- [ ] 4.4 Grind-less drink type (tea) and empty `grindPinned` leave the dial untouched.
- [ ] 4.5 Recipe switch/deactivate before the re-read lands does not leak a stale grind (flag cleared).
- [ ] 4.6 Dial→recipe stamp echo-guard does not loop and does not drift `m_pendingRecipeSelfWrites` (assert via observable effects / `QSignalSpy`).
- [ ] 4.7 Deactivate-on-ingredient-swap fires on a profile/bag/equipment change to a non-recipe value.
- [ ] 4.8 Recorded limitation: editing the active recipe to zero `rpmPinned` leaves `Settings.dye.dyeGrinderRpm` unchanged (pin current parity behavior).

## 5. Migrate proxy tests

- [ ] 5.1 Enumerate existing recipe tests and classify each assertion: genuine `RecipeStorage`/model unit test (keep) vs proxy for `MainController` wiring (migrate).
- [ ] 5.2 Move proxy assertions into `tst_maincontroller` and delete the proxy tests, ensuring no net coverage loss.

## 6. Verify

- [ ] 6.1 Build all test targets (`--target all`) and run `tst_maincontroller` + the touched recipe tests; confirm green and non-flaky (repeat runs).
- [ ] 6.2 Confirm the production `Decenza` target builds unchanged and the constructor default path is behaviorally identical.
