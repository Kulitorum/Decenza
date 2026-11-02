# Tasks

## 1. Water amount contract and compatibility

- [ ] 1.1 Add the canonical fixed/ratio hot-water specification helper (design D1): normalize legacy fixed/name-only blocks, parse `amountMode` and `waterToEspressoRatio`, reject conflicting/unknown/non-finite values, and resolve/round against the espresso target using existing water bounds. Extend `tst_recipestorage` with meaningful cases for 2.5×40=100, 2.5×41=103, fixed ml, missing yield, invalid ratio and out-of-range results; verify through Qt Creator and break the resolver to confirm a failing case.
- [ ] 1.2 Apply that contract at recipe storage and shared-service write boundaries, retaining full editable snapshots/preconditions. Preserve ratio JSON in clone and backup/transfer without a SQL migration. Verify storage/service round trips for custom 2.7×, rejection without partial updates, and unchanged legacy fixed/name-only behavior using existing test targets.
- [x] 1.3 Update `docs/CLAUDE_MD/RECIPES.md` to describe fixed versus recipe-only ratio water, units, whole-gram resolution and preset isolation. Verify that no remaining model description says all water snapshots must contain only a preset's fixed amount.

## 2. Host resolution and machine application

- [ ] 2.1 Add the single host recipe-water resolver/application path and share the existing live yield projection rather than duplicating its ladder. Move activation water resolution after queued dose/yield establishment; initialize on restoration and committed active-recipe edits. Extend existing recipe-wiring/profile/machine-control tests to verify switching from 50 g to an 18 g, 1:2 recipe applies 90 g water, restart restoration, and a ratio-only edit updates water at unchanged espresso yield.
- [ ] 2.2 Subscribe to effective espresso-target changes, including dose-driven yield changes, overrides and deliberate espresso target bumps; deduplicate unchanged inputs and defer changes during water dispensing until the drink ends. Verify 40→42 g gives 100→105 g water, an absolute yield ignores dose-only changes, repeated unchanged drinks do not trigger resolution, and actual shot output/tare/settling do not affect water. Use existing integration test targets with observable settings/resolution events.
- [ ] 2.3 Keep the resolved amount in effective machine settings, retain the global fixed preset, preserve the ratio on same-vessel re-selection, and keep different-vessel deactivation. Verify with existing machine-control/preset and wiring tests that a shared 200 g vessel can pour recipe water at 100 g, stays 200 g in standalone editing, and returns to standalone behavior after a different vessel is selected.
- [ ] 2.4 Route manual and sequenced hot-water starts through the same recipe-water readiness validation, including the sequencer's direct device request. Reject unusable results with a clear reason and no stale target; recreate a missing ratio vessel as a fixed preset at resolved grams. Verify no hot-water start is dispatched for an invalid result, correction restores readiness, and the one-tap sequence's existing timing/firmware tests remain valid.
- [x] 2.5 Extend the brewing section of `RECIPES.md` with the event-driven update rule and actual-output distinction. Verify its activation ordering and target source match the implemented host path.

## 3. Shared ratio and vessel editors

- [ ] 3.1 Extract the common ratio chooser presentation from `RatioPresetDialog.qml` into one shared control, retaining the espresso wrapper's public API, preference editing, help and pick-only/live-apply semantics (D3). Register new QML for native/Wasm and update the active shared-UI inventory. Verify the Qt Creator build/QML inventory checks and exercise the existing espresso widget, Brew Settings picker and recipe wizard picker without changing their presets or application behavior.
- [ ] 3.2 Add the water wrapper with 2×, 2.5× and 3× choices plus an editable custom ratio with 0.1 steps, current-value highlighting and a derived added-water preview. Verify custom 2.7× survives reopening, the preview is 108 g at a 40 g target, no incorrect preset highlights, and water edits do not write espresso preferences or arm its anchor.
- [ ] 3.3 Extract shared vessel editor fields and full-row revision-checked preset submission from the existing Hot Water flow. Keep standalone weight/volume editing fixed-only, seed new standalone vessels at 100 g, and preserve saved values on edit. Extend `tst_recipecontextservice` for full-row saves, duplicate/blank names, stale revisions and request isolation; verify native screen behavior including fixed volume and cancellation.
- [ ] 3.4 Wire visible Add vessel and Edit actions into the recipe water window and retain amount specification in draft load/save, tiles and summaries. Implement D2's separate fixed-preset save and recipe-snapshot update, new 2.5× default, fixed fallback for profile-less tea and draft-target preview. Verify add/edit/custom ratio/mode switching, existing fixed recipe edits, missing-preset recreation, cancel-before-save, cancel-recipe-after-preset-save, and no implicit live vessel selection while designing a recipe.
- [ ] 3.5 Add translated labels, accessible activation/focus order and keyboard commit handling to the shared controls. Update the wiki manual's Recipes and Hot Water entries in the separate wiki repository with concise instructions for vessel management, editable ratio choices, when water updates and the 100 g standalone default. Verify keyboard/screen-reader navigation in both shells and review the manual wording against the finished UI.

## 4. API and shot-history round trips

- [ ] 4.1 Extend MCP hot-water schema/conversions and HTTP recipe handling with the amount discriminator/ratio, preserving fixed-field compatibility and rejecting invalid/conflicting writes through the canonical validator. Update the retained HTML form to preserve ratio fields on unrelated edits. Update MCP documentation and `McpSurfaceVersion`. Verify MCP/HTTP create/read/update cases and a name-only form edit retaining 2.7×; run the relevant existing tests and MCP budget gate through the repository's Qt Creator workflow.
- [ ] 4.2 Capture the recipe water specification and `resolvedTargetG` separately from actual dispensed water, using the water step's target and operation/UUID-keyed result join (D5). Preserve the ratio through both promotion paths and distinguish skipped water from dispensing. Extend existing shot-water, recipe-promotion and storage tests for both sequence orders, water arriving before/after the shot save, a target change before water starts, and a later recipe edit that cannot rewrite the captured values.
- [ ] 4.3 Update recipe readouts and shot-facing projections to show ratio plus resolved target where appropriate without presenting planned grams as measured water. Document the added JSON fields and history semantics in `RECIPES.md` and the existing API/agent-file documentation. Verify browsing uses the recipe's own yield context, active views use the effective target, and legacy fixed shots display as before.

## 5. Integration acceptance

- [ ] 5.1 Build and run the full suite through Qt Creator, including the QML lint and source/resource closure gates and focused new regression cases. Verify all pass; perform representative break checks for the shared amount resolver and target-change integration rather than adding duplicate test targets. Record concrete results and any held checks honestly.
- [ ] 5.2 After the user starts/restarts the intended native app, verify the recipe and standalone flows on native and the matching host-served browser: add/edit ratio, custom choice, fixed-only standalone, live target update, stale/conflicting save, disconnect with an unsaved draft and refresh after reconnect. Verify equal UI behavior and no mutation replay; record outcomes.
- [ ] 5.3 Validate Americano and long black water targets using the simulator and available DE1 hardware with supported firmware: 40 g/2.5× gives 100 g, 42 g gives 105 g, unchanged repeated drinks reuse their target, and target changes during water do not move that pour. Verify actual espresso variance does not alter the water amount, standalone remains fixed and saved history/promotion retain the ratio. Record any hardware check held for the user without marking it passed.

## Workflow follow-up

- Once implementation, documentation and required review/validation are complete, archive this change with the CLI before a requested merge, following `CLAUDE.md`.

## Validation evidence — 2026-11-02

- Qt Creator build `1793559335093` succeeded for the native app and packaged Wasm client, including the QML lint and resource gates. The full test run `1793559335329` passed all 207 tests with no failures, skips or test warnings (40.05 s).
- The resolver break check deliberately replaced whole-gram rounding with truncation. Run `1793559335326` failed the 41 g × 2.5 case (102 instead of 103); rounding was restored before the passing full suite.
- Added regression coverage exercises custom-ratio storage and rejected updates, shared yield/amount previews, configured yield while a shot target is latched, blocked manual/sequenced water starts, promotion, and planned water target versus measured output.
- Native and host-served browser checks confirmed the 100 g fixed-only standalone default, recipe add/edit, the 2×/2.5×/3× chooser, custom 2.7× persistence, and 97 g at a 36 g target. A stale browser preset save was rejected after a native edit, retaining the browser draft.
- Wiki manual edits are prepared in the separate local wiki checkout; they are not part of this repository's PR or published yet.
- Remaining acceptance includes the final rebuilt dialog appearance, the full espresso-picker regression matrix, host activation/restoration and target-change observations, disconnect/reconnect with an unsaved draft, and simulated/hardware Americano and long black sequences. API/HTML and history paths also need the complete end-to-end matrix specified above. No dispensing was started during the live UI checks; the local session reported simulation mode.
- Tasks whose implementation is present but whose complete verification is outstanding remain unchecked. Keep the PR in draft and retain this change unarchived until those obligations are reconciled.

## Review corrections — 2026-11-02

All nine findings from the read-only review have been addressed:

- Cached recipe water is reapplied after fixed preset commits without rewriting the preset or repeating the ratio calculation. Ratio-to-fixed edits apply the fixed snapshot immediately.
- Activation dose/yield writes share a generation and keep ratio resolution pending until completion. Superseded activations receive a failure result and their queued writes cannot alter the newer setup.
- Storage keeps the installed-profile target through async and static validation; clone/import can defer a profile fallback that is unavailable to the static reader.
- Legacy name-only editor snapshots use the same preset fallback as the host. Pending vessel saves freeze the submitted snapshot and disable form editing. Switching to profile-less tea converts water to fixed grams, and fixed-only dialogs cannot retain ratio mode.
- Long-black starts check the same device guard before arming sequence intent or reporting success.
- Qt Creator native/Wasm build `1793559335097` passed. Full test run `1793559335333` passed all 207 tests with no failures, skips or test warnings (42.53 s). Targeted storage, preset, device, activation-queue and shipping QML-handler regressions pass, including frozen save values, legacy tiles/save JSON and tea conversion.
- The first new activation-queue test exposed that its previously event-free test executable had no application event loop; it now uses `QTEST_GUILESS_MAIN` and the queued callbacks execute normally. No assertion was relaxed.
- A user-started restart has been requested for the updated app. No new live UI, reconnect, simulated dispensing or hardware acceptance result is claimed here; the broader outstanding acceptance above remains pending.
