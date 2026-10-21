## 1. Define the seam

- [x] 1.1 Add a ports header (`recipewiringports.h`): pure-virtual `DialPort`, `RecipeStorePort`, `ProfilePort`, `SideEffectPort` with the minimal methods the wiring needs (see design D3).
- [x] 1.2 Create `RecipeWiringController` skeleton owning `activeRecipe`, `refreshDialFromRecipeEdit`, and the self-write echo-guard counter; take the four ports by reference/pointer in the constructor.
- [x] 1.3 Decide `RecipeSelectionModel`'s relationship (compose vs merge) — default: compose/observe, leave its tests intact.

## 2. Move the logic (verbatim, behavior-preserving)

- [x] 2.1 Move `stampActiveRecipe` (dose/grind/rpm/steam/hot-water) behind `DialPort`/`RecipeStorePort`, preserving the equality guard and self-write counter semantics.
- [x] 2.2 Move the `recipeUpdated` handler (echo-guard decrement; set edit-refresh flag before the re-read) into the controller.
- [x] 2.3 Move the `recipeReady` handler (cache update, steam re-assert via `SideEffectPort`, dial refresh with the Flow-3 flag, deactivate-if-archived) into the controller.
- [x] 2.4 Move the deactivate-on-ingredient-swap decisions (`activeBagIdChanged`/`activeEquipmentIdChanged`/`currentProfileChanged`) into the controller via `onIngredientChanged`.
- [x] 2.5 Move `activate`/`deactivate`/same-id-reactivation coordination; keep heavy activation side-effects (profile load, dose/yield/temp, steam) in `MainController` behind `SideEffectPort`.

## 3. Reduce MainController to an adapter

- [x] 3.1 Replace the inline lambdas in `setupRecipeConnections()` with thin forwarders to the controller's methods.
- [x] 3.2 Implement the four ports on `MainController` over the real `RecipeStorage`/`SettingsDye`/`ProfileManager`.
- [x] 3.3 Delegate the `activeRecipe` Q_PROPERTY to the controller; remove the moved members from `MainController`.

## 4. Test the controller

- [x] 4.1 Add `tests/tst_recipewiringcontroller.cpp` + target in `tests/CMakeLists.txt` (links only the controller + value types + fakes — no app graph).
- [x] 4.2 Implement fake ports (a fake store that invokes callbacks synchronously/on-demand so callback ordering is controllable).
- [x] 4.3 Cover the contract: active-edit→dial; inactive no-push; non-edit reads (startup/relink/prefill) no-push; grind-less/empty no-push; flag cleared on switch/deactivate; echo-guard no-loop/no-drift; deactivate-on-swap; rpm-clear parity limitation.
- [x] 4.4 Add an ordering test per known past bug (Flow-3 refresh; Bug B2 re-read-vs-write-through).

## 5. Verify

- [x] 5.1 Build via Qt Creator; run `tst_recipewiringcontroller` + the existing recipe/settings tests — all green.
- [x] 5.2 Manual pass in the running app: activate → edit grind → Shot Plan refreshes without re-selecting; startup does not re-apply; deactivate-on-swap still fires; heater hold still follows milk recipes.
- [x] 5.3 Confirm no user-observable behavior change (behavior-preserving extraction).

## 6. Close out

- [x] 6.1 Remove the superseded `add-maincontroller-test-harness` change.
- [x] 6.2 Archive this change (`/opsx:archive`) as the last commit on the branch.
