# Tasks

This checklist supersedes the earlier implementation checklist after the 2026-11-02 design reassessment. Existing build/live evidence below is retained as history; it does not verify the revised state ownership or newly added scenarios. Keep the current local fixed-water correction while refactoring.

Acceptance scope: the user selected simulator-only validation on 2026-11-02. Earlier hardware holds in the historical evidence below are superseded; they are not release requirements.

## 0. Design review gate

- [x] 0.1 Have an independent agent review the revised proposal, three delta specs, design and tasks against production call paths before any implementation edit. Record findings and resolve/re-review actionable gaps; verify the reviewer has no remaining design blockers and OpenSpec strict validation passes.

## 1. Canonical amount and recipe validation

- [x] 1.1 Consolidate WaterAmount parsing/normalization into the explicit fixed-weight, fixed-volume, ratio, disabled and legacy-reference cases (D1), sharing resolution between preview and host. Add tests requiring a vessel for fixed and ratio recipe water and rejecting malformed discriminators, conflicting inputs, invalid ratios and unsupported results while preserving legacy fixed/name-only behavior, custom 2.7× and whole-gram rounding. Run the existing focused targets through Qt Creator.
- [x] 1.2 Route create, merged update, clone, promotion and static import through one complete-candidate water validator with explicit known/unavailable profile context; remove field-trigger lists and duplicate validation decisions. Verify HTTP/native/MCP errors and tests for bagId-only/profileJson-only edits, installed-profile precedence, known-invalid targets, missing import context and no partial writes.
- [x] 1.3 Update RECIPES.md and API documentation for the canonical input/derived-data boundary and import exception. Verify described behavior against the same production validator; preserve fixed volumeMl MCP compatibility and unrelated legacy HTML edits.

## 2. One recipe-water state owner

- [x] 2.1 Add the small framework-free RecipeWaterState and one complete WaterPlan representation (D3). Move next-setup inputs/readiness, the frozen current-drink specification/pending plan, its scoped target adjustment and water-cycle capture out of parallel MainController caches. Remove superseded fixed/ratio paths and settings-send repair callbacks as the state owner lands. Test production transitions for fixed/ratio edits, ratio→fixed→ratio, unchanged-repeat reuse and invalid→valid recovery.
- [x] 2.2 Adapt queued activation, restoration and active-recipe edits to publish coherent setup inputs only after the current dose/yield generation is complete. Verify A→B activation at 18 g / 1:2 / 2.5× produces 90 g water, superseded requests terminate without stale writes, and restoration/fixed-only edits use the same state path.
- [x] 2.3 Use the one settings adapter for cached fixed/ratio plans, explicit preset commits and same/different vessel selection. Verify a shared 200 g row remains 200 g while recipe water is 100 g, an unrelated preset edit does not alter ownership, replacement/deactivation/selection during a drink defers next-setup writes without erasing the current plan, missing presets are recreated fixed, and settings transmission cannot recursively resolve water.
- [x] 2.4 Connect configured-target changes and accepted espresso bumps as explicit events (D4), scoped to the current operation; distinguish same-drink target edits from whole-setup replacement and clear a temporary adjustment on that drink's completion/abort without leaking it into the next setup. Test dose-relative versus absolute targets, bump up/down back to baseline, no measured-output/tare/settle input and no recalculation on unchanged repeats.
- [x] 2.5 Document the state owner, settings adapter and event boundaries in RECIPES.md. Verify MainController retains event/I/O integration without a second authority for recipe water.

## 3. Explicit editor effects

- [x] 3.1 Simplify the vessel editor/caller result contract (D2): request-correlated confirmed saved row, frozen submitted snapshot, preset persistence separated from recipe-draft replacement and live selection. Verify stale/conflicting/disconnected saves retain drafts with no mutation replay, including another client's concurrent preset-list change.
- [x] 3.2 Preserve live selection when editing an unselected standalone vessel; apply selected fixed edits only under the defined ownership policy. Make clicking an already selected amount mode a no-op and retain custom ratio draft values across mode switches. Exercise real shared QML controls or an existing interaction fixture for Cup/Mug editing, fixed 150 ml, new 100 g, 2.5× default and custom 2.7×.
- [x] 3.3 Retain the common ratio chooser and thin espresso/water wrappers; verify espresso preset editing, pick-only/live-apply behavior and water choices do not share effects. Check native/Wasm source/resource inventory and translated labels, keyboard commit and accessible focus through the repository workflow.
- [x] 3.4 Update the concise wiki Recipes/Hot Water instructions in the separate wiki checkout to match finished save/selection behavior. Verify the text covers the editable ratio and standalone 100 g default without expanding into design documentation.

## 4. One captured water plan

- [x] 4.1 Freeze the complete plan on actual HotWater entry before next-setup refresh callbacks (D4), including physical starts; retain it through the current drink's water ownership. Do not add a standalone pre-entry hold or change existing sequencer timeout/arming semantics. Verify eligible target edits before entry can update the pending plan, changes during pouring cannot retarget that step, and silent non-start/local refusal/abort do not strand state.
- [x] 4.2 Use the captured plan for machine application and the existing operation/UUID-keyed history join (D5), with separate actual water results. Freeze the drink's water specification at setup while allowing explicit espresso-target adjustments before Americano water. Verify both sequence orders, later recipe edits, skipped/refused water, and water finishing before/after shot save; no independent history target cache.
- [x] 4.3 Add at least one integration fixture through the shipping state/settings/start/history wiring (D6), supplementing the focused pure-state tests. Demonstrate a representative ownership regression fails when deliberately broken and passes after restoration; copied or regex-extracted handlers alone do not satisfy this task. Update history documentation in the same step.

## 5. Integration acceptance

- [x] 5.1 Build native and packaged Wasm and run the full suite through Qt Creator, including lint/source/resource and relevant API gates. Record new evidence for this refactor; prior 207-test passes remain historical.
- [x] 5.2 After a user-started restart, verify the intended checkout and matching host-served browser: add/edit both modes, custom ratio, selection/unit preservation, active recipe edits, live target changes, failed/stale saves, reconnect without replay and restoration. Restore original settings and remove only owned validation data.
- [x] 5.3 Validate both sequence orders with the DE1 and scale simulators: 40 g / 2.5× → 100 g, 42 g → 105 g, unchanged repeats, delayed starts, changes during water and independent actual-output/history results. Record simulator evidence and leave any unavailable live checks explicitly held; physical hardware is not required for acceptance.
- [x] 5.4 Review the resulting implementation and the latest PR review comments, address actionable findings, rerun affected checks and update PR #2170 with the simplified design, new evidence and honest remaining acceptance.

## 6. Final review corrections

- [x] 6.1 Give active-recipe refreshes the same coherent dose/yield boundary as activation. Verify a water-only recipe edit cannot replace a running drink's temporary target, while explicit live target edits still update pending water.
- [x] 6.2 Preserve ratio-yield recipes whose dose/profile fallback is unavailable during static import. Test importing a live-valid title-only recipe without weakening known-invalid target rejection.
- [x] 6.3 Track explicit standalone water settings in the next setup, ignoring notifications from the settings adapter itself. Verify overrides survive pouring and preset values remain independent.
- [x] 6.4 Reject explicit invalid fixed amounts before legacy fallback, retain old name-only/zero-placeholder interpretation, and cover atomic storage rejection and editor hydration. Build native/packaged Wasm, run focused and full Qt Creator tests, and update PR #2170 with the results.

## 7. Follow-up review corrections

- [x] 7.1 Save selected standalone vessel edits through the host commit path, retaining recipe ownership on rename; preserve unselected and recipe-draft save behavior. Add regressions through the shipping editor and host/controller paths.
- [x] 7.2 Include enabled installed-profile dose recommendations in complete-candidate water validation. Cover valid zero-profile-target recipes, invalid resolved amounts, dose precedence and unavailable-context import behavior.
- [x] 7.3 Remove the unbounded parallel water-snapshot map by sharing the existing bounded operation snapshots. Cover aborted-cycle retention and delayed history association, build native/Wasm, run focused/full tests and update PR #2170.

## 8. Final GitHub review correction

- [x] 8.1 Capture water-validation defaults from typed installed ProfileInfo metadata, not the lightweight availableProfiles picker projection. Reproduce the missing dose/target through the real ProfileManager catalog and async recipe create/update/clone, rerun native/Wasm and full tests, resolve the reviewed thread, and re-archive before merge.
- [x] 8.2 Include the enabled installed or embedded profile dose as the final fallback in the shared recipe editor's yield preview. Cover recipe/bag precedence, disabled recommendations and the shipping QML call, build native/Wasm and run focused/full tests, then resolve the review and re-archive before merging.
- [ ] 8.3 Use the embedded profile's enabled dose during import validation and preserve recipe-owned water when its shared vessel is deleted. Reuse existing validation/application paths, add one focused regression for each finding, build native/Wasm, run the full suite, resolve the findings and re-archive before merge.

## Workflow follow-up

- Once implementation, documentation and required review/validation are complete, archive this change with the CLI before a requested merge, following CLAUDE.md. No merge or archive is part of the design revision.
- Preserve the evidence below as historical results from the earlier implementation.

## Completed simulator acceptance — 2026-11-02

- Used the user-rebuilt native app from this checkout (PID 55032, host port 8888) and its matching packaged Wasm client in the internal browser. The DE1 and scale were simulated, with automatic uploads disabled. Edge was not exposed by the connected browser runtime; the internal browser supplied the browser acceptance.
- Native custom 2.7× saved/reopened at 108 g for a 40 g target, with no suggested card incorrectly selected. Vessel Save left the recipe/live target unchanged until recipe Save, and the shared fixed preset remained independent. New recipe vessels showed 2.5× / 100 g; an explicitly saved new preset survived discarding its enclosing recipe.
- Native and browser standalone additions showed fixed-only 100 g. Saving an unselected Mug edit (350→360 g) kept the selected recipe vessel and its effective 100 g; Mug was immediately restored to 350 g. Editing the selected shared preset from 200→180 g kept the recipe at 108 g. An active fixed recipe edit applied 150 ml, and clicking the browser editor's already-selected Fixed amount retained 150 ml. Compiled production QML controls also cover that no-op and the custom 2.7× → fixed 108 g → 2.7× round trip.
- Espresso preset editing changed Normale 2.0→2.1 and back through the real chooser. The recipe wizard's 1:3 pick changed only its draft to 54 g, while live espresso remained 40 g. Brew Settings applied 1:2 only after confirmation, producing 36 g espresso / 90 g water. Water choices left espresso presets at 1/2/3. Existing keyboard/commit/accessibility regressions and native/Wasm source/resource/lint checks passed in the full suite; both shells exposed the expected translated control names and initial name-field focus.
- A browser vessel save became stale after another client's preset-list update: Save failed with a recoverable message and retained the custom draft. A loopback TLS pass-through then disconnected only the acceptance browser: its unsaved 2.7× / 108 g draft remained visible, Save was disabled, and reconnect re-enabled it without replaying a write. Cancel preserved the saved host state. The browser tab and relay were closed afterward.
- Five simulated drinks exercised two Americanos and three long blacks. The 4:42 PM Americano captured 100 g at 40 g / 2.5× (108.7 g measured water). The 4:43 PM Americano captured 105 g at 42 g / 2.5× (115.9 g measured); changing the configured target to 40 g during water kept that pour at 105 g and applied 100 g after completion. Long blacks at 4:44 PM, 4:46 PM and 4:47 PM captured 100 g before espresso (107.8, 108.5 and 107.4 g measured). The last two used unchanged inputs and retained the same 100 g target. Times are the local Shot History handles for the owned simulator records, which were subsequently removed.
- Editing the stored ratio to 3× during the first long black left that drink's capture/specification at 2.5× / 100 g, then applied 120 g to the next setup after completion. All five history rows kept ratio, captured target and measured result distinct. Clone and shot promotion retained 2.5×; a conflicting HTTP ratio-plus-fixed write returned 400 / invalidWaterAmount atomically. Delayed entry, explicit bump up/down, refused starts and both history completion orders are covered by the passing production-controller/sequence/history fixtures, including waterRuntimeRejectsInvalidAndRecoversWithoutPendingHold and waterRuntimeKeepsFrozenSpecAndActualTarget.
- Removed only the five owned simulated shots, three owned recipes and two owned vessels through guarded APIs; recipe reads then returned 404. Verified the original active recipe 92, original 19-shot count, exact three-preset list/selection, 36 g espresso / 200 g water, 85°C, 4 ml/s, ratio presets 1/2/3, simulation mode and upload setting against the captured baseline. The original recipe's unnamed water block was left unchanged, with no automatic matching. Native and browser Home showed the restored original recipe; the browser acceptance tab was then closed.
- Published the two concise manual sections in the separate wiki repository as 8215637, covering required vessels, editable recipe ratios and the standalone fixed 100 g default. Only this feature’s sections were published; unrelated local wiki edits were left untouched.
- No implementation edits were needed during this acceptance pass. Native/packaged Wasm build 1793559335142 and full Qt Creator run 1793559335357 remain the applicable code checks: 207 passed, zero failed/skipped/reported test warnings (40.83 s). Read-only test results confirmed the same pass. All PR review threads remain resolved; no newer Codex review of code commit 746fadfcf was available. Physical hardware is outside the user-selected acceptance scope.

## PR update — 2026-11-02

- Pushed the reviewed refactor and fixes as 746fadfcf to PR #2170, which remains open and ready for review. Updated its description with current validation and outstanding live/hardware acceptance.
- GitHub text-invariants run 41541045011 passed on that code commit. All existing inline review threads are resolved; no new Codex review of this commit was available at this check.
- Restored the original active recipe (92) without rewriting its saved vessel block or performing automatic matching. Verified the original setup: 36 g espresso, wewe selected, 200 g water, 85°C and 4 ml/s; preset list and original shot count match the baseline. No test shots or recipes were created in this live pass. Closed the temporary browser tab and removed the two owned temporary files.

## Post-refactor review corrections — 2026-11-02

- Corrected the distinction between an active recipe and an enabled recipe water block: standalone selected-preset edits apply for espresso-only recipes, including absent or disabled water blocks.
- RecipeWaterState now retains application failures as readiness errors. A failed preset recreation/application refuses both guarded water starts and stale amounts; successful reapplication clears the error. Canonical vessel-name trimming prevents whitespace-only lookup differences from causing duplicate creation attempts.
- Added production-controller regressions for espresso-only preset editing, refused application with stale 200 g settings, successful recovery to 100 g, and whitespace-normalized vessel lookup. These supplement the actual QML mode-button and operation/history transition tests.
- Native and packaged Wasm build 1793559335142 passed; warnings were local OpenSSL deployment-target and linker compact-unwind warnings. Focused run 1793559335356 passed all four selected targets. Full run 1793559335357 passed all 207 tests with no failures, skips or reported test warnings (40.83 s).
- Final full native/browser interaction and supported-hardware acceptance remain outstanding. The app was user-restarted for the preceding required-vessel build; these final backend corrections have not been claimed as live-validated. No unnamed-vessel fallback, automatic vessel matching or bulk recipe rewrite was retained.

## Simplified implementation verification — 2026-11-02

- Replaced the parallel MainController fixed/ratio caches and settings-send repair path with RecipeWaterState and its production RecipeWaterController event/settings adapter. One resolved plan handles fixed and ratio settings; a current drink keeps its own specification/pending target and actual-entry capture separately from the next setup.
- Complete-candidate storage validation now uses explicit installed-profile context; every update is checked, including bag/profile edits. Supplied amount modes are type checked and derived targets are removed from writable snapshots.
- Preset saves return a request-correlated confirmed row. Editing an unselected vessel preserves selection, selected preset edits preserve recipe ownership, and the production WaterAmountModeSelector preserves fixed ml and custom ratios across mode switches. Its actual buttons run in the compiled QML interaction tests.
- Per the user's clarification, every enabled recipe water block requires a nonblank vessel. The attempted unnamed/current-selection compatibility fallback was removed before the final validation. No automatic vessel matching or bulk data rewrite is included.
- Native/Wasm build 1793559335138 passed. Full Qt Creator run 1793559335354 passed all 207 tests, with no failures, skips or reported test warnings, in 42.97 s. OpenSpec strict validation and whitespace checks pass.
- Production integration break check 1793559335352 failed at 120 g versus the expected retained 100 g when the current drink was deliberately overwritten by the next setup. The break was restored, and the subsequent full runs passed.
- Before the final required-vessel rebuild, native and matching host-served browser controls showed the fixed-only 100 g standalone default; saving the native unselected Mug retained the selected wewe vessel and 200 g settings. These do not yet constitute the final live matrix. A user-started restart was requested for the final cleanup build.
- Wiki wording is updated in the separate local wiki checkout and is not published. Hardware acceptance and remaining live editor/conflict/reconnect checks remain unchecked below.

## Design reassessment and independent review — 2026-11-02

- Revised proposal, design, all three delta specs and this checklist before implementation changes. The current local fixed-water correction and its regression test predate this revision and were preserved.
- The independent reviewer found two actionable lifecycle gaps: an accepted-request hold had no terminal event for silently refused standalone starts, and replacing/deactivating a recipe could erase the current drink's pending plan/capture.
- Removed the added pre-entry hold; capture now begins at actual HotWater entry and existing sequencer timeout/arming policy stays unchanged. Defined next setup, current drink specification/pending plan and water-cycle capture as separate values owned by one state object. Selection/replacement changes defer water-setting application while the current drink retains ownership.
- A follow-up review requested two wording clarifications for the editor-effects table and the no-drink standalone-selection scenario. Both were applied. Final independent review: **No findings; ready for implementation.**
- OpenSpec strict validation and git diff whitespace checks pass. No application builds, tests, implementation edits, commits or pushes were performed during this design revision. Production-wiring tests and matching native/Wasm acceptance remain implementation obligations.

## Validation evidence — 2026-11-02

- Qt Creator build `1793559335093` succeeded for the native app and packaged Wasm client, including the QML lint and resource gates. The full test run `1793559335329` passed all 207 tests with no failures, skips or test warnings (40.05 s).
- The resolver break check deliberately replaced whole-gram rounding with truncation. Run `1793559335326` failed the 41 g × 2.5 case (102 instead of 103); rounding was restored before the passing full suite.
- Added regression coverage exercises custom-ratio storage and rejected updates, shared yield/amount previews, configured yield while a shot target is latched, blocked manual/sequenced water starts, promotion, and planned water target versus measured output.
- Native and host-served browser checks confirmed the 100 g fixed-only standalone default, recipe add/edit, the 2×/2.5×/3× chooser, custom 2.7× persistence, and 97 g at a 36 g target. A stale browser preset save was rejected after a native edit, retaining the browser draft.
- Wiki manual edits are prepared in the separate local wiki checkout; they are not part of this repository's PR or published yet.
- Remaining acceptance includes the final rebuilt dialog appearance, the full espresso-picker regression matrix, host activation/restoration and target-change observations, disconnect/reconnect with an unsaved draft, and simulated/hardware Americano and long black sequences. API/HTML and history paths also need the complete end-to-end matrix specified above. No dispensing was started during the live UI checks; the local session reported simulation mode.
- Tasks whose implementation is present but whose complete verification is outstanding remain unchecked. Retain this change unarchived until those obligations are reconciled.

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
- GitHub's text-invariant check found an existing-key conflict in the new dialog's flow label (`Flow rate` versus `Flow Rate`). The dialog now uses the established English fallback. Native/Wasm build `1793559335102` passed after that label-only correction; the functional test result above remains applicable.

## Live validation after restart — 2026-11-02

- Verified the port owner was the main checkout's build `1793559335102`; the session reported the simulated DE1 and Simulated Scale. The standalone dialog has an opaque background, starts fixed at 100 g, and focuses the name field. A new recipe vessel previews 2.5× / 100 g before a name is entered.
- Native and matching host-served Wasm controls both showed 2× / 2.5× / 3× and accepted a custom 2.7× with a 108 g preview at 40 g espresso. Browser keyboard Up steps worked, and the custom selection removed the incorrect 2.5× current marker. Inspection drafts were canceled.
- Live host checks: 40 g / 2.5× applied 100 g; editing yield to 42 g applied 105 g; switching to fixed applied 150 g; restoring ratio applied 100 g. A standalone preset commit to 107 g left the active recipe targeting 100 g, while standalone editing retained its own fixed value.
- Switched from a 20 g dose / 50 g espresso setup to 18 g / 1:2.5 with a missing water vessel. Activation finished at 45 g espresso / 113 g water, and the recreated shared preset was fixed at 113 g. A water-ratio-only edit to 3 applied 135 g without changing espresso yield.
- A legacy name-only block applied its named preset after activation; the browser tile and fixed editor both showed 107 g / 86°C. Changing to a different vessel intentionally deactivated the recipe before reactivation. Two concurrent HTTP activation calls both returned terminal success responses in under 0.5 s; this pair did not exercise the superseded-failure branch, which remains covered by the queue regression test.
- Removed the two temporary recipes and both validation vessels through the guarded local APIs. Restored the original recipe and standalone vessel at 36 g espresso / 200 g water, 85°C and 4 ml/s, and returned the native app home. No dispensing was started, and no shots were created.
- This closes the restart-dependent editor and host checks listed here. Ratio restoration after restart, disconnect/reconnect with a draft, the full API/history matrix and actual simulated/hardware drink-sequence acceptance remain outstanding; their aggregate tasks are not marked passed. All 207 automated tests and GitHub text-invariant run `41507686479` passed before this evidence-only update.

## Final acceptance follow-through — 2026-11-02

- PR #2170 is ready for review. Codex's review of `53cf4b6` completed with no findings after the draft was marked ready.
- The local DE1 and scale were simulated; automatic uploads were already disabled. Four simulated drinks exercised water after espresso and water before espresso. A 40 g / 2.5× Americano recorded a 100 g water target separately from 107.6 g measured water. The long black joined its earlier 100 g water result to the correct espresso and promoted with its ratio intact.
- At 42 g / 2.5×, changing the next recipe yield to 40 g during water kept that pour at 105 g, then applied 100 g for the next drink. A deliberate espresso target bump from 40 to 45 g updated pending water to 113 g. History retained its setup plan separately from the water step's used target and measured result; later editing the recipe to 2.7× left all four captured 2.5× specifications unchanged.
- A loopback TLS pass-through disconnected only the test browser. Its unsaved vessel name and 2.7× / 108 g draft survived; Save was disabled offline. Reconnect refreshed host state and enabled Save without replaying a write. Cancel left the saved recipe and shared vessel unchanged.
- MCP create/read/update and HTTP read/update/clone preserved ratio fields. A conflicting MCP ratio-plus-fixed write rejected the entire update. A name-only save through the retained HTML form preserved custom 2.7×.
- Extended the existing storage round-trip case to import the custom ratio through the backup/transfer path. Deliberately discarding imported water JSON and substituting preset amounts for resolved targets made both focused targets fail in Qt Creator run `1793559335334`. Restored production code before native/Wasm build `1793559335105` and full run `1793559335335`: 207 passed, zero failed/skipped/test warnings (44.98 s).
- After the user-started restart, verified the main checkout owned port 8888 and restored the active recipe at 40 g espresso / 2.7× / 108 g water. Removed all four temporary simulated shots, all three temporary recipes and the test vessel through the guarded APIs, then verified their reads returned 404. Restored the original active Americano Test recipe and wewe vessel at 36 g espresso / 200 g water, 85°C and 4 ml/s; native Home shows the original shot plan. The browser relay and test tabs were already closed. Physical-machine acceptance is not claimed: this checkout runs the simulator and the connected machine tool targets a different Android installation.

## Additional Codex review corrections — 2026-11-02

- The review of `26bf8f3` identified two further issues. Clone now captures the installed-profile yield before dispatching storage work, validates against it and passes it into the insert; unknown-target deferral remains available for static import.
- Storage validation failures retain a recognizable cause and the original message. HTTP create/update/clone/promotion and archive/restore paths return client error 400 with `invalidWaterAmount`; MCP returns the same code/message. Name conflicts retain their existing handling.
- Regression run `1793559335336` reproduced both findings: cloning succeeded after its installed-profile fallback changed to an out-of-range target, and validation failures lost their client-visible classification. The tests also assert request-token retention and atomic rejection of invalid updates.
- Native/Wasm build `1793559335109` passed after both fixes. Full Qt Creator run `1793559335338`: 207 passed, zero failed/skipped/test warnings (45.99 s). No further dispensing or UI changes were needed; the additional backend changes are verified by storage/signal/response regressions and the full build.
- The subsequent review of `c489efa` identified a locally refused water start leaving the sequencer waiting for its generic timeout. Device start methods now return whether a request was made; the sequencer ends a locally refused step immediately and records `notDispensed`. Run `1793559335339` reproduced the active-sequence failure before the fix. The existing firmware-refusal timeout remains for requests that were actually sent.
- Native/Wasm build `1793559335111` and full run `1793559335340` passed after the refusal fix: 207 passed, zero failed/skipped/test warnings (42.69 s).
- The review of `0f560bfb` identified two target-tracking issues. The shipping target-change handler now overwrites an earlier bump when the live target returns to baseline. History aligns ratio `resolvedTargetG` with `MachineState`’s captured water-start target during both initial save and the operation/UUID-keyed late result update; the ratio stays frozen, fixed snapshots are unchanged and skipped water retains its plan. The late update changes the result and captured target in one transaction.
- Run `1793559335341` reproduced both defects: the shipping handler kept 45 g after a return to 40 g, and persisted ratio targets stayed at 100 g for both completion orders instead of 105/113 g. Native/Wasm build `1793559335113` and full run `1793559335342` passed after the fixes: 207 passed, zero failed/skipped/test warnings (42.66 s).
- Tightened the late-result fixture to start with no water result, matching an Americano saved before water finishes; focused run `1793559335343` passed.
- The review of `07ec2d48` identified a static-write validation gap for bag relinks. Static recipe updates now revalidate both `bagId` and `profileJson`, alongside the other water/yield inputs. Run `1793559335344` reproduced out-of-range writes being accepted for both fallback sources. Build `1793559335116` and full run `1793559335345` passed after the fix: 207 passed, zero failed/skipped/test warnings (41.93 s); regression checks also verify atomic rejection and a successful corrected write.


## Final four-issue review corrections — 2026-11-02

- Reproduced all four findings in Qt Creator run `1793559335358` before their behavior fixes: recipe-refresh target ownership, import without dose/profile context, standalone overrides, and invalid explicit fixed amounts falling back to presets.
- Recipe refresh now uses the existing application generation before dose/anchor notifications and publishes the next water setup after queued writes finish. The running drink retains its own target and specification; explicit live target edits remain supported. No parallel suppression state or timer was added.
- Static import defers only unavailable profile fallback, including an espresso ratio anchor with no dose. Known-invalid installed targets still fail. Explicit fixed amounts must be positive whole numbers within the supported range; invalid writes are atomic and cannot use legacy fallback. Legacy omission and fixed ml editor hydration retain their interpretation.
- Explicit standalone settings update the next setup without editing the shared vessel. Live validation exposed a second reset in page activation; navigation no longer selects a vessel, the shared apply-settings action only transmits current values, and selected-vessel saves explicitly select/apply their saved preset. This removes competing preset writes rather than introducing another ownership flag.
- Focused runs `1793559335359`, `1793559335360`, `1793559335362` and final `1793559335364` cover controller/queue/state integration, real static import, storage rejection, production settings/phase wiring, service requests and shipping QML handlers. A fixture initially named the wrong QML hydration method; it was corrected to execute the shipping `applyHotWaterJson` method. Final focused run: five targets passed without test warnings.
- Native and packaged Wasm build `1793559335155` succeeded. Final full Qt Creator run `1793559335365`: **207 passed, zero failed/skipped/test warnings (38.58 s)**. The native linker retains three local environment warnings (two OpenSSL deployment-version mismatches and the large unwind-table warning); no compile errors were reported.
- User-started simulator checks verified 42 g / 2.5× stayed at 105 g while an external water-only edit changed the next recipe to 3× at its stored 40 g. The 5:33 PM simulated drink recorded the frozen 2.5× and 105 g target separately from 116.0 g measured water; completion applied 120 g for the next setup. Native and matching browser legacy editors both showed fixed 150 ml. On the rebuilt navigation path, a completed standalone 150 g pour retained 150 g while its shared vessel remained 200 g.
- Removed the owned simulated shot, both temporary recipes and the temporary vessel. Verified the original active recipe's water JSON and shot count, original preset list/selection, simulated DE1/scale and 36 g espresso / 200 g water / 85°C / 4 ml/s settings against the captured baseline. The final shared apply-settings close/reopen smoke check awaits the requested restart; it will be recorded before final PR handoff.


## Three follow-up review fixes — 2026-11-02

- Selected standalone vessel saves now use the existing revision-checked host commit. A rename preserves an active recipe's cached water plan; unselected and recipe-draft saves remain updates without live selection. The page no longer issues a second selection after an edit.
- Complete-candidate validation captures the installed profile's normalized target and enabled dose recommendation from existing catalog metadata. Recipe and bag doses retain precedence; unknown-context static import retains its earlier behavior. Async create/update/clone regressions include a dose-less ratio recipe with a zero profile target, excessive resolved water, disabled recommendation and atomic rejection.
- Removed MainController's parallel unbounded water-specification map. Water specifications now share the existing bounded shot-start records with review baselines, including operation-specific water association and removal on take. A regression runs 130 incomplete cycles and verifies expired snapshots are absent, late orphan association cannot recreate them, and a current operation retains/removes its own water data correctly.
- Regression run `1793559335367` reproduced the editor operation and valid profile-dose rejection before behavior fixes. Final focused run `1793559335369`: five targets passed without test warnings. Native/packaged Wasm build `1793559335160` succeeded. Full Qt Creator run `1793559335370`: **207 passed, zero failed/skipped/test warnings (39.80 s)**. The native linker retains the same three local environment warnings.
- Strict OpenSpec validation and whitespace checks pass. No new live acceptance is claimed: the app has not yet been restarted for this build. The native/browser selected-vessel rename check and the preceding close/reopen check remain pending that restart. No live settings, recipes, vessels, profiles or shots were changed during these fixes.


## Final live checks and restoration — 2026-11-02

- Verified the user-restarted main-checkout app (PID 13613, host port 8888) and matching host-served Wasm client. The DE1 and scale remained simulated, with automatic uploads disabled.
- Renamed a selected disposable shared vessel through the actual native editor and then through the browser editor. Both saves retained the same active recipe at 40 g espresso / 2.5× / 100 g added water; the saved recipe ratio was unchanged. The native rename retained the shared row’s independent 200 g fixed amount. Recipe snapshot recreation and confirmed preset-save results remained consistent after each rename.
- Selected the original fixed vessel to release recipe ownership, applied a temporary 150 g setting, and closed/reopened Hot Water in both native and browser clients. After each close and reopen the host still held 150 g, while the shared vessel remained fixed at 200 g. No dispensing was started and no shot was created.
- Removed the one disposable recipe and all three owned vessel rows (including snapshot recreations), then restored the captured original setup. Verified the original recipe’s identity, water JSON, profile, dose/yield, bag, equipment, grind and 19-shot count; the exact three-preset list and selection; and 18 g dose / 36 g espresso / 200 g water / 85°C / 4 ml/s. Both native and browser Home showed Americano Test again; the disposable browser tab was closed.
- This closes the pending rename acceptance and the earlier close/reopen check. No implementation changes were needed. Native/Wasm build `1793559335160`, focused run `1793559335369` and full run `1793559335370` remain applicable: 207 passed with no failures, skips or test warnings. GitHub text-invariants run `41585512395` passed and Codex reviewed code commit `7fc808bd` with no findings; all inline threads were resolved at the preceding check.


## Final installed-catalog integration correction

- Codex identified that the lightweight availableProfiles picker list omits target/dose metadata. The earlier storage tests supplied that context directly, so they did not cover the production catalog boundary.
- MainController now delegates to ProfileManager::recipeBrewDefaultsSnapshot, which captures typed ProfileInfo fields directly in activation’s first-exact-title order. No QVariant picker projection or per-profile disk read is involved; an enabled dose recommendation is retained and a disabled one contributes no dose.
- Added a real-catalog-to-async-storage regression using installed test profiles: zero target / enabled 18 g dose, positive inherited target / disabled recommendation, successful create/clone and rejected excessive water on update. Qt Creator run `1793559335371` reproduced the zero-versus-18 g failure before the typed lookup fix.
- Focused run `1793559335372` passed both profile-manager and recipe-storage targets. Native/packaged Wasm build `1793559335164` succeeded, and full run `1793559335373` passed all **207 tests**, zero failed/skipped/test warnings (39.87 s). The same three local linker environment warnings remain. Strict change validation and whitespace checks pass.
- This backend catalog correction leaves the shared UI code unchanged; the preceding completed native/browser rename and close/reopen acceptance and verified original-settings restoration remain applicable. No further live app data was changed.

## Final editor preview correction

- The shared wizard now passes the selected installed profile summary, or its existing embedded-profile fallback, to RecipeContext's yield preview. The resolver uses the same recipe → bag → enabled profile dose order as candidate validation; disabled recommendations do not supply a dose.
- Added service regressions for 18 g / 1:2 → 36 g espresso / 90 g added water, recipe/bag precedence, disabled and embedded recommendations, and the JavaScript-to-facade call. A shipping-QML fixture verifies the wizard passes its selected profile and uses embedded data only when installed data is absent.
- Native and matching packaged Wasm build `1793559335171` passed, including the QML diagnostics gate (361/361 clean). After the user authorized switching Qt Creator to this checkout, focused run `1793559335378` passed both changed targets, and full run `1793559335379` passed **207 tests**, zero failed/skipped/test warnings (38.02 s). The same three local linker environment warnings remain.
- This correction changes preview inputs only. No new live acceptance is claimed for the inherited-dose edge case; its coverage is automated. The completed native/browser vessel and close/reopen checks remain recorded above, and no app data or restored settings were changed.

## Embedded-dose import and vessel deletion corrections

- Import validation now reads the enabled embedded dose alongside the embedded target, retaining installed-profile precedence. The reported 18 g / 1:2 / 11× case imports as a valid 396 g plan rather than being rejected using the profile's 50 g target.
- Deleting the selected vessel requests the existing host preset-apply path. That path retains a recipe-owned water plan or applies the surviving standalone preset. Removed the redundant client-side selection callback and its unused signal.
- Added one focused regression for each report using the existing storage/service fixtures. Qt Creator focused run `1793559335381` passed both targets; full run `1793559335382` passed **207 tests**, zero failed/skipped/test warnings (41.35 s). Native and matching packaged Wasm build `1793559335183` passed with 361/361 QML files clean and the same three local linker environment warnings.
- These final cases have automated coverage; they were not repeated live. The original live setup remains restored, and no app data was changed during these corrections.
