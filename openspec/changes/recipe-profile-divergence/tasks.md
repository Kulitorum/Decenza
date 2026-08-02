## 1. Establish the predicate, with tests that can fail

- [x] 1.1 Write the pure predicate — given the recipe's `profileTitle` and the loaded profile's title, does the recipe own the loaded profile? Empty (after trim) recipe title = owns nothing = never a mismatch. Free function or static, no `this`, so it is reachable from a test (no test in the suite constructs a `MainController`)
- [x] 1.2 Settle the comparison semantics against the existing code rather than inventing them: `maincontroller.cpp:1211` and `:1237` use `trimmed().isEmpty()`, the watcher at `:1288` compares raw. Pick one, and say in a comment which sites it replaced
- [x] 1.3 Add slots to an existing test file (NOT a new one — a new `tst_*.cpp` costs ~1.4 s of build forever, a new slot costs milliseconds) covering: match, mismatch, empty recipe title with a profile loaded, both empty, and whitespace-only recipe title
- [x] 1.4 Break the predicate and watch each new slot go red. A test that cannot fail is a comment that compiles

## 2. Route the existing watcher through it

- [x] 2.1 Replace the raw comparison in the `currentProfileChanged` watcher (`src/controllers/maincontroller.cpp:1285-1291`) with the predicate. This is where the ownership gate arrives — a profile-less recipe stops being deactivated by a profile change
- [x] 2.2 Fix the comment above the watcher block (`:1260-1264`), which already claims "a recipe without that rung doesn't own the choice" — true of the bag and equipment watchers, false of the profile watcher until now. Say that it is now true of all three

## 3. Close the startup hole

- [x] 3.1 In the `recipeReady` handler (`:1182`), after the row is confirmed non-empty and non-archived, consult the predicate and `deactivateRecipe()` on a mismatch — the same branch shape as the empty/archived case at `:1191-1195`
- [x] 3.2 Confirm placement: this must run BEFORE `m_activeRecipe = recipe` and the `setActiveRecipe` dose claim (`:1199-1213`), or a recipe about to be dropped first arms the dose ladder
- [x] 3.3 Verify the ordering claim the fix rests on — that the startup profile load completes inside `new ProfileManager(...)` (`maincontroller.cpp:130`; the load is inline in the constructor, `m_startupLoadDone` at `profilemanager.cpp:366`) and therefore always before `requestRecipe` at `:1388` can call back. If that is ever not true, this reconcile fires against an unloaded profile and wrongly deactivates
- [x] 3.4 Confirm the reconcile cannot fight an activation in flight: activation returns via `recipeActivationReady`, not `recipeReady` (stated at `:1204-1206`) — verify that is still the case rather than trusting the comment

## 4. Make deletion reconcile

- [x] 4.1 Add a deletion signal to `ProfileManager` carrying the deleted profile's TITLE (recipes store the title, not the filename — `deleteProfile` takes a filename, so resolve it before the file is gone)
- [x] 4.2 Emit it from the success branch of `deleteProfile` (`profilemanager.cpp:1142-1163`), beside the existing favorites and auto-load eager-clears — the same class of "this pointer is now dangling" cleanup
- [x] 4.3 Connect it in `MainController` beside the other deactivation watchers, consulting the same predicate against `m_activeRecipe`
- [x] 4.4 Decide and record what happens for a BUILT-IN profile, where `deleteProfile` removes only a local override and returns `false` (`:1134-1140`) — the title still resolves, to the built-in version, so this must NOT deactivate. Confirm the signal is not emitted on that path
- [x] 4.5 Add a slot to `tests/tst_profilemanager.cpp` (it already constructs a `ProfileManager` and already manipulates `dye/activeRecipeId`) asserting the signal fires with the right title on a user-profile delete and does not fire for a built-in override cleanup

## 5. Warn before deleting a profile recipes name

- [x] 5.1 Add a count-recipes-by-profile-title query to `RecipeStorage`. Inline on the main thread is correct here — a discrete user action against a table with tens of rows — and per CLAUDE.md put the measured median AND worst case in a comment at the call site rather than the conclusion
- [x] 5.2 Confirm the count's title matching agrees with activation's. `applyActivatedRecipe` resolves through `findProfileByTitle` (exact, case-SENSITIVE, `profilemanager.cpp:1014`) while `RecipeStorage` matches recipes with `LOWER(...)` (`recipestorage.cpp:1030`, `:1624`). If those disagree the warning counts recipes that would still activate, or misses ones that would not — settle it and say which is right
- [x] 5.3 Warn in the delete confirmation with the count, and proceed on confirm. Never refuse the delete
- [x] 5.4 Show no warning when the count is zero, and none for the built-in-override cleanup path (`profilemanager.cpp:1134-1140`), where the title still resolves afterwards
- [x] 5.5 Do NOT list the affected recipes or offer to repair them — that invites the repair flow this change deliberately does not build

## 6. Show a missing profile in the recipe list

- [x] 6.1 Mark a recipe whose `profileTitle` does not resolve, in `RecipeDrinkCard` (`qml/components/RecipeDrinkCard.qml:87-88` prints the title today with no idea whether it resolves). Derived at display time — nothing stored, nothing scanned, no migration
- [x] 6.2 Make the marking FOLLOW THE CATALOG. `findProfileByTitle` is a `Q_INVOKABLE`, so a binding calling it records no dependency and will never re-evaluate when a profile is deleted or imported — the `translate` failure in CLAUDE.md, silent and runtime-only. Depend on the catalog-changed signal, or expose reachability as a property
- [x] 6.3 Leave profile-less recipes (empty `profileTitle`, hot-water block) unmarked — reuse the section-1 predicate's ownership rule rather than writing a second empty-title test
- [x] 6.4 Check the other recipe-listing surfaces (`RecipesPage`, the quick-switch pills, the ShotServer `/recipes` page) and decide per surface whether the marking belongs there. Per CLAUDE.md the web surface should not silently drift from the app; add a task rather than leaving it implicit
- [x] 6.5 Follow `EMOJI_SYSTEM.md` and `ACCESSIBILITY.md` for whatever the marking is — it must not be carried by colour alone, and it needs an accessible name

## 7. Make a failed activation visible

- [x] 7.1 Surface `recipeActivated(id, false)`. The signal exists and already carries the failure; no QML handles it (the two references in `qml/` are `BrewDialog` re-seeding on success), which is why the pill reverting is all the user sees
- [x] 7.2 Name the missing profile title in the message — that is the value the user must change in the editor. Requires the reason to reach QML; today `applyActivatedRecipe` only `qWarning`s it (`:1569`)
- [x] 7.3 Cover the non-profile failure too (`:1541`, recipe row not found), which fails just as silently today
- [x] 7.4 Internationalize per CLAUDE.md: `TranslationManager.translate` / `Tr`, reusing existing keys where they fit

## 8. Verify

- [x] 8.1 Run the full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask before building, Qt Creator is shared
- [ ] 8.2 Manual: activate a recipe, delete its profile, confirm the warning names the right count, the pill deselects, and the recipe shows as missing its profile
- [ ] 8.3 Manual: activate a recipe, quit, delete its profile file on disk, relaunch, confirm the recipe is not active and the next shot carries no recipe id. This is the exact path that produced the 2026-07-28 shots
- [x] 8.4 Manual: recipe 43 in the development database is ALREADY in the broken state (`A-Flow / ZZ Fmt Test aflow`, no stored JSON). Confirm it now reads as missing its profile, and that tapping it says so instead of silently reverting
- [x] 8.5 Manual: re-import a deleted profile and confirm the marking clears with the list open — this is what catches the `Q_INVOKABLE` dependency trap in 6.2, which no build or test will
- [ ] 8.6 Manual: re-point a marked recipe at an installed profile in the recipe editor and confirm it activates
- [ ] 8.7 Manual: activate a profile-less (tea / hot-water) recipe, change the profile, confirm it stays active and is not marked — the behaviour change users could notice
- [ ] 8.8 Manual: edit the loaded profile in place (same title) with a recipe naming it active, confirm the recipe stays active
- [ ] 8.9 Manual: open the recipe list on a cold launch and confirm no recipe flashes as missing its profile before the catalog loads (the loud-and-wrong failure mode from the design)
- [ ] 8.10 Read the `text-invariants.yml` PR run before merging — it gates `src/**` and nothing blocks a merge on it

## 9. Document and land

- [x] 9.1 Note the rules in `docs/CLAUDE_MD/RECIPES.md`: a recipe is deactivated whenever its profile stops being the loaded one by any route; a profile-less recipe owns no profile choice; a missing profile is a derived display state, never stored, and is deliberately not repaired by snapshotting
- [x] 9.2 NOT DOING: no wiki entry. A confirmation dialog that states its own consequence, and a card that says its profile is missing, are self-documenting — a manual page restating them is overkill and one more thing to drift. CLAUDE.md's "update the wiki for user-visible changes" is about features a user has to be told exist, not about warnings they read at the moment they matter
- [x] 9.3 If 6.4 put the marking on the ShotServer `/recipes` page, keep the two surfaces in sync in this same change
- [x] 9.4 Open the PR, then run the automated `/pr-review-toolkit:review-pr` before merging
- [ ] 9.5 Archive the change + spec sync as the final commit on the same PR

## 10. Review round (5 agents, 2026-08-02)

- [x] 10.1 `profileMissing` ignored the recipe's frozen `profileJson`. Activation's refusal is a THREE-term test, so a recipe carrying stored JSON activates fine — and was being marked broken on both surfaces, on exactly the device-transfer/import route the fallback exists for. Following the advice would have destroyed the profile the recipe carried
- [x] 10.2 `profileDeleted` fired on "was a user file", not "did the title stop resolving". `refreshProfiles()` reclassifies a shadowing file as UserCreated, so deleting one emitted and deactivated a recipe whose title still resolved. Gated on `findProfileByTitle(...).isEmpty()` after the rebuild
- [x] 10.3 The old test for that passed for the WRONG reason — it wrote the override to `userProfilesPath()`, which `refreshProfiles()` skips, so the catalog row stayed BuiltIn and the early return fired. Replaced with one that shadows a built-in TITLE from a differently-named file
- [x] 10.4 Trim asymmetry: the predicates and the count trimmed, `findProfileByTitle` does not. A stray-space title read as fine everywhere and then failed to activate. `namesProfile` now compares raw; `ownsProfileChoice` still trims, and that asymmetry has its own assertion
- [x] 10.5 The count returned 0 on failure — indistinguishable from "no recipes", and the dialog hides at 0. Now `kRecipeCountUnknown` with a distinct "couldn't check" message, and `withTempDb`'s discarded open-failure handled
- [x] 10.6 `activateRecipe`'s no-storage branch emitted `recipeActivated(false)` with no paired failure signal, keeping the silent pill this change exists to remove
- [x] 10.7 MCP `recipe_activate` and web `/activate` still returned generic strings. Both now latch the reason and name the profile; web returns 409 (the recipe exists, its profile does not)
- [x] 10.8 The profile-delete deactivation logged nothing while its sibling did — invisible in a submitted log
- [x] 10.9 `deleteProfile` mis-diagnoses a filename missing from the catalog as a built-in override cleanup; now warned
- [x] 10.10 Covered `countRecipesUsingProfileStatic` — case-sensitivity, archived exclusion, empty-title short-circuit. Three agents flagged it independently
- [x] 10.11 Justified `profileOwnershipGate` rather than deleting it: it is the only place the trim asymmetry is asserted directly
- [x] 10.12 Corrected "accompanies every `recipeActivated(id, false)`" — it did not, and now names all three sites plus the emit-ordering the MCP/web latches depend on
- [x] 10.13 Corrected "requested at the end of THIS constructor" — it is the end of `setupRecipeConnections()`, called ~40% through a constructor spanning 114-789
- [ ] 10.14 The PR body claims fault injection proves the three predicate slots each catch a distinct shape. It does not: breaking `ownsProfileChoice` reddens all three because they share it. Reword to claim only what it shows
