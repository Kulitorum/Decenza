## Context

Issue #1533 asks for ratio-defined yield. The audit found the app already behaves inconsistently around the (dose, ratio, yield) triple — see `proposal.md` — because ratio is never stored and "is this an override?" is inferred by float-comparing grams against the profile default.

The precedent for the fix is already in the repo. `Recipe::tempOffsetC` (migration 31) converted temperature from an absolute to a **relative quantity resolved at use time**, so editing the profile moves the recipe with it and a stale absolute can never be manufactured. `recipestorage.h:78-84` states the rule; `recipepromotion.cpp:50-73` does the absolute→relative conversion at promotion time and its comment — *a delta against an unknown baseline is meaningless* — is the argument for this change, written next to the code that needs it.

The difference: `tempOffsetC` anchors on the **profile**, which does not move mid-session. A ratio anchors on the **dose**, which does. That single difference is where all the new machinery lives.

```
              ┌── recipe active ──▶ Recipe.yield  {value, mode}   ── button-protected
  resolve ────┼── else bag ───────▶ Bag.yield     {value, mode}   ── button-protected
              └── else ───────────▶ Profile.target_weight (absolute)

  session anchor {value, mode} ──▶ resolve(dose) ──▶ grams ──▶ MachineState ──▶ WeightProcessor
                     │                                                            (stop decision)
                     └────────── provenance ──────────┐
                                                       ▼
                                    shots: yield_override (grams — what ran)
                                           yield_mode + yield_anchor_value (what was meant)
```

## Goals / Non-Goals

**Goals**
- A recipe, a bag, and the session can each express yield as `none` | `absolute` | `ratio`, with the ratio re-deriving the target whenever the dose changes pre-shot.
- Every surface agrees on which quantity is held constant when the dose moves.
- The stop target never moves mid-shot as a result of a dose write.
- No new user-facing setting (per project convention: prefer smarter defaults over flags).

**Non-Goals**
- Changing the stop-at-weight algorithm, SAW learning, or auto flow calibration (see Decision 6).
- Putting a ratio on a **profile**. `target_weight` is absolute, and profiles are shared, exported, and authored by third parties — there is no ratio in that format. Profiles stay absolute-only.
- Ratio on the Visualizer/DYE wire. DYE has no such field and computes ratio itself from `bean_weight`/`drink_weight`.
- Reworking the `+10 g` mid-shot bump (`maincontroller.cpp:3533`), which is correctly phase-gated already.

## Decisions

### 1. One value column + a mode discriminator — not two columns

**Decision:** store `yield_value REAL` + `yield_mode TEXT` (`none`/`absolute`/`ratio`). Reject `yield_g` + `yield_ratio` side by side.

**Why:** the user's requirement is that a recipe cannot hold both. Two columns make the illegal state *representable* and push the invariant into every reader. Two concrete failures:

- **MCP `recipe_update` is present-keys-only** (`mcptools_recipes.cpp:207-215`). A client sending only `yieldRatio` would **not** clear a stored `yieldG` — the invariant is violated by a wire format we do not control, silently. With a mode column, writing a ratio *is* setting the mode; no stale sibling can exist.
- It recreates the `temp_offset_c` / `temp_override_c` ambiguity exactly: a row with both non-NULL, plus a "derive the mode from which one is set" pass — the same class of bug as `convertLegacyTempOffsetsStatic` (`recipestorage.cpp:1049-1130`).

The JSON surfaces still expose `yieldG` and `yieldRatio` as sparse, mutually exclusive keys and **reject both-present loudly**, mirroring the established `temperatureOverrideC` rejection (`mcptools_recipes.cpp:440-441`, `:538-539`; its web twin at `shotserver_recipes.cpp:205-206`, `:362-363`).

**Note:** `COL_DBL` binds via `nullIfZero`. A ratio is strictly positive so plain `COL_DBL` is safe for `yield_value`; no `COL_DBL_SIGNED` needed (contrast `tempOffsetC`).

### 2. The anchor is "last written", not a mode the user picks

**Decision:** whichever of {ratio, yield} was last written is the anchor. The other is derived through the dose and displayed as a consequence.

**Why:** this already exists as `BrewDialog`'s `targetManuallySet` (`:40`, `:223-231`) — an ephemeral, dialog-local discriminator that dies on close. The feature is largely *giving it a permanent home*. Consequences:

- The wizard's "if I type 36, pick 1:2, then switch back, is 36 lost?" problem dissolves — the non-anchor field is not empty, it is *derived and dimmed*. Nothing is lost, so nothing tempts us to persist both.
- Both rows already derive their override state from **one** baseline, with the second computed through the dose (`BrewDialog.qml:1006`, `:1061`). Only the anchor's *unit* changes. The payoff falls out in both directions: on a dose change, the derived row and its derived baseline move by the same amount, so **neither row spuriously highlights**, in either mode, with no special-casing.

**Seed:** `BrewDialog.qml:147` currently reads `targetManuallySet = Settings.brew.hasBrewYieldOverride`. Since activation writes a grams override for *any* recipe, the dialog always opens yield-first and a ratio recipe's identity is invisible. That line becomes "is the recipe's mode absolute?".

### 3. A single Update button that sits on the anchored row

**Decision:** one button, on whichever row is anchored; editing the other row moves it there. Label follows the ladder: *Update Recipe* → *Update Bag*.

**Why:** it makes the button's location the anchor indicator, so no separate mode chip is needed — which matters because the visual channel is already taken (`valueColor: overridden ? highlightColor : textColor`). Two buttons both reading "Update Recipe" but producing different recipes would be a coin flip; per-row pins would depart from the existing idiom (`BrewDialog.qml:748` uses the same button shape for Temp Delta).

Handled cases:
- **mode `none`** — no anchor, so **no button on either row** until the user edits one. The tri-state's third state needs no special case.
- **Converting ratio → absolute** — edit the stop-at row; that anchors it and the button follows. Expressing "I want this number, absolutely" by touching the number field is the honest gesture.
- **No recipe and no bag** (bean-less brew, fresh install) — nothing to write back to, so the button disappears; the session override still works for that brew. *(See Open Questions.)*

**"Update Profile" leaves Brew Settings.** Both profile editors still expose target weight (`ProfileEditorPage.qml:659`, `SimpleProfileEditorPage.qml:585`), so the capability moves rather than disappears — to where profiles are edited.

### 4. The scale owns the dose; the recipe/bag owns the anchor

**Decision:** a dose capture always writes the dose (unchanged) and never changes the mode.

| Anchor | Capture reads 17.5 g (was 18) |
|---|---|
| absolute 36 g | dose → 17.5, **yield stays 36**, ratio display drifts to 1:2.06 |
| ratio 1:2 | dose → 17.5, **yield re-derives to 35**, ratio stays 1:2 |
| none | dose → 17.5, yield stays the profile's |

This settles what a recipe's `doseG` is: a **seed, not a pin**. The recipe does not own the dose. Cards resolve against the recipe's seed while browsing; at brew time the scale wins.

The diff is small — both sites already write the dose correctly and unconditionally; only the yield half is wrong:

```qml
// IdlePage.qml:248-249
Settings.dye.dyeBeanWeight = net                                    // ✓ keep
Settings.brew.brewYieldOverride = net * Settings.brew.lastUsedRatio // ✗ anchor-blind, global ratio

// BrewDialog.qml:206-207
root.targetManuallySet = false                                      // ✗ silently flips the anchor
root.doseValue = Settings.dye.dyeBeanWeight                         // ✓ keep
```

### 5. Bag dial memory splits on measurement vs intent

**Decision:** `grinderSetting`, `rpm`, `doseWeightG` keep their unconditional write-through. The yield anchor is button-protected.

**Why:** it is Decision 4 applied one level down. Grind/rpm/dose are **things the user physically did** — the bag remembering them is dial-in memory, and the dose one is already load-bearing (every capture writes through). The yield anchor is **intent**, the same category as the recipe's yield.

This also kills a live bug by construction. Today the bag's dose writes through on every capture (`settings_dye.cpp:469`) while its yield lands only at the two commit points below, so the pair drifts:

> Bag has dose 18, yield 36. Weigh 17.5 → `doseWeightG` becomes 17.5, `yieldOverrideG` stays 36. The bag now stores a **1:2.06 dial nobody chose**, and re-selecting that bean brews it.

Storing an anchor leaves no implied ratio to rot. No extra write-through needed.

**Forced consequence — there are TWO auto-writers of the bag's yield, not one.** Both must go, or "Update Bag" is a no-op because something else already did its job:

1. `ProfileManager::activateBrewWithOverrides` → `persistYieldOverrideToBag` (`profilemanager.cpp:444`) — fires on every Brew Settings OK.
2. **`MainController`'s shot-save stamp (`maincontroller.cpp:3193-3207`)** — fires on **every saved shot**, calling `CoffeeBagStorage::yieldOverrideForTarget(shotTargetWeight, profileTarget)` and writing `yieldOverrideG` into the bag update. This one is easy to miss: it is the reason the `coffeebagstorage.h:80` comment "stamped on shot save" is **accurate**, and it means `yieldOverrideForTarget` has three callers, not two.

Cutting only (1) would leave the bean still silently learning its yield from every shot. The behaviour change is therefore larger than "OK stops writing": **the bean stops learning its yield from shots at all.** That is the intent — a yield is intent, and a shot is a measurement — but it is the most invisible of the three behaviour changes, because the thing being removed was never visible in the first place.

### 6. Ratio never crosses `MachineState`

**Decision:** the anchor resolves to grams before `MachineState::setTargetWeight`. The shot record receives the anchor by a **side channel**, not through the machine.

**Why:** everything downstream of `MachineState` consumes grams and only grams, and this is the cheapest seam in the change:

- `shotanalysis.cpp:346`/`:575` — the overshoot and shortfall arms are pure `finalWeightG / targetWeightG`. A ratio there is meaningless. **No changes needed.** (`detectPourTruncated` reads no target weight at all despite the name.)
- `ShotAnalysisDialog.qml` / `QualityBadges.qml` contain zero yield/dose/target/ratio references. **No changes.**
- **MQTT** publishes `de1_*_target_weight` as an HA discovery entity with `device_class: weight`, `unit_of_measurement: "g"` and a stable `unique_id` (`mqttclient.cpp:885-892`). Third-party HA installs have history and statistics bound to it. It reads `MachineState::targetWeight()`, so it stays correct — **never emit a ratio on this topic.**
- **DYE/Visualizer** export the shot's *actual* `bean_weight`/`drink_weight` only (`visualizeruploader.cpp:362-363`). No recipe target, no ratio field. **No changes.**

**The limit of this decision: units, not drift.** "Downstream consumes grams, so downstream is untouched" is true about *units* and false about *drift assumptions*. One consumer depends on the resolved gram value being a stable constant per setup, which a ratio anchor removes by design: **Auto Favorites**, in its finest, opt-in `bean_profile_grinder_weight` mode, groups on a **bucketed** dose (`ROUND(x*2)/2.0` — bucketed precisely because dose drifts) and an **exact** `yield_override` (`shothistorystorage_queries.cpp:1143-1150`, with matching filters at `:249` and `:1288-1296`). Under a ratio anchor the yield drifts too, so shots at 17.9/18.1/18.2 g share one dose bucket but produce three exact yields — three cards where there was one.

**Accepted as-is, per an explicit call:** Auto Favorites keeps grouping on the yield as it lands, with no anchor branch. Three reasons, in order of weight:

1. **Auto Favorites may eventually be deprecated in favour of recipes** — its finest mode groups by bean + profile + grinder + grind + dose + yield, which *is* a recipe's identity; the only difference is that a recipe is authored and a favorite is discovered from history, and `recipe_create_from_shot` already bridges that. Teaching a possibly-doomed feature a new concept is the wrong investment. (This change quietly narrows the gap further: once shots carry their anchor, promotion is lossless — a 1:2 shot promotes to a 1:2 recipe rather than freezing into grams.)
2. **The blast radius is one opt-in mode.** The default `autoFavoritesGroupBy` is `bean_profile` (`settings_network.cpp:133`), which does not group on yield at all.
3. **Nothing else depends on the stability.** Every *other* reader of `yield_override` treats it as "what this shot's target was" — for which a value that varies per shot is simply correct.

Verified unaffected: the AI advisor's `dialInSessions` (`dialing_blocks.cpp:151`) runs its own SQL and does not group on yield; bean search (`unifiedbeansearchmodel.cpp:207`), MCP `shots_list` (`mcptools_shots.cpp:241`), and ShotServer all read it as a per-shot value.

The residual, accepted: within a single 18.0 g dose bucket a 1:2 ratio spans 35.5–36.5 g, so a ratio user of that opt-in mode sees more cards than before. If it ever grates, bucketing the yield to 0.5 g like the dose absorbs most of it and still needs no anchor branch.

**SAW learning needs no changes, and this should be stated so nobody "fixes" it.** It learns a drip-vs-flow regression keyed on `(baseProfileName, scaleType)` and stores no absolute target: `drip = m_weight - m_weightAtStop` (`shottimingcontroller.cpp:624`) is measured against the **trigger weight** and predicted purely from flow. A 1:2 and a 1:3 shot on one profile teach the same drip model — correctly, since drip is a lag×flow quantity. The one target-dependent value, `overshoot`, uses `m_targetWeightAtStop` captured **at trigger time** (`:630`, fed by `sawTriggered`'s live payload), so it is already correct against a target that moved — the `+10 g` bump forced that property. Learning being per-profile while the target is per-recipe *looks* like a mismatch and is not; the flow-similarity weighting already absorbs ristretto-vs-lungo end flows.

### 7. Shots store outcome **and** intent; promotion is a copy

**Decision:** keep `shots.yield_override` (resolved grams). Add `yield_mode` + `yield_anchor_value`. Promotion copies the anchor.

**Why the anchor value must be stored, not derived:** `dose_weight` is **post-shot editable** (`PostShotReviewPage.qml:1483`). Deriving `ratio = yield_override / dose_weight` breaks the moment a user corrects the dose:

> Brew 18 g → 36 g at a 1:2 anchor. Correct the dose to 19 g on the review page. Derived ratio becomes 1:1.89 — a number nobody chose. Promote it and the recipe inherits the typo.

Storing it makes promotion a straight copy and **dissolves** the "which dose — frozen or corrected?" question. Compare `recipepromotion.cpp:48-49` today, whose `doseG` = achieved/corrected but `yieldG` = frozen plan, silently minting an implicit ratio nobody intended.

This is not the Decision-1 exclusivity problem: `yield_override` (outcome) and the anchor (intent) are **different facts**, both legitimately true at once. Redundancy when mode is `absolute` is harmless — shot records already snapshot the entire profile JSON wholesale.

### 8. Ratio survives a profile change; absolute does not

**Decision:** a profile load clears an absolute session override and keeps a ratio one.

**Why:** the existing wipe is correct **for grams** — 36 g from profile A is meaningless on profile B. But 1:2 is 1:2 on any profile. The reset should follow the type, not the storage slot. This is the same principle that keeps ratio off the profile entirely (Non-Goals), and it delivers the issue's "persistent brew-by-ratio mode" **as a consequence of what a ratio is** — no toggle, no setting, nothing to explain.

**Where it actually goes — not where it looks like it goes.** `resetBrewOverridesForLoadedProfile()` (`profilemanager.cpp:466-480`) early-returns:

```cpp
if (m_startupLoadDone) {
    brew->clearAllBrewOverrides();   // ← EVERY normal runtime profile load
    return;
}
// tail: startup only — the float-compare "matches the default, so not an override" pass
```

The float-compare tail (including the Bug-A instance at `:478`) runs **only at startup**. Every user-initiated profile switch takes the early return. So the mode-awareness must land in the `m_startupLoadDone` branch, which means touching `clearAllBrewOverrides()` (`settings_brew.cpp:937`) — and that has **five callers with conflicting intent**:

| Caller | Wants |
|---|---|
| `profilemanager.cpp:460` (`clearBrewOverrides`, the user's explicit **Clear**) | clear the ratio |
| `:471` (reset on profile load) | **keep** the ratio |
| `:2205`, `:2456` (profile edit / new profile) | clear — a kept ratio would leak into a freshly edited profile |

The helper needs splitting into "clear everything" and "clear what this profile owns". Doing the obvious edit to the visible float-compare would put the headline behaviour in dead code.

### 9. The dose is latched at `espressoCycleStarted`

**Decision:** resolution uses a dose latched at shot start, cleared at shot end. Event-based, not a timer (project convention).

**Why:** `main.cpp:953` forwards *any* `MachineState::targetWeightChanged` to the worker with **no phase gate**, and `weightprocessor.cpp:358` assigns with no `m_active` check. Its safety comment only reasons about pre-shot callers. This is inert today purely because `dyeBeanWeightChanged` is notify-only (`profilemanager.cpp:337`). The moment a ratio re-derives yield from dose, **every dose-write path becomes a live SAW-target mutation** — QML, MCP `mcptools_write.cpp:875`, settings import, and worst of all the *queued* `setDyeBeanWeight(recommendedDose)` at `profilemanager.cpp:1259`, which can land after a shot has begun.

Latching alongside the SAW model snapshot already taken at `main.cpp:849-859` keeps the two consistent.

### 10. Retire the "is this an override?" float-compares

There are **five**, not the four the first audit found: `profilemanager.cpp:429`, `:478` (startup-only), `:400` (inside `brewByRatioActive`), `maincontroller.cpp:1373`, and `maincontroller.cpp:788-789` (shot restore). Each infers override-ness from `qAbs(yield − profileTarget) > 0.1`, and each erases a ratio whose derived yield lands on the profile target. `CoffeeBagStorage::yieldOverrideForTarget` (`coffeebagstorage.cpp:670`) is a sixth instance of the same shape, but it is deleted outright rather than fixed — it exists only to serve the two bag auto-writers Decision 5 removes. A stored mode gives all of them a real answer; none has a job afterwards.

## Risks / Trade-offs

- **Behaviour change, absolute recipes.** Today a dose capture stomps an absolute yield with `dose × lastUsedRatio` in two places. After this, absolute yields stay put. This is the fix, but users will notice their numbers stop moving. → Wiki manual entry (tasks §9).
- **Behaviour change, bag yield.** Today your bean silently learns your yield on every OK. After this you press Update Bag. Mitigation: the current "memory" is already incoherent (Decision 5) and is the mechanism producing the drift.
- **Behaviour change, "Update Profile" leaves Brew Settings.** Mitigation: both profile editors still expose target weight.
- **The `yieldG == 0` sweep is the bulk of the risk, not the feature.** Seven sites need attention (tasks §5, a test per site). Four literally fall back to the profile yield (`maincontroller.cpp:1557`; `BrewDialog.qml:76`, `:395`; `RecipeDrinkCard.qml:240`); `ShotPlanItem.qml:40` falls back to 0 instead; `maincontroller.cpp:1572` compares against the wrong baseline; `CustomItem.qml:55` only reads `yieldIsRealOverride` and is fixed transitively. Missing one reintroduces #1485's spurious amber `"36.0 → 40.0g"` arrow through a side door.
- **Asymmetric override tolerances.** Ratio uses `> 0.05`, stop-at uses `> 0.1` g (`BrewDialog.qml:1006`, `:1061`). At an 18 g dose a 0.05 ratio nudge is **0.9 g** — under the ratio row's tolerance but 9× over the stop-at row's, so the two rows disagree about whether the user is overridden. Invisible while the anchor is always grams; live once the rows are symmetric peers. → express tolerance in one unit and convert through the dose (task 4.6).
- **Activation write order.** `applyActivatedRecipe` writes yield **synchronously** (`:1371`) but the dose **queued** (`:1356`); an inline `dose × ratio` at `:1371` would multiply the stale, pre-activation dose. → task 3.3.
- **Bag override beats an active recipe today.** `maincontroller.cpp:257` applies the bag's override **unguarded** after `activeBagIdChanged` clears, so on a bean switch it lands last and overwrites an active recipe's anchor. The ladder must be enforced, not left to event ordering. → task 3.5.
- **Three homes = more surface.** Mitigation: one `YieldSpec` type, one control, one anchor mark, reused three times; only two SQL tables change (`Settings.brew` is QSettings, no migration).
- **Latent, surfaced but not fixed here:** frame `exitWeight` limits are snapshotted at `configure()` and never scale with the override (`weightprocessor.cpp:311-338`), so a 36 g frame exit still fires when a ratio derives 54 g. True today for a manual 54 g override too; ratio makes large excursions routine. → Open Questions.

## Migration Plan

**Migration 34** (latest is 33, `shothistorystorage.cpp:1622`), three tables in one step:

| Table | Columns | Backfill |
|---|---|---|
| `recipes` | `yield_value REAL`, `yield_mode TEXT` | `yield_mode = 'absolute'` where `yield_g > 0`, else `'none'`; `yield_value = yield_g`. Leave `yield_g` dead in place (the `temp_override_c` precedent). |
| `coffee_bags` | `yield_value REAL`, `yield_mode TEXT` | `yield_mode = 'absolute'` where `yield_override_g > 0`, else `'none'`; `yield_value = yield_override_g`. Leave `yield_override_g` dead in place. |
| `shots` | `yield_mode TEXT`, `yield_anchor_value REAL` | `yield_mode = 'absolute'` where `yield_override > 0`, else `'none'`; `yield_anchor_value = yield_override`. |

The recipe and bag backfills reproduce today's behaviour exactly — an absolute yield is already absolute, so the conversion is a relabel, not a recomputation.

**The shots backfill is a deliberate simplification, not an exact reproduction.** `shots.yield_override` is three-way overloaded at save time (`maincontroller.cpp:2902-2905`, `:3040-3041`): it holds the user's override, *or* the profile's default target, *or* — for volume/timer profiles with no target — a backfill of the **achieved final weight**. At migration time these are indistinguishable. Labelling all three `absolute` therefore asserts an anchor the user did not always choose.

Accepted, per an explicit call: a legacy shot promotes using its recorded yield, exactly as `recipepromotion.cpp:48-49` does today, so promotion behaviour is unchanged. The only new exposure is display (task 6.8's target-ratio chip), where a volume-profile shot can show a "target" that is really just what came out. Not worth a fourth mode to model precisely; if it grates in practice, suppress the chip for shots whose profile carries no `target_weight`.

- **QSettings** (`Settings.brew`): a new `espresso/brewYieldMode` key; absent = `absolute` when `hasBrewYieldOverride`, else `none`. No migration step.
- **Device transfer / backup restore** is tolerant in the old→new direction: `recipestorage.cpp:1365-1390` substitutes `NULL AS <col>` for any column in `recipeColumnList()` missing from the source, so an older backup imports with struct defaults. The `coffee_bags` and `shots` import paths need their column lists and source-index resolution extended the same way.
- **Visualizer:** the bag's dose/yield are already excluded from `touchesVisualizerFields()` (`coffeebagstorage.cpp:652-661`) — the new mode column must land in that same local-only set so an anchor edit never triggers a network PATCH.
- **`lastUsedRatio`** is demoted to preset memory (which preset is highlighted; the seed for a fresh no-recipe brew). Check during implementation whether it is retirable entirely.

## Resolved by review

- **Legacy shot backfill** → `absolute`; a legacy shot promotes using its recorded yield, as today. See the Migration Plan caveat.
- **The bag's shot-save yield stamp** → removed, along with the OK-time write. See Decision 5.
- **Auto Favorites grouping** → unchanged; group on the yield as it lands, no anchor branch. See Decision 6.

## Open Questions

1. **No recipe and no bag** — button disappears (assumed, Decision 3) vs. falls back to "Update Profile" for an absolute anchor. One-line decision; flagged for review.
2. **Shot restore / "brew this again"** (`maincontroller.cpp:788-795`, and `AutoFavoritesPage.qml:67-78`, which carries its own second yield-resolution implementation) — now that the shot remembers its anchor, should re-brewing a 1:2 shot restore **1:2 against today's dose**, or **36 g flat**? Restoring the anchor is more faithful and consistent with Decision 8, but it is a behaviour change to an existing feature, and the legacy-backfill simplification means a legacy volume-profile shot's "anchor" may be a fabricated one — restoring grams is the safer default for those. Leaning anchor-restore for shots that genuinely carry a ratio, grams otherwise. Needs a call.
3. **Frame `exitWeight` vs. a derived target** (Risks) — out of scope, or fold in?
4. **Activation stomps a measurement.** Weigh 17.5 g, then activate a recipe with `doseG = 18` → activation writes 18 (`maincontroller.cpp:1356`) and the capture will not re-fire (`StableWeightCapture` is a persistent latch that deliberately does not re-capture an already-weighed cup, `IdlePage.qml:231`). "Capture overrides no matter where it came from" holds by precedence but not by ordering. Pre-existing; today a stale dose only mislabels the shot record, but with a ratio anchor it silently mis-derives the stop target. Note this is in direct tension with `recipe-activation`'s scenario "The ratio resolves against the recipe's dose, not the previous one" — if the live dose came from the scale, the recipe's seed is the *wrong* answer by Decision 4's own rule. In scope?
5. **Ratio bounds are inconsistent three ways** and none of them is in C++: `setRatioPreset1/2/3` clamps `qBound(0.5, r, 6.0)` (`settings_brew.cpp:151/162/173`), `ratioInput` allows `0.5–20.0` (`BrewDialog.qml:1009`), `targetInput` allows `1–500` (`:1064`), and `setBrewYieldOverride` (`settings_brew.cpp:909-931`) clamps nothing. Dial 1:20, save it as a preset → silently snapped to 6.0. A stored ratio re-deriving on every dose change makes this latent inconsistency live: ratio 20 × dose 50 = 1000 g, double `targetInput`'s ceiling, straight into an unclamped setter. Pick one bound and enforce it in C++ (task 4.9).
