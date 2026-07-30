## Context

`EquipmentStorage::supersedeOrEditStatic` has three outcomes today: identity unchanged (no-op), another in-inventory package already carries this exact identity (merge into it), otherwise edit in place when the package has no shots and fork when it does. Identity is the full tuple — grinder brand/model/burrs, basket brand/model, puck-prep canonical flag string — and burrs are in it precisely because a burr swap changes how the grinder behaves.

The fork rule reads the tuple but not the *direction* of the edit, so an empty field gaining a value is indistinguishable from a real swap. [#1713](https://github.com/Kulitorum/Decenza/issues/1713) is that gap in the field: a user typing the burr make/model onto a grinder he had used for years got the package retired and a fresh one created.

A fork does not merely rename the grinder — it empties its history, because the history lookups are keyed on exactly what the fork changed. `buildGrinderCalibrationBlock` matches shots on grinder model **and burrs** (`src/ai/dialing_blocks.cpp`), so every shot pulled before the burrs were recorded stops matching; the reporter's own log shows the calibration collapsing to a single pair. Dial-in session grouping keys on `COALESCE(equipment_id, 0)`, so it splits at the fork as well. From the user's side the grinder is simply new, and the grind wheel loses the resolution it had learned.

The state that leaves behind is unrepairable with what exists. The merge branch inside `supersedeOrEditStatic` matches on identity (not ids), only considers `in_inventory = 1` candidates, and leaves the source's shots on the source — none of which fits "these two packages are one grinder, put them back together".

## Goals / Non-Goals

**Goals:**
- Recording gear a package always had never costs the package its history.
- A genuine swap keeps forking, so historical shots keep the gear they were pulled on.
- A user who already hit this — or who splits a grinder any other way — can repair it.
- The repair moves *every* reference, so nothing is left pointing at a deleted row.

- Users this already happened to get repaired on upgrade, without being asked to notice.

**Non-Goals:**
- No change to what identity *is*. Burrs stay part of it.
- No UI for merge — MCP only, by decision. The app's Equipment page and the ShotServer `/equipment` page keep no merge action.
- The heal does not attempt to repair *every* wrongly split package, only the one signature it can identify without guessing. Everything else stays a manual `equipment_merge`.
- Not a fix for the grind-step lookup (`grindStepForGrinder`) keying on an exact, case- and whitespace-sensitive `model = ?` while identity matching everywhere else folds case. That query is model-only, so a burr-only fork should not have emptied it — it is a separate defect surfaced by the same report.

## Decisions

**Enrichment is empty → value, per component, and nothing else.** `filledIn(before, after)` is true when `before` is empty or equal to `after`; the edit is enrichment when that holds for every component. Alternatives considered: (a) treat any *superset* as enrichment — rejected, since "wdt" → "wdt,rdt" describes a different routine than the earlier shots were pulled with; (b) ask the user "did you swap, or are you recording?" at edit time — rejected as a question most users cannot answer confidently about their own past, for a fork they did not ask for.

The asymmetry is the point and is worth stating plainly: if the old burrs were never named, the app had nothing to distinguish the two states by, so nothing is lost by not forking. If they *were* named, a swap forks exactly as before.

**Enrichment is checked after the merge lookup, before the shot count.** Merge stays the first branch: if another live package already describes the enriched identity, folding into it is still the right answer and is existing behavior. Enrichment then pre-empts the fork, so the branch order reads unchanged → merge → in-place (unused *or* enrichment) → fork.

**Merge takes two explicit ids, not an identity.** The repair case is precisely the one where the two packages do *not* match on identity — one has burrs recorded, the other does not. Identity matching cannot express it, and inferring a merge from near-identity would be guessing about someone's gear. Both ids come from `equipment_list`.

**Merge repoints and deletes rather than soft-deleting the source.** Once shots, bags and recipes have moved, the source has no referents, so keeping the row would leave a dead package in the inventory list for no reason. This is why the repoint must cover all three tables: missing one both strands a row and makes the delete unsafe. The `recipes` table is written by `RecipeStorage` into the same database file, and tables absent from the connection are skipped rather than failing the merge (each storage class creates its own table on first initialize, and the equipment tests create only what they exercise).

**The target is revived.** The package that ends up holding the history has to be selectable, and in the fork-undo case the target is the retired one. The same statement clears a supersession pointer aimed at the row about to be deleted, and the lineage update excludes the target so the survivor cannot end up superseded by itself.

**The whole merge is one `DbWriteTxn`.** A partial merge is the worst outcome available: shots on the target, the source deleted, bags stranded. Consistent with the identity-edit path, which already wraps for the same reason.

**The heal matches a lineage pair, not a similarity.** Migration 35 requires `older.superseded_by = newer.id` — the app's own record that it forked them — plus equal grinder brand/model, equal basket, equal puck prep, and burrs empty on the older side and set on the newer. The lineage requirement is what separates "the app split this grinder" from "this user owns two Mazzers and has only named the burrs on one". Alternatives considered: matching on identity similarity alone (rejected — it would merge two real grinders), and prompting the user at startup (rejected — the question is unanswerable without seeing the history it is about, and this fires once, before any UI exists).

A real burr swap where the old burrs were never named is indistinguishable from an enrichment fork, and is healed too. That is the same trade the enrichment rule itself makes, deliberately.

**The newer package survives.** It is in inventory, it carries the burrs the user just recorded, and new shots already point at it. Choosing the older one would mean re-retiring the package the user currently sees.

**The heal runs inside the migration's transaction, so the merge body had to be split.** `DbWriteTxn` refuses to nest by design (an inner rejection would leave partial writes staged in the outer transaction). `mergePackagesUnlockedStatic` is the body, `mergePackagesStatic` is that body wrapped in a transaction, and the heal calls the unlocked one — so a failure mid-heal rolls back with the migration and the version does not advance, leaving it to retry next launch.

**Chains collapse one link per pass.** Burrs named, then something else named on top, produces `A → B → C`; the scan re-reads lineage each pass and the loop is bounded at 10 so a cycle in `superseded_by` cannot hang startup.

**MCP level `settings`, matching `equipment_update`.** Merge is destructive but it is equipment bookkeeping, not machine control. The tool description carries the warning and instructs the caller to have the user name the surviving package; the tool itself will not guess.

## Risks / Trade-offs

- **A user really did swap burrs but never recorded the old ones, and now the old shots claim the new burrs.** → Accepted, and unavoidable: with the old burrs unnamed there was no recorded difference to preserve. The alternative — forking on every enrichment — is the reported bug.
- **Merge is destructive and an AI can call it.** → Two explicit ids required, no identity inference, refusals are hard no-ops, and the tool description states plainly that it is not undoable and that two genuinely different grinders must stay separate.
- **Merging packages that were legitimately distinct silently rewrites what history says about the gear.** → No mitigation beyond the above; this is the user's judgement to make, which is why it is not automated.
- **The heal merges a genuine burr swap whose old burrs were never named.** → Accepted, and identical to the enrichment trade above: with the old burrs unrecorded there is no difference in the data to preserve. It is also bounded — it only fires on a lineage pair the app itself created.
- **A heal failure mid-migration.** → The whole heal is inside the migration transaction and the version bump is gated on it, so a failure rolls back and retries next launch rather than half-merging and marking the schema done.
- **Forks the heal does not match stay split until someone notices.** → Accepted. `equipment_merge` exists so support can walk a user through the rest.
- **A future table gains `equipment_id` and the repoint list is not updated.** → The repoint list and the hard-delete pre-check are the two places that enumerate referencing tables; both are touched here and both name the same three tables, so a miss in one is visible against the other.

## Migration Plan

Schema version 34 → 35, data-only (no DDL). On first launch of the new build the heal runs once inside the migration transaction; a fresh database reaches it with nothing to match and pays a single indexed scan. There is no rollback path for a *successful* heal — the merged-away rows are gone — which is why the signature is narrow and the operation is logged per pair (`qInfo` naming both ids and the moved counts, so a user-submitted log shows exactly what was folded).

Behaviour going forward applies to edits made from the new build; anything the heal does not match is repaired on demand with `equipment_merge`.

## Open Questions

- Should a merge write a marker-registry log line rather than a bare `qInfo`, so the repair is retrievable per subsystem from a submitted log? The heal logs per pair today; the MCP tool reports counts in its response only.
