## 1. Data model & migration

Migration 34 (latest is 33, `shothistorystorage.cpp:1622`) covers three tables in one step. Every backfill reproduces today's behaviour exactly.

- [x] 1.1 Add `yieldValue` (double) + `yieldMode` (enum/QString: `none`/`absolute`/`ratio`) to `Recipe` in `src/history/recipestorage.h`, replacing `yieldG`'s role; keep `yieldG` readable for the migration only
- [x] 1.2 Add `yieldValue` + `yieldMode` to `CoffeeBag` in `src/history/coffeebagstorage.h`, replacing `yieldOverrideG`'s role
- [x] 1.3 Add `yield_value`/`yield_mode` rows to `kCols` in `recipestorage.cpp:100-121` and `coffeebagstorage.cpp:130-160`; plain `COL_DBL` is correct for `yield_value` (a ratio is strictly positive, so `nullIfZero` is safe — no `COL_DBL_SIGNED` as `tempOffsetC` needed)
- [x] 1.4 Add both columns to `ensureTableStatic` CREATE TABLE in `recipestorage.cpp:713-737` and the `coffee_bags` equivalent
- [x] 1.5 Write migration 34 in `src/history/shothistorystorage.cpp`: `ALTER TABLE recipes ADD COLUMN yield_value REAL, yield_mode TEXT`; backfill `yield_mode='absolute', yield_value=yield_g` where `yield_g > 0`, else `yield_mode='none'`. Leave `yield_g` dead in place (the `temp_override_c` precedent)
- [x] 1.6 Same migration: `coffee_bags` gains `yield_value`/`yield_mode`; backfill from `yield_override_g` (>0 → `absolute`, else `none`). Leave `yield_override_g` dead in place
- [x] 1.7 Same migration: `shots` gains `yield_mode TEXT`, `yield_anchor_value REAL`; backfill `yield_mode='absolute', yield_anchor_value=yield_override` where `yield_override > 0`, else `'none'`. Do NOT touch `yield_override` — it stays the resolved-grams column every detector reads
- [x] 1.8 Add `yieldMode` to the local-only set in `CoffeeBagStorage::touchesVisualizerFields` (`coffeebagstorage.cpp:652-661`) so an anchor edit never triggers a Visualizer PATCH
- [x] 1.9 Extend the device-transfer/backup-restore column lists + source-index resolution for all three tables. `recipestorage.cpp:1365-1390` already substitutes `NULL AS <col>` for columns missing from an older source; confirm the `coffee_bags` and `shots` import paths do the same or add it
- [x] 1.10 Migration tests: an existing DB gains all six columns with correct backfill; a source DB predating the migration imports cleanly with defaults and no per-row warning
- [x] 1.11 **Merge check:** `bean-freshness-followup` also modifies `coffee-bag-model`'s "CoffeeBag data model" requirement (adding `storageHint`/`openedDate`). Whichever archives second must fold in the other's fields rather than clobber them

## 2. The YieldSpec type & resolution

- [x] 2.1 Add the session anchor to `SettingsBrew`: value + mode, QSettings keys (`espresso/brewYieldMode` alongside the existing value key). Absent mode = `absolute` when `hasBrewYieldOverride`, else `none`. No migration step (QSettings, not SQL)
- [x] 2.2 Redefine `hasBrewYieldOverride` as `mode != none`
- [x] 2.3 Rewrite `ProfileManager::targetWeight()` (`profilemanager.cpp:390-395`) as `resolve(spec, dose)` — the ladder's single evaluation point. It must keep returning plain grams
- [x] 2.4 Rewrite `ProfileManager::brewByRatioActive()` (`:397-401`) to read the stored mode instead of `qAbs(override − profileTarget) > 0.1`. The name finally matches its meaning
- [x] 2.5 Rewrite `brewByRatio()` (`:407-411`) to return the stored ratio when mode is `ratio`, rather than deriving `override ÷ dose`
- [x] 2.6 Retire the Bug-A float-compares. There are **five**, not four: `profilemanager.cpp:429`, `:478` (startup-only — see 2.7), `:400` (inside `brewByRatioActive`), `maincontroller.cpp:1373`, and `maincontroller.cpp:788-789` (shot restore — `qAbs(shotRecord.targetWeight - currentProfile().targetWeight()) > 0.1`, see 7.6). The "matching the profile default is not an override" rule survives for `absolute` only
- [x] 2.6a `CoffeeBagStorage::yieldOverrideForTarget` (`coffeebagstorage.cpp:670`) can only be deleted **after** both of its callers go — `profilemanager.cpp:444` (task 3.7) and `maincontroller.cpp:3202` (task 3.8). It has three callers today, not zero; deleting it first will not compile
- [x] 2.7 Make a profile load mode-aware: clear `absolute`, keep `ratio`; temperature still clears unconditionally. **The obvious edit lands in dead code** — `resetBrewOverridesForLoadedProfile()` (`profilemanager.cpp:466-471`) early-returns `clearAllBrewOverrides()` when `m_startupLoadDone`, so the visible float-compare tail runs only at startup and every user profile switch takes the early return
- [x] 2.7a Split `clearAllBrewOverrides()` (`settings_brew.cpp:937`) into "clear everything" vs "clear what this profile owns". Its five callers disagree: `profilemanager.cpp:460` (explicit user Clear — must clear the ratio), `:471` (profile-load reset — must keep it), `:2205`/`:2456` (profile edit / new profile — must clear it, or a kept ratio leaks into a freshly edited profile)
- [x] 2.8 Audit the eight `targetWeightChanged` emit sites (`profilemanager.cpp:335/338/419/1280/1333/2221/2379/2466`) and their hand-paired `MachineState::setTargetWeight` calls. `:338` (`dyeBeanWeightChanged`) is the one that must now re-derive rather than notify-only. Note `:1266`/`:1319` pass the resolved value while `:2210`/`:2372`/`:2459` pass the raw profile target — that inconsistency is already load-bearing on clear-ordering; do not "tidy" it blind
- [x] 2.9 Add `resolve()` unit tests: each mode; `ratio` with a zero/unset dose; `none` falling through the ladder; a ratio deriving exactly the profile target still reporting as anchored

## 3. Ladder & activation

- [x] 3.1 Implement the ladder explicitly: recipe (mode != none) → bag (mode != none) → profile `target_weight`
- [x] 3.2 Apply the recipe's spec verbatim (value + mode) in `MainController::applyActivatedRecipe` (`maincontroller.cpp:1371-1375`)
- [x] 3.3 **Fix the write order.** Yield is written synchronously at `:1371` but the dose is queued at `:1356` (deliberately, so it beats `loadProfile`'s deferred `recommendedDose`). Derive the ratio inside the queued step, or resolve against `recipe["doseG"]` directly — never read the dose back from settings at `:1371`
- [x] 3.4 Remove the Bug-A guard from the ratio path at `:1373` (see 2.6): a ratio deriving the profile default must stay anchored
- [x] 3.5 **Guard the bag-anchor apply** at `maincontroller.cpp:257-262` on "no recipe active". Today it is unguarded and lands after `activeBagIdChanged`'s clear. Note the trigger is **not** a manual bean switch — `recipe-activation` already deactivates the recipe on one (`openspec/specs/recipe-activation/spec.md:57`); the live hazard is recipe activation selecting *its own* linked bag via `applyActiveBag`, which re-arms the bag's anchor over the recipe's
- [ ] 3.6 Tests: recipe outranks bag on recipe-driven bag selection; bag answers with no recipe; ratio resolves against the recipe's dose not the previous one
  - **Not automated — no harness reaches it.** `McpTestFixture` builds a `ProfileManager`, not a `MainController`, so activation's ladder has no unit-test seam. All three arms were verified LIVE via MCP instead (recipe 40 g beat bag 42 g; a yield-less recipe fell through to the bag's 42 g; a 1:2.5 recipe resolved 45 g against its own dose). Closing this properly needs a MainController test fixture — a separate piece of work.

### Removing the bag's auto-writers (without these, "Update Bag" is a no-op)

- [x] 3.7 Remove the `persistYieldOverrideToBag` call from `ProfileManager::activateBrewWithOverrides` (`profilemanager.cpp:444`) — the Brew Settings OK write
- [x] 3.8 **Remove the yield half of the shot-save bag stamp** (`maincontroller.cpp:3193-3207`). It writes `yieldOverrideG` via `yieldOverrideForTarget` on **every saved shot** — a second, unconditional auto-writer that is easy to miss and that alone would keep the bean silently learning its yield. Keep the `doseWeightG` and `lastUsedEpoch` halves of the same stamp untouched (dial-in stays automatic)
- [x] 3.9 Wire `persistYieldOverrideToBag` (`settings_dye.cpp:727`) to the "Update Bag" button instead, writing value + mode
- [x] 3.10 Tests: a saved shot does not change the bag's yield spec; Brew Settings OK does not change it; "Update Bag" does; a saved shot still stamps `doseWeightG`

## 4. BrewDialog

- [x] 4.1 `:147` seed — `targetManuallySet` becomes "is the active anchor's mode `absolute`?", not `Settings.brew.hasBrewYieldOverride`. This is the single line where the persisted mode enters the dialog
- [x] 4.2 `:76` `recipeYieldBaseline` — becomes a type-aware anchor pair: the stored value in its own unit, the other derived through the dose (see the table in `recipe-aware-brew-settings`)
- [x] 4.3 `:205-208` dose watcher — drop `targetManuallySet = false`; keep the `doseValue` write. A capture must not flip the anchor
- [x] 4.4 `:395` Clear — restore the active store's spec, whichever mode; today it restores `activeRecipe.yieldG` and would restore nothing for a ratio recipe
- [x] 4.5 `:1087-1109` — collapse the Stop-at row's button into a single yield/ratio persist button bound to the anchored row; label by ladder (*Update Recipe* / *Update Bag* / hidden). Writes value + mode. The Temp Delta button at `:748` is untouched
- [x] 4.6 Unify the override tolerances (`:1006` ratio `> 0.05` vs `:1061` grams `> 0.1`) into one unit converted through the dose, so the two rows cannot disagree
- [x] 4.7 `:449` — `lastUsedRatio` write becomes preset memory only, not an authority
- [ ] 4.8 Tests: button sits on the anchored row; moves on editing the other; hidden at mode `none`; hidden with no recipe and no bag; a mode-only change enables it; a dose capture moves neither the anchor nor the button
  - **Not automated — no QML test harness in this repo.** Verified live on screen: the button sat on the Stop-at row labelled *Update Bag* and greyed at baseline; editing the Ratio row moved it there and enabled it; pressing it cleared the highlight and re-greyed. NOTE: a missing `Q_PROPERTY` on `activeBagYieldValue`/`Mode` made the enable-gate read `undefined` and never grey — a bug no unit test could have caught, found only by reading the running app's log.
- [x] 4.9 **Unify the ratio bounds and enforce them in C++** (design.md Open Question 5). Today: `qBound(0.5, r, 6.0)` in `setRatioPreset1/2/3` (`settings_brew.cpp:151/162/173`), `0.5–20.0` on `ratioInput` (`BrewDialog.qml:1009`), `1–500` on `targetInput` (`:1064`), **no clamp at all** in `setBrewYieldOverride` (`settings_brew.cpp:909-931`) or `RecipeWizardPage.qml:1383-1389`. A stored ratio re-deriving on every dose change makes this live — ratio 20 × dose 50 = 1000 g, double the stop-at ceiling, into an unclamped setter
- [x] 4.10 **Guard the zero dose.** `mcptools_control.cpp:97-101` (`machine_start_espresso`) defaults a missing `dose` argument to **0** and passes it straight to `activateBrewWithOverrides` → `setDyeBeanWeight(0)`, unguarded (`settings_dye.cpp:465`). Today that only mislabels the shot record; under a ratio anchor it resolves to a **0 g stop target** on a shot the user just started. Decide `resolve()`'s contract for a zero/unset dose and enforce it at this entry point (`mcptools_control.cpp` is otherwise not in the Impact list — add it)
- [x] 4.11 Pick one canonical "effective dose" accessor. Eight sites re-derive the zero guard with three different answers: `brewByRatio()` returns 0.0 (`profilemanager.cpp:409`), `activateBrewWithOverrides` substitutes 2.0 (`:448`), `RatioPresetDialog.qml:42` and `RatioQuickSelectItem.qml:22` substitute 18.0, `BrewDialog.qml:146/397/400` fall back to `lastUsedRatio`, and `brewByRatioDose()` (`:403-405`) returns `dyeBeanWeight()` raw. The same zero-dose state renders `0.0` in one widget and `1:2.0` in another

## 5. The `yieldG == 0` sweep (highest regression risk)

A ratio-anchored recipe has no absolute yield. Every `yieldG > 0 ? yieldG : profileYield` fallthrough silently substitutes the profile's target as the baseline, which reintroduces the spurious amber `"36.0 → 40.0g"` arrow that #1485 fixed. One test per site.

- [x] 5.1 `MainController::activeBaselineYieldG` (`maincontroller.cpp:1557-1570`) — resolve through the ladder, mode-aware
- [x] 5.2 `MainController::yieldIsRealOverride` (`:1572`) — compare like with like (ratio vs stored ratio, grams vs stored grams)
- [x] 5.3 `BrewDialog.qml:76` `recipeYieldBaseline` (covered by 4.2 — verify)
- [x] 5.4 `BrewDialog.qml:395` Clear (covered by 4.4 — verify)
- [x] 5.5 `ShotPlanItem.qml:40-42` `recipeBaselineYield`
- [x] 5.6 `RecipeDrinkCard.qml:240` `targetWeight`
- [x] 5.7 `CustomItem.qml:54-55` — **not** an independent fallthrough: it just reads `MainController.yieldIsRealOverride`, so 5.2 fixes it. Verify only, no edit expected
- [ ] 5.8 Regression test: a ratio recipe sitting at its designed yield renders a plain target with **no** arrow and **no** highlight, on the live Shot Plan and on its card
  - **Not automated — no QML test harness in this repo.** Verified live on screen: a bean anchored at 42 g rendered `Brew 42.0g` plain white (it rendered an amber `36.0 → 42.0g` before the ladder fix), and pressing Update Bag cleared the highlight on both the Shot Plan and Brew Settings simultaneously.

## 6. Display & editors

- [x] 6.1 `ShotPlanText.qml:120-130` `_yieldStr` — hard-codes `toFixed(1) + "g"`. Add the anchor mark; a ratio recipe with no dose renders a bare `1:2` (nothing to multiply, and no fallback to the profile's target)
- [x] 6.2 `ShotPlanText.qml:111-112` `_yieldOverride` — the highlight rule, mode-aware
- [x] 6.3 `RecipeWizardPage.qml:2186` — `yieldField` becomes the three-state control (nothing / yield / ratio) with the last-written anchor rule; the non-anchor field shows derived and dimmed, never blank. Save/load at `:214`, `:291`, `:564`
- [x] 6.4 `ChangeBeansDialog.qml` — the same three-state control for the bag
- [x] 6.5 `RecipeDrinkCard.qml`, `BagCard.qml` — anchor mark; `18.0g → 36.0g` + `1:2` when the dose is known, bare `1:2` when it is not
- [x] 6.6 `RatioPresetDialog.qml:56-68` `applyRatio` — write a ratio anchor instead of `brewYieldOverride = dose * r`; stop being the only writer of a flattened ratio
- [x] 6.7 `RatioQuickSelectItem.qml` — show override state against the active anchor (the layout override-highlight mechanism already exists); drop the "lastUsedRatio can diverge" apology comment once it is true
- [x] 6.8 `ShotDetailPage.qml:171-176` `formatRatio` — show the target ratio alongside the achieved one (`1:2.06 (target 1:2)`), now that the shot records its anchor
- [x] 6.9 Reconcile the dial-in cards: `ShotDetailPage.qml:192` prefers the **target**, `PostShotReviewPage.qml:118-123` prefers the **achieved**. Same-looking card, two authorities — pick one
- [x] 6.10 `IdlePage.qml:248-249` — keep `dyeBeanWeight = net`; replace `brewYieldOverride = net * lastUsedRatio` with anchor-aware re-derivation (the active anchor's own ratio, never the global preset)

## 7. Shots, promotion & restore

- [x] 7.1 Add `yieldMode`/`yieldAnchorValue` to `ShotRecord` (`src/history/shothistory_types.h`) and `ShotProjection`; extend the shot-save INSERT, the read SELECT, and the device-transfer INSERT
- [x] 7.2 `maincontroller.cpp:3036-3041` — keep resolving to grams for `yield_override` (unchanged; this is what keeps every detector, MQTT, and DYE untouched) and additionally record the anchor. Do not introduce a sentinel colliding with the existing `0 = unknown`
- [x] 7.3 `recipepromotion.cpp:48-49` — `yieldG` becomes a copy of the shot's anchor (value + mode). `doseG` stays the shot's achieved/corrected dose. Do **not** derive the ratio from `target ÷ dose` — `dose_weight` is post-shot editable (`PostShotReviewPage.qml:1483`) and a corrected dose would mint a ratio nobody chose
- [x] 7.4 Verify `shotanalysis.cpp` needs **no** change: `:346` and `:575` consume `finalWeightG / targetWeightG` in grams, which resolution preserves
- [x] 7.5 Verify MQTT `target_weight` (`mqttclient.cpp:717`) still publishes grams — never a ratio. It reads `MachineState::targetWeight()` live, **not** the shot column, so resolution upstream of `MachineState` is what keeps it correct; the HA entity is `device_class: weight` with third-party history bound to it
- [x] 7.6 **Open decision (design.md Q2):** shot restore / "brew this again" (`maincontroller.cpp:790-795`, `AutoFavoritesPage`) — restore the anchor, or the frozen grams? Leaning anchor-restore now that the shot remembers it
- [x] 7.7 Tests: a ratio shot records mode+value; a post-shot dose correction leaves the anchor untouched; promotion of a ratio shot yields a ratio recipe; legacy shots promote as absolute exactly as today

## 8. Safety: the dose latch

- [x] 8.1 Latch the dose at `espressoCycleStarted` (`main.cpp:840-902`), alongside the SAW model snapshot already taken at `:849-859`; release at shot end. Event-based flag, never a timer (project convention)
- [x] 8.2 Verify no dose-write path can move the live SAW target mid-shot: QML, MCP (`mcptools_write.cpp:875`), settings import (`settingsserializer.cpp:786`), and the **queued** `setDyeBeanWeight(recommendedDose)` at `profilemanager.cpp:1259` which can land after a shot begins
- [x] 8.3 The forwarder at `main.cpp:953-959` is not phase-gated and `weightprocessor.cpp:358` assigns with no `m_active` check. Its safety comment only reasons about pre-shot callers — either gate it or guarantee the latch makes it moot, and update the comment either way
- [x] 8.4 Test: a mid-shot dose write does not move the stop target; the next shot picks up the new dose
- [x] 8.5 Confirm-and-document that SAW learning needs no change: it learns drip-vs-flow keyed on `(baseProfileName, scaleType)`, stores no absolute target (`drip = m_weight − m_weightAtStop`, `shottimingcontroller.cpp:624`), and `overshoot` uses `m_targetWeightAtStop` captured at trigger time (`:630`). Add the rationale to `SAW_LEARNING.md` so nobody "fixes" the apparent per-profile/per-recipe mismatch

## 9. MCP, web & docs

- [x] 9.1 `mcptools_recipes.cpp` — sparse-emit `yieldG` **or** `yieldRatio` (`:121-122`); add both to `kNumberKeys` (`:211`) and the create/update schemas (`:418-419`, `:518-519`) with descriptions; **reject both-present loudly**, mirroring the `temperatureOverrideC` rejection at `:440-441`/`:538-539` (`:452-455`/`:543-546` are the *drinkType* rejection) and its web twin at `shotserver_recipes.cpp:205-206`/`:362-363`
- [x] 9.2 Document the cross-field semantics in the tool descriptions: sending `yieldRatio` alone clears an absolute (and vice versa). `recipe_update` is present-keys-only, so callers will get this wrong without it
- [x] 9.3 `mcptools_write.cpp:1544`/`:1633`/`:1689` — same treatment for `bag_update`'s `yieldOverrideG`
- [x] 9.4 `shotserver_recipes.cpp:70-71`/`:129-132`/`:789-790` — the web editor gains the anchor control. Note `parseFloat(…) || 0` currently makes a blank yield field POST an explicit clear on every save; that must not silently wipe a ratio
- [x] 9.5 `shotserver_bags.cpp:33` — add the bag's yield spec to the field list
- [x] 9.6 `recipe_clone` copies columns generically — verify the new columns ride along with no work
- [x] 9.7 `docs/CLAUDE_MD/RECIPES.md` — the anchor model; drop yield from "tweaks stamp the active recipe"; document the bag's measurement/intent split
- [x] 9.8 `docs/CLAUDE_MD/RECIPE_PROFILES.md` — the resolve-to-grams boundary
- [x] 9.9 `docs/SHOT_REVIEW.md` — the shot's new anchor provenance columns. While here, fix the stale §4 "Lazy persist on view" text: it says QML calls `requestReanalyzeBadges(id)` from `onShotReady`, but neither page does anymore (`ShotDetailPage.qml:127`, `PostShotReviewPage.qml:323`)
- [x] 9.10 `coffeebagstorage.h:80` — the comment "write-through from edits, stamped on shot save" is **accurate**; the shot-save stamp is `maincontroller.cpp:3193-3207`. Update it to describe the post-change split instead (grind/rpm/dose write through; the yield anchor is button-protected)

## 10. Wiki manual

The end-user manual lives at https://github.com/Kulitorum/Decenza/wiki/Manual (separate repo, `Kulitorum/Decenza.wiki.git` — clone it to edit). This change is user-visible four separate ways, three of them changes to behaviour people rely on today. Per CLAUDE.md a shipped feature with no manual entry is incomplete — these are part of the work, not an afterthought.

- [x] 10.1 **The new feature** — a recipe, a bean, or the current brew can define its yield as a ratio (1:2) instead of a fixed weight, and the target then follows the dose you actually weighed. Cover the three-state choice (nothing / a yield / a ratio), and that the two are mutually exclusive
- [x] 10.2 **The anchor rule** — whichever of ratio/yield you last set is the one that's kept; the other is shown derived. Explain that the single Update button sits next to whichever one is anchored, and that this is how you switch a recipe between the two
- [x] 10.3 **The ladder** — recipe → bean → profile, and that the bean's yield is the useful one when you're brewing without a recipe. Note that a ratio sticks across a profile change while a fixed weight doesn't, and why
- [x] 10.4 **Behaviour change: absolute yields no longer move after a dose capture.** Today weighing your beans silently rewrites a fixed yield via the last-tapped ratio preset; after this it stays put. This is the change most likely to be noticed and reported as a regression, so it needs to be findable in the manual
- [x] 10.5 **Behaviour change: the bean's yield is now button-protected.** It used to be learned silently — from Brew Settings OK *and* from every saved shot. Now it changes only via "Update Bag". Say so plainly; the old behaviour was invisible, so its absence will be too
- [x] 10.6 **Behaviour change: "Update Profile" for yield moves** out of Brew Settings to the Profile Editor / Simple Profile Editor (a profile can't hold a ratio). Point at where it went
- [x] 10.7 Check the manual's existing ratio/stop-at-weight/brew-settings pages for text this change invalidates — the ratio quick-select widget's behaviour changes (it now arms a ratio rather than snapshotting a weight), and any screenshot showing an Update button on both rows is now wrong

## 11. Close-out

- [x] 11.1 Full `--target all` build (MCP register-stub signatures are duplicated in `tst_mcpserver_session`/`tst_mcpserver_protocol` and externed in `tst_mcptools_*`)
- [x] 11.2 Clear all warnings, including QML TypeErrors in the running app
- [x] 11.3 Ask Jeff to launch and verify on the real app: ratio recipe tracks a dose capture; absolute recipe does not; the button moves; a bean switch does not clobber an active recipe
- [ ] 11.4 `/opsx:archive` as the last commit on the branch, so the archive + spec promotion land inside the PR
- [ ] 11.5 Close issue #1533 on merge
