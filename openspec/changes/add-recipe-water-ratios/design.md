# Design

## Context

See `proposal.md` for motivation and the three delta specs for behavior.

- `RecipeWizardPage.qml` snapshots vessels by value in `fVessel*` fields and `hotWaterJson`, but its water window only selects tiles. It currently directs users to Hot Water to add vessels.
- `HotWaterPage.qml` creates fixed 200 g presets; its name dialogs and main amount/temperature/flow controls are separate. `SharedPresetDraft` submits revision-checked `RecipeContext` preset operations. The host validates weight targets at 10–500 g, volume targets at 10–255 ml, temperature at 40–100°C and flow at 5–100 in native tenths of ml/s.
- `RatioPresetDialog.qml` already supports pick-only use, but also owns espresso preset preferences, Ristretto/Normale/Lungo content, help and live anchor writes. Copying it would duplicate UI and mixing water into its preset settings would change espresso behavior.
- `applyActivatedRecipe()` currently applies hot water before the queued dose/yield step completes. `ProfileManager` publishes target changes and freezes its espresso target during a shot; `brewSettingsSnapshot()` supplies live inputs without accidentally reading an old shot latch. Deliberate mid-shot espresso bumps use `MachineState` directly.
- `currentHotWaterSpecJson()` currently reads the shared fixed vessel, so it cannot describe a recipe-specific ratio or its resolved target. MCP conversion explicitly enumerates hot-water fields and would drop a new ratio unless extended. The retained HTML form rebuilds a name-only water block on save.
- Native and Wasm must use the same controls and services. The existing one-tap sequence is separate work; its controller requests hot water directly from the device and therefore needs the same invalid-water guard as user-driven starts.

## Goals / Non-Goals

**Goals:** Keep shared presets useful without a recipe, keep each recipe's dilution independent, reuse the ratio chooser UI, and resolve from the intended espresso target using existing host events.

**Non-Goals:** Ratios on standalone presets; water derived from measured output; a new global water-ratio preset preference system; ratio water for profile-less tea; changing drink sequencing or firmware policy; duplicating the new controls in legacy HTML.

## Decisions

### D1. An additive amount specification inside the recipe snapshot

Keep shared vessel records unchanged. Extend the recipe's hot-water JSON:

```json
{"hasWater":true,"vesselName":"Cup","amountMode":"ratio","waterToEspressoRatio":2.5,"mode":"weight","flowRate":40,"temperatureC":90,"order":"after"}
```

Fixed snapshots retain `volume` and `mode` (`weight` or `volume`); `amountMode: "fixed"` is optional for legacy compatibility. Ratio snapshots require a finite positive `waterToEspressoRatio`, use weight mode, and have no authoritative `volume`. Distinguish the water amount mode from the existing weight/volume measurement mode. Do not reuse espresso's dose multiplier or its bounds as a water-ratio definition.

Put normalization, validation and `qRound(ratio * espressoTargetG)` in one small core helper used by host paths and preview projection. Validate the rounded result against the existing 10–500 g water range, with no silent clipping. A ratio needs a usable espresso target; saving a profile-less ratio recipe is invalid. Unsupported or unresolvable runtime results are explicit failures rather than stale cached amounts.

Fixed legacy and name-only blocks retain their existing behavior. Unknown modes and conflicting authoritative fixed/ratio fields fail. No SQL schema migration is needed because the snapshot is already JSON.

*Alternative:* Put ratio mode in shared presets. Rejected because standalone editing must remain fixed-only and different recipes can use the same vessel at different ratios.

### D2. Shared vessel fields with context-owned drafts and persistence

Extract one vessel editor/field component for name, amount, temperature and flow. Recipe context enables Fixed/Ratio and seeds a newly added espresso vessel at 2.5×; standalone context offers fixed weight/volume and seeds 100 g. Existing edits always seed from their saved values. Profile-less tea starts fixed at 100 g.

The recipe editor opens from visible Add/Edit controls, seeds edits from its snapshot rather than a potentially changed global preset, and retains ratio metadata in its tile model and summary. Mode changes are explicit; switching to fixed seeds the resolved grams and switching to ratio offers 2.5×. Offer the existing amount stepper/text entry alongside the ratio chooser. Show `2.5× espresso yield · 100 g added water`, with temperature/flow in the shared fields. A ratio preview uses the draft's resolved target, including its dose/yield and existing fallback ladder, rather than the active machine's unrelated recipe.

Host preset writes continue through revision-checked `RecipeContext` operations. Add a full-row update helper to `SharedPresetDraft` or extract its applicable submission behavior so both callers share request tracking. Do not call `selectWaterVessel()` merely to design a recipe: the wizard updates its local draft after the preset operation succeeds.

When explicitly saving from a recipe:

- New vessel: create a fixed shared preset at the entered fixed amount or the ratio's current resolved grams, then select a full snapshot in the recipe draft.
- Existing vessel: save name/temperature/flow and an explicitly edited fixed amount through the shared preset operation. In ratio mode retain the existing preset's fixed amount/mode; save the ratio only in the recipe draft. Seed untouched preset fields from the captured preset, not default values.
- Missing shared preset: explicitly create it as a fixed preset from the edited snapshot's resolved amount, then attach the recipe-specific snapshot.

Explain that Save saves the vessel; the enclosing recipe still needs its normal Save. Cancelling before this operation changes neither; cancelling the recipe afterwards keeps the explicitly saved preset. Preset failure/staleness leaves the editor draft open. Keep duplicate-name, keyboard commit, accessibility and disconnect behavior from the existing flow. Renaming or editing never rewrites other recipes' snapshots.

*Alternative:* Update only a private recipe vessel without adding/editing the shared preset. Rejected because the requested actions manage vessels and should make a newly added vessel available outside the recipe as well.

### D3. Extract a reusable ratio chooser with two thin hosts

Extract the common dialog/card/selection/stepper presentation from `RatioPresetDialog.qml` into a parameterized shared control (for example `RatioChoiceDialog.qml`). Its API takes a title, explanatory content, a choices model, current ratio, validation bounds and choice/custom-edit signals. It owns UI state only, not preference writes or brew changes. Extract only the controls the two hosts actually use; retain espresso-only help in the espresso host.

The existing `RatioPresetDialog` remains the espresso wrapper, supplying its configured named presets and existing pick-only/live-apply behavior. A water wrapper supplies initial 2.0, 2.5 and 3.0 choices (Less water / Default / More water), water-specific explanation and a custom numeric field with 0.1 steps. Its selection updates the vessel draft only. Water quick-choice values are initial choices, not a new saved global preset collection. Highlight by numeric equality; a custom 2.7 must not highlight 2.5.

This is shared presentation with different data and effects; neither wrapper copies the chooser body. Register extracted QML in both native and Wasm source lists and update the active shared-UI inventory. Translation, Theme, focus order and screen-reader controls apply to both.

*Alternative:* Add many water-specific branches and preference paths inside the existing dialog. Rejected because the independent settings behavior is easier to preserve with a pure shared control and small wrappers.

### D4. Resolve after activation and on effective target changes

Add one host-owned recipe-water application path, called after activation's queued dose and yield anchor are established, on active-recipe restoration/edit, and on effective target changes. Reuse the existing yield resolution logic; expose/share the live setup projection already needed by `currentShotReviewSetup()` rather than implementing another recipe/bag/profile ladder or reading an old shot latch.

Track only the last normalized recipe-water inputs, recipe identity and effective espresso target needed to avoid duplicate work. Resolve initially, when the water specification changes, and when the numerical target changes. Reapplying cached settings when another operation changed machine configuration is permitted; starting another unchanged drink does not recompute the multiplier. Do not create a polling timer or write the derived number back into the recipe/global preset.

Use `ProfileManager` target-change events for ordinary overrides, dose-driven changes and profile edits. Include deliberate `MachineState` espresso-target bumps when they change the intended target for the pending water step. Actual weight, tare, settling and hot-water target bump signals are not ratio inputs. Changes while water is already pouring do not alter that pour; use existing phase/sequence completion events to apply the latest configured target for the next drink. For a long black whose water already ran, do not retroactively alter that completed step or modify the in-progress espresso latch.

Activation must not mark its water setup complete using the prior recipe's target. Keep terminal activation semantics after the new water target is configured. A change to just the water ratio also refreshes live water after a committed recipe edit even if yield stayed equal. Re-selecting the same vessel uses the active recipe's water spec, while selecting a different vessel deactivates the recipe as today.

An invalid result withdraws recipe-water readiness and supplies a clear error; neither a manual start nor the sequencer's direct device path may use the previous target. Share that guard at the host water-start boundary reached by both. Do not change firmware permission policy or add retries.

*Alternative:* Resolve at every brew start or in QML. Rejected because the user requested target-change updates, and GHC/MCP/browser operation must share host behavior.

### D5. Keep effective settings, presets and history distinct

Apply resolved grams to the existing live water settings, leaving fixed shared preset data untouched. Standalone editing reads preset values; effective recipe readouts show resolved values. A preset recreated during ratio activation is fixed at that activation's resolved grams; later target changes do not rewrite it.

Change `currentHotWaterSpecJson()` and start-of-drink capture to use the recipe's amount specification and effective target rather than reconstructing from the shared preset. Record `resolvedTargetG` for ratio water separately from `waterToEspressoRatio` and measured water results. Freeze the ratio specification at drink setup and the target used at the water step's start: an explicit espresso-target change before an Americano's water can legitimately change that target. Extend the existing operation/UUID-keyed water-result join to deliver this target alongside the water outcome even when the espresso row was saved first; do not attach by recency. A long black captures its target before espresso, and skipped water retains the planned target without claiming it was dispensed. Later edits cannot rewrite either capture. Clone and promotion carry the ratio, not one inferred from measurements. Browsing recipe cards resolve against that recipe's seed/fallback context; active views use its effective target.

Extend explicit MCP field conversions and schema with `amountMode` and `waterToEspressoRatio`; expose `resolvedTargetG` only as derived read/history data, never as a competing authoritative amount. HTTP/native JSON use the same semantics; existing fixed `volumeMl` MCP compatibility remains. Update tool documentation and surface version with the schema change. Legacy HTML must merge/preserve the loaded ratio block on unrelated edits instead of replacing it with name-only water; new ratio UI lives in shared QML.

*Alternative:* Keep history as only resolved grams. Rejected because editing, cloning or promotion would flatten the user's chosen ratio.

## Risks / Trade-offs

- [Preset Save and recipe Save are separate commits.] -> Name the two effects in the editor; preserve drafts on failed requests and test cancellation after successful preset creation.
- [An old fixed preset can share a name with a recipe's different snapshot.] -> Seed recipe edits from the snapshot, keep the captured global fixed fields for ratio saves, and use expected revision/name checks.
- [Yield notifications can arrive before activation finishes or carry a shot latch.] -> Resolve from established live inputs after the queued activation step; test switching from a different yield and restoring after restart.
- [A large ratio can exceed the water range.] -> Show the computed result/error and prevent that recipe's water from starting; never silently clamp or retain an obsolete usable target.
- [Refactoring a widely used espresso chooser can regress espresso behavior.] -> Keep its existing wrapper API and verify widget, Brew Settings and recipe-wizard callers, including preset editing and pick-only behavior.
- [The active one-tap change shares controller and water-page files.] -> Integrate with its existing sequencer and add a focused guard test; do not rewrite its timing/firmware rules or held validation tasks.

## Migration Plan

1. Add tolerant reads for old fixed/name-only blocks and validated writes for the new ratio variant. Leave stored presets and existing recipes untouched.
2. Ship the shared UI/service changes and host amount resolution together; native/Wasm package matching already prevents mixed client versions.
3. Verify ratio JSON through storage, MCP/HTTP, promotion and backup/transfer. No table rebuild or bulk conversion is planned.
4. Rollback to a version without ratio support is not transparent: old hosts can ignore the new ratio fields. Before downgrading, explicitly convert ratio recipes to their resolved fixed amount or restore the pre-change backup; never claim old binaries preserve ratio behavior.
