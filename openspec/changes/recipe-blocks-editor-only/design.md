## Context

See proposal.md — Why. The mechanism is five `connect()` calls in `MainController::setupRecipeConnections()` that fire `stampActiveRecipeSteam()` / `stampActiveRecipeHotWater()` off live `SettingsBrew` signals. Each stamp re-snapshots the currently selected pitcher or vessel into the active recipe's row.

Two existing facts shape the approach:

- `applyActivatedRecipe` already re-pushes both blocks into the live settings on every activation, including same-id re-activation (which replays the in-memory `m_activeRecipe` rather than re-reading the row). So the stored block is the recovery path; nothing new has to be built to restore a live value.
- The same-shape decision was already made for yield and temperature by `recipe-aware-brew-settings`: the auto-stamp watchers were deleted and persistence moved to an explicit action. This change puts the two blocks on that side of the line.

## Goals / Non-Goals

**Goals:**
- A live pitcher/vessel change never rewrites a recipe row.
- The user can see they have left the recipe, and get back to it in one tap.

**Non-Goals:**
- No revert-on-dispense, and no "Update Recipe" button on the Hot Water or Steam page. Blocks persist through the wizard, MCP, and web only.
- Dose, grind, and RPM write-through are untouched.

## Decisions

**A pitcher or vessel is an ingredient, not a tweak.** The first draft of this change made them per-brew overrides on the yield/temperature model: the tweak applies, the recipe stays active, its row is untouched. That is coherent but silent — the machine goes on making a drink the active recipe does not describe, the shot is still attributed to that recipe, and the way back is unobvious (from the idle screen a second tap on the selected pill starts a shot, so the recipe cannot be re-applied from there at all).

Deactivating instead puts them where the bean, the equipment package and the profile already are. The pill deselects, so the user can see they have stepped outside the recipe; the shot is no longer attributed to a recipe it did not match; and because the pill is now deselected, the next tap re-activates rather than starting a shot — one tap restores everything. The recovery path stops being a caveat and becomes the mechanism.

*Alternative considered*: keep the per-brew-override model and add a revert once the dispense ends. Rejected — it fights a deliberate mid-session choice, and it needs new machinery to reach a state one tap already reaches.

**Delete the connections, keep the helpers.** `currentSteamSpecJson()` / `currentHotWaterSpecJson()` build a block from live settings and still have legitimate callers (shot metadata, and the editor paths that assemble a block from what is currently set up). `stampActiveRecipeSteam()` / `stampActiveRecipeHotWater()` lose their only callers and go with the connections — a private method with no caller is dead code, and leaving it invites the next person to re-wire it.

*Alternative considered*: keep the stamps but gate them behind a setting. Rejected — a per-user toggle for "does my recipe change when I touch a control" is a question users should not have to hold an opinion about, and it doubles the states every later recipe change has to reason about.

**Dose stays.** Issue #1895 argues for grind only, but the dose stamp is welded to the dose-source-precedence ladder: the same handler calls `setActiveRecipe(recipeId, dose)` so the ladder's recipe rung names the dose the stamp just persisted. Removing the stamp without also reworking the rung would leave the ladder claiming a dose the recipe does not store. Dose is also dial-in in the same sense grind is — a measurement of what the user physically did. Out of scope; confirmed with the maintainer.

**Milk weight goes with the steam block.** `lastSteamMilkG` is the amount steamed for one drink, not a property of the recipe's design, and it is stamped through the same block. Leaving it behind would keep a recipe mutating from the milk jug while the pitcher beside it no longer does.

**Only the SELECTION signals deactivate.** `steamPitcherPresetsChanged` / `waterVesselPresetsChanged` fire on add, remove, reorder and field edits — editing a vessel's flow rate is not swapping an ingredient, and the recipe's block is a by-value snapshot that deliberately does not follow the preset. `lastSteamMilkGChanged` is written automatically when a steam session ends (`main.qml`, the phase-change handler), not by the user: deactivating on it would drop every milk recipe seconds before its own shot is saved, so the drink that most needs its recipe attribution would be the one that loses it.

**Ownership gates each watcher, and the rule has one definition.** A recipe whose steam block names no pitcher owns no pitcher choice, exactly as a bean-less recipe owns no bag choice — the shape `Recipe::ownsProfileChoice` / `profileDiverged` already established, and the reason that shape exists is that hand-written comparisons at each call site drifted until a shot was stamped with a recipe whose profile it had not run. The new rules go beside them as `Recipe::` statics, which also makes them directly testable without standing up a `MainController`.

**Deactivation must not eat its own trigger.** `deactivateRecipe()` unwinds the recipe pitcher override, and that unwind re-selects the standing pitcher parked at activation (`SettingsBrew::resolveRecipePitcherOverride`). Deactivating in response to the user's own pitcher pick would therefore restore the parked pitcher over the pick. The user has now chosen a pitcher themselves, so there is no parked "user's own" selection left to restore: the watcher clears the standing selection before deactivating, and the unwind becomes the no-op it should be.

## Risks / Trade-offs

- **A user who relied on the old behavior loses an implicit save.** Someone who tuned a pitcher mid-session and expected the recipe to keep it must now edit the recipe. → This is the reported defect from the other side, and the same trade was already accepted for yield and temperature. The wiki manual entry states where a block change is kept.
- **Deactivation is more visible than a silent tweak, and that cuts both ways.** A user who changes vessels mid-drink loses the Active badge and the recipe's dial-in framing for that drink. → That is the point: they are making a different drink. It matches what a bean or equipment change already does, so it is not a new idea for the user to learn.
- **A stale block after a preset edit.** Renaming or re-valuing a vessel no longer propagates into recipes that snapshot it, so an old recipe can name values the preset no longer has. → Already the documented model (`snapshot-not-reference`); activation recreates a deleted preset from the snapshot rather than failing.
- **A test asserting an absence can pass vacuously.** No existing test asserted the stamps, and `MainController` needs five live collaborators to stand up, so the guard is a source-text assertion. It must be written as an ALLOWLIST — the fields that DO stamp — not as a list of signals that must not: a denylist is satisfied by a member-slot connect, a renamed helper, or a signal nobody listed, all while the bug is back.
