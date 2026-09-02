## Context

See proposal.md — Why. The mechanism is five `connect()` calls in `MainController`'s constructor ([maincontroller.cpp:1609-1627](../../../src/controllers/maincontroller.cpp)) that fire `stampActiveRecipeSteam()` / `stampActiveRecipeHotWater()` off live `SettingsBrew` signals. Each stamp re-snapshots the currently selected pitcher or vessel into the active recipe's row.

Two existing facts shape the approach:

- `applyActivatedRecipe` already re-pushes both blocks into the live settings on every activation, including same-id re-activation (which replays the in-memory `m_activeRecipe` rather than re-reading the row). So the stored block is the recovery path; nothing new has to be built to restore a live value.
- The same-shape decision was already made for yield and temperature by `recipe-aware-brew-settings`: the auto-stamp watchers were deleted and persistence moved to an explicit action. This change puts the two blocks on that side of the line.

## Goals / Non-Goals

**Goals:**
- A live pitcher/vessel change never rewrites a recipe row.
- The recipe's stored block stays reachable — re-activating restores it.

**Non-Goals:**
- No revert-on-dispense. Watching for the end of a hot-water or steam operation to re-push the block is machinery for a state the user can already leave by re-activating, and it would fight a deliberate mid-session choice.
- No "Update Recipe" button on the Hot Water or Steam page. Blocks persist through the recipe editor, MCP, and web only.
- Dose, grind, and RPM write-through are untouched.

## Decisions

**Delete the connections, keep the helpers.** `currentSteamSpecJson()` / `currentHotWaterSpecJson()` build a block from live settings and still have legitimate callers (shot metadata, and the editor paths that assemble a block from what is currently set up). `stampActiveRecipeSteam()` / `stampActiveRecipeHotWater()` lose their only callers and go with the connections — a private method with no caller is dead code, and leaving it invites the next person to re-wire it.

*Alternative considered*: keep the stamps but gate them behind a setting. Rejected — a per-user toggle for "does my recipe change when I touch a control" is a question users should not have to hold an opinion about, and it doubles the states every later recipe change has to reason about.

**Dose stays.** Issue #1895 argues for grind only, but the dose stamp is welded to the dose-source-precedence ladder: the same handler calls `setActiveRecipe(recipeId, dose)` so the ladder's recipe rung names the dose the stamp just persisted. Removing the stamp without also reworking the rung would leave the ladder claiming a dose the recipe does not store. Dose is also dial-in in the same sense grind is — a measurement of what the user physically did. Out of scope; confirmed with the maintainer.

**Milk weight goes with the steam block.** `lastSteamMilkG` is the amount steamed for one drink, not a property of the recipe's design, and it is stamped through the same block. Leaving it behind would keep a recipe mutating from the milk jug while the pitcher beside it no longer does.

**The residual gap is accepted.** Tapping an already-selected recipe pill on the idle screen starts a shot; it does not re-activate. So from idle the recovery is the Recipes page (or another recipe and back), not the pill. Changing the two-tap pill gesture is a different change with its own blast radius.

## Risks / Trade-offs

- **A user who relied on the old behavior loses an implicit save.** Someone who tuned a pitcher mid-session and expected the recipe to keep it must now edit the recipe. → This is the reported defect from the other side, and the same trade was already accepted for yield and temperature. The wiki manual entry states where a block change is kept.
- **A stale block after a preset edit.** Renaming or re-valuing a vessel no longer propagates into recipes that snapshot it, so an old recipe can name values the preset no longer has. → Already the documented model (`snapshot-not-reference`); activation recreates a deleted preset from the snapshot rather than failing.
- **Silent no-op risk in test coverage.** The existing tests assert the stamps happen; inverted assertions must be written so they can still fail — assert the stored block is byte-identical after a vessel change *and* that a recipe-editor update does change it, so a test that stopped exercising the path shows up.
