# Design

## Context

The common `PostShotReviewPage` autosaves each committed field, then `runStickySync()` sends a full set of sticky metadata to `RecipeContext`. Its guard compares part of the shot against live preferences, but the Change Beans dialog separately activates a bag before updating the shot. Bag activation clears brew overrides and applies bag fields; an ingredient swap can deactivate the recipe. An active recipe edit also refreshes the live dial. These paths explain #2064's reproduced 59-to-profile-target class of failure.

The page serves both native and Wasm, also opens from history and can step between shots. Autosave advances its in-memory baseline, and bag selection has a separate completion. `StackView.onDeactivating` also runs when a child covers the page, so it is not by itself an exit. A shot's recorded yield anchor is distinct from its measured output.

## Goals / Non-Goals

**Goals:** Preserve the shot's autosave and upload lifecycle; offer a transparent, guarded patch to the next setup; keep the active recipe consistent with an accepted bag correction; and retain equivalent behavior on native and Wasm.

**Non-Goals:** Change ordinary bean selection, recipe activation or Brew Settings behavior; offer historical-shot edits for carry-forward; turn measured output into a yield target; or use a timer to determine eligibility.

## Decisions

### Separate shot edits from the proposed patch

The review session retains a stable, per-shot opening snapshot independent of its autosave baseline. Compute net edits from that snapshot to the final saved shot, including the bag id and separately completed selector operations; undo or returning to the opening value removes a candidate. Only explicitly edited beans, dose, grinder setting/RPM, equipment and barista are candidates. Rating, taste, notes, measured output, TDS and EY are shot-only. A bag's saved settings are not candidates. If a carried dose changes a ratio-derived target, include the old and resulting target as a consequence, not as an independently edited target.

Remove automatic sticky sync on autosave and use shot-only bean selection for both latest and older shots. Do not infer which fields were edited from a later database reload: it would incorporate changes made elsewhere. Track successful saves and selector completions, and wait for the relevant pending saves or report failure before offering. The existing upload hold remains scoped to the shot.

### Compare with the shot, then revalidate

The offer is all-or-nothing eligible: latest shot and current next-shot setup equals its baseline. Capture the full baseline in memory at shot start, associate it with the saved shot, and discard or replace it on a newer shot. After restart, when that baseline is unavailable, capture the current setup when the latest shot opens for review; changes made before this fallback cannot be detected and the explicit summary is the user's review. Compare profile, recipe, bag, equipment, bean identity, dose, grind/RPM, barista, effective temperature, and yield anchor mode/value plus the relevant effective target. Equality is value-based, with explicit domain-appropriate numeric tolerances; changing a value back restores equality. A new shot or a concurrent difference suppresses the entire offer. Recheck when requesting an offer and when accepting it. While visible, withdraw the offer if its underlying setup diverges. Recheck latest-shot identity at each boundary.

Shot records do not necessarily expose every historical input with sufficient provenance. The in-memory baseline is separate from editable shot metadata. If it cannot be captured, suppress the offer rather than assume equality. The existing uncommitted `nextShotUnchangedSince` mode/value comparison and its test express an essential distinction, but are only a partial implementation: review them against the in-memory baseline, effective target, profile and temperature, and the redesigned host-side check. Preserve or adapt them only where correct; do not infer a ratio from grams.

### Host-owned preview and apply

Shared QML presents a host-produced, request-scoped proposal with concrete before/after values, resulting target and every persistent recipe/bag effect. The host owns the comparison and apply through an existing shared service, not separate native and browser paths. Use a revision or equivalent compare-and-set that covers every relevant setting and latest-shot identity; a revision is a stale-operation guard, while equality against the shot decides eligibility. An unrelated revision change must not manufacture a different proposed patch. Before committing, verify that the exact proposal remains valid; do not silently recalculate a different one after acceptance.

An accepted bean-only patch updates the selected bag identity and, when a recipe is active, the recipe's bag link without normal bag activation, recipe refresh or setter cascades importing bag dose, grind, equipment, temperature or yield. Existing dose/grind write-through may occur only for those fields explicitly included in the accepted patch and disclosed in the offer. Preserve anchor mode and value, not merely resolved grams. Keep the recipe active and its cache and dose rung consistent with the committed link.

Coordinate the recipe/bag persistent writes and in-memory publication as a single admitted operation: validate the shot, setup and record preconditions at the commit boundary, stage database work off the UI thread, and publish only if the approved state is still current. Serialize competing mutations through the same owner or otherwise provide a tested transactional protocol that prevents stale or partial writes; a naive snapshot/restore around ordinary bag activation can overwrite another client's changes. A machine operation must not wait for a user decision. Report failures and leave the saved shot intact; never claim success for a partially applied proposal. Request IDs, disconnect handling and record preconditions follow the shared-service conventions.

### Explicit leave intent

Route Back, Android Back/Escape, auto-close and stepping to another shot through a single leave-intent coordinator. Flush pending input and shot saves first. No candidates or an ineligible setup proceeds directly. An eligible offer waits for Keep or Apply, then resumes the original navigation once. Auto-close itself never accepts. A new machine operation, shutdown or lost session abandons the offer with no apply, and must not obstruct operation navigation. Pushing a child page is not a final exit and must not trigger the question. Handle shot deletion and navigation races without replaying an old offer.

## Risks / Trade-offs

- **Restart loses shot-start provenance** → use the user-approved review-entry baseline, disclose actual proposed values, and test changes before and after that baseline separately.
- **Partial state or stale writes across asynchronous storage and live settings** → make the host apply protocol and its failure/competing-writer tests a prerequisite, not a UI-only guard.
- **Recipe re-pointing is persistent** → name the recipe and old/new bag in the offer; do not change the recipe on decline or suppressed offers.
- **Extra confirmation after an unattended timeout** → show it only for net eligible corrections and let machine operations take precedence.
- **Prior uncommitted guard change appears complete** → its comparison is retained as evidence and evaluated during implementation, but the old automatic sync and picker path still need replacement.

## Migration Plan

No data migration is needed. The session-only snapshot is replaced by a review-entry baseline after restart. The manual explains the confirmation in a short entry. Rolling back restores prior behavior and does not require reversing stored data.
