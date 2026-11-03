# Design

## Context

See proposal.md for the feature scope. This revision replaces the implementation's growing collection of water caches and repair callbacks with explicit ownership. The current branch supports the requested editors and amount calculation, but review repeatedly found disagreements between preset selection, active-recipe refresh, queued activation and history capture.

Keep the existing JSON representation, shared QML/service path, activation queue, drink sequencer and operation/UUID-keyed water-result join. MainController already has an explicit espresso-target adjustment action; infer neither user intent nor water targets from measured weight notifications.

## Goals / Non-Goals

**Goals:** One amount contract, one owner of active recipe-water state, one application path for fixed and ratio water, explicit editor effects, and history derived from the same pour target as machine control. Make transitions directly testable.

**Non-Goals:** A new workflow framework, new remote object, new global ratio preferences, ratios on standalone presets, SQL migrations, changes to firmware permissions or sequence timing, or a rewrite of the espresso chooser. Retain the existing shared ratio presentation and both wrappers.

## Decisions

### D1. One parsed water specification and one candidate validator

Keep the additive recipe JSON:

    {"hasWater":true,"vesselName":"Cup","amountMode":"ratio","waterToEspressoRatio":2.5,"mode":"weight","flowRate":40,"temperatureC":90,"order":"after"}

WaterAmount owns parsing/normalization and resolution. Its parsed amount is explicitly disabled, fixed weight, fixed volume, ratio, or unresolved legacy preset reference. Callers do not independently interpret amountMode or decide whether a volume overrides a ratio. Existing snapshot fields and API names stay unchanged; use a small value representation, without replacing recipe maps across the application.

Only an absent amountMode receives legacy fixed semantics and name-only/zero-placeholder preset lookup. An explicit fixed amount must be a positive whole number in the supported range; invalid values never inherit a preset amount. A supplied discriminator must be a string with value fixed or ratio; null, numeric, boolean, object, array, empty or unknown values fail. Ratio must be finite and positive, use weight mode and contain no authoritative fixed amount. Derived resolvedTargetG is read/history data and is never accepted as an amount input. Every enabled recipe water block requires a nonblank vesselName, for fixed and ratio water. Preserve compatible legacy name-only lookup; a block without a vessel is invalid and must be corrected explicitly, never matched to the current selection automatically. Unsaved editor previews may calculate before a name is entered; saving and applying require it. Full snapshots remain independent of later preset edits.

Resolution is pure: a parsed specification and an explicit espresso target produce a complete WaterPlan or an explanatory error. A WaterPlan contains the normalized snapshot, amount, measurement mode, temperature and flow; ratio plans also retain their espresso target and ratio. Round with qRound and validate 10–500 g; fixed volume retains 10–255 ml. No clipping or stale-plan fallback. Preview and host call this same function.

Recipe storage owns one validation entry for the complete resulting recipe. Create, merged update, clone, promotion and import pass through it before a write, within the existing transaction/conflict handling. Validate every candidate, so changing bagId or profileJson cannot bypass water validation merely because hotWaterJson was untouched. Adapters translate field names and return the validator's error; they do not rebuild validation ladders.

Build validation context from the candidate's linked bag and the installed profile target and enabled dose recommendation captured before background work, falling back to embedded profile JSON only when the installed profile is absent. Reuse BrewBaseline and YieldSpec, resolving dose from recipe, then linked bag, then the installed profile’s enabled recommendation. Distinguish unavailable profile context from a known nonpositive target: normal live writes with complete context reject unusable ratios; static import without an installed-profile catalog may preserve a structurally valid ratio whose inherited target is unavailable. It must still reject a known invalid explicit target, dose or ratio, and activation must resolve before enabling water. Carry this exception in an explicit context value rather than negative-number sentinels and transient recipe fields. Existing missing-profile handling remains authoritative.

Alternative: keep partial validators in adapters and enumerate fields that trigger revalidation. Rejected because new write paths or yield dependencies repeatedly bypass that approach.

### D2. Preset editing and vessel selection are different actions

Keep WaterVesselDialog and the shared ratio chooser. Simplify the editor contract to a local draft, a captured preset/revision, and one frozen submitted snapshot. The shared editor performs a revision-checked preset save (the existing selected-preset commit for standalone edits, an update for unselected or recipe-draft edits) and returns its confirmed result; the caller decides what to select or update.

| Action | Shared preset | Recipe draft | Live selection/settings |
| --- | --- | --- | --- |
| Cancel before vessel Save | Unchanged | Unchanged | Unchanged |
| Save a recipe vessel | Explicitly add/update fixed row | Replace from submitted snapshot after success | Unchanged |
| Add on standalone Hot Water | Add fixed row, default 100 g | None | Select the confirmed new row, as before |
| Edit an unselected standalone vessel | Update that row | None | Preserve current selection and effective settings |
| Edit the selected standalone vessel | Update that row | None | Apply the edited row when no recipe owns water; otherwise retain the recipe plan |
| Explicitly select another vessel | Unchanged | None | Existing recipe-deactivation and fixed-preset application |

The table's selection/application effects are immediate only when no current drink owns water. Otherwise record the selection in the next setup and defer its water-settings writes under D3, even if the current drink's recipe has since been deactivated. Preset persistence and recipe-draft updates can still complete immediately.

Recipe edits seed from the recipe snapshot, not the global row. New espresso-recipe vessels start at editable 2.5×; profile-less tea and standalone additions start fixed at 100 g. Existing fixed weight/volume or ratio settings remain unchanged until explicitly edited. Clicking an already selected amount mode is a no-op. Converting ratio to fixed seeds resolved grams and weight mode; selecting fixed when already fixed must preserve ml versus g. Reopening or selecting ratio retains an existing custom draft ratio, defaulting to 2.5 only if none exists.

For ratio saves to an existing shared vessel, preserve its captured fixed amount/mode and update explicitly edited name/temperature/flow. For a new or missing shared vessel, create a fixed row at the draft's resolved amount. Ratio stays only in the recipe draft. Save the enclosing recipe separately; cancelling it does not undo an explicitly saved preset. No automatic rewrite of other recipes.

Pending writes disable editing and dismissal, correlate replies by request, and use the submitted snapshot on success. Failure/conflict keeps the draft available; disconnect terminates pending work without replay. Confirmed new-row identity must come from the admitted save result or its revision-checked confirmation, not an unrelated list's last index. Preserve duplicate-name checks, keyboard commit and accessible focus.

Alternative: have the shared editor select its saved vessel or apply machine settings. Rejected because recipe design and editing another preset must have no such side effects.

### D3. One host state owner and one settings adapter

Introduce a small host-side RecipeWaterState value object. It has no QML, storage, BLE or event-loop dependencies. It owns three explicitly separate values: the latest next-setup inputs/plan or error; the current drink's immutable water specification, intended espresso target and pending plan; and an optional water-cycle capture. A temporary espresso adjustment belongs to that drink only. MainController supplies events and applies the returned plan; it no longer owns parallel fixed/ratio caches or inference flags.

The coherent setup input is recipe identity, normalized water snapshot and the configured espresso target. Include changes to fixed snapshots as well as ratios. Activation submits this input only after the existing RecipeApplyQueue has established dose/yield and completed its current generation. Start readiness remains blocked while activation is pending; superseded generations finish with failure and cannot publish a plan or write into a newer setup. Restoration and committed active-recipe edits use the same transition. RecipeWiringController begins the generation before refreshing the dose rung and finishes after queued anchor writes, so its target notifications cannot be mistaken for a current-drink edit.

Resolve only when normalized water or its relevant target changes. Recipe identity changes replace next-setup ownership even if the numeric plan is equal; they do not erase a running drink or its capture. A drink begins by retaining the existing plan and specification, without recomputing an unchanged ratio. Starting another unchanged drink reuses the setup plan. A missing preset is recreated once as fixed at the resolved amount. Legacy name-only references are expanded with the current named preset before comparison; full snapshots never inherit later preset edits.

MachineControlPresets (or its existing settings-only helper) is the sole adapter from a WaterPlan into live settings. The state owner retains failed application as a readiness error until a successful retry; a resolved number alone does not prove application succeeded. Fixed and ratio water use it equally. Explicit standalone settings writes update the next fixed setup without changing the shared row; ignore notifications from the adapter itself. Opening/reopening Hot Water performs no implicit vessel selection, preserving explicit temporary overrides. The shared apply-settings operation transmits current settings; selecting a vessel or committing its selected preset explicitly loads preset values. Applying settings is downstream of resolution; sending settings must not call back into resolution to repair ownership. Existing preset commits explicitly request reapplication of the active recipe's cached plan when that recipe owns water. Applying that cached plan performs no multiplication and does not overwrite the preset. While a drink owns water settings, preset saves and next-setup changes cannot write around this adapter into those effective settings.

Same-vessel re-selection retains the recipe plan. With no current drink, explicit different-vessel selection releases recipe ownership and applies the fixed preset. During a drink, vessel selection, recipe replacement/deactivation and recipe edits change the next setup only; machine application is deferred until that drink releases its water settings. Preserve its frozen specification, intended target and capture independently of whether its recipe is still active. Clear only next-setup readiness/overrides on next-setup deactivation. The current drink's adjustment/capture ends on that operation's completion or abort, with captured history values retained by the join.

Alternative: move the existing MainController methods unchanged into another class. Rejected: the reduction comes from one plan and explicit transitions, not from relocating competing caches.

### D4. Explicit target events and a captured water step

Ordinary target changes use ProfileManager's configured target, not its in-shot latch. Deliberate live espresso bumps enter RecipeWaterState from the accepted bumpTargetWeight action, scoped to the current espresso operation. Do not subscribe to generic MachineState target notifications as evidence of an intentional edit.

A live bump affects an Americano's pending water only. The latest accepted bump wins, including a bump back to the original value. A later configured-target edit for the same current drink replaces that temporary adjustment. Whole-setup replacements (activation/restoration), stored water-spec edits and target notifications belonging to a replacement recipe affect the next setup, not the current drink. Supply that event context explicitly instead of inferring it from the new target number. Clear the drink adjustment on its completion or abort; deactivation/replacement must not leak it into the new setup or erase the current drink's retained value. For a long black whose water has already started, later espresso changes do not rewrite that step or alter the in-progress espresso latch.

| Transition | Water behavior |
| --- | --- |
| Stable setup or committed recipe edit | Resolve current fixed/ratio plan and apply when no current drink step holds it |
| Accepted espresso target change before water | Replace pending water plan using the intended target |
| Actual espresso output, tare or settling | No plan transition |
| Water starts | Freeze the complete plan used for that water cycle |
| Stored water-spec edit, recipe replacement or different-vessel selection during a drink | Update next setup; keep this drink's specification/pending plan or capture |
| Target changes during water or after a long black's water | Retain latest next-setup inputs; keep this drink's capture/settings |
| Water/drink completes or aborts | Release capture for machine application and apply latest setup when the drink no longer holds water settings |
| Start refused locally | Return failure immediately and terminate pending sequence intent |

Freeze the water-step plan at actual HotWater entry, before entry/phase callbacks can apply next-setup updates. Its amount, mode, temperature and flow serve machine control and history. Before actual entry, eligible intended-target edits may update the pending plan; accepting a request does not freeze it or create a water-settings hold. This preserves the original in-progress-pour promise without adding a standalone request lifecycle. A physical water start uses the same entry capture.

Keep the sequencer's existing AwaitingStart/refusal timeout unchanged. Local start refusal ends its existing intent immediately. Do not add a timer or route standalone/first-step long-black starts through AwaitingStart: that would interfere with first-step arming. No recipe-water pending-request flag can remain latched after a silent standalone refusal.

The device's existing common start guard checks the relevant plan's readiness for manual and sequenced starts: a current drink's pending water plan when it owns the step, otherwise the current setup. Invalid ratio inputs keep the old numeric settings from being treated as a usable recipe plan; no dispatch is allowed until corrected. Explicitly switching to a standalone fixed vessel releases recipe ownership and remains usable.

Alternative: recalculate on every drink/start or repair amounts during settings sends. Rejected because it obscures ownership and violates target-change-only calculation.

### D5. Use the same capture for history, keep the existing join

Keep the operation/UUID-keyed ShotWaterJoin and existing ShotWaterResult codec. Retain start-of-drink water specifications with the already bounded per-operation shot-start snapshots, rather than an unbounded parallel map; aborted cycles cannot accumulate snapshots indefinitely. The state owner's water-step capture supplies resolvedTargetG and the ratio specification; actual scale weight/flow estimate remains a separate result. Consumers never reconstruct the target from current settings, a shared preset, measured output or the current recipe after the step.

Freeze the drink's recipe water specification at drink setup in RecipeWaterState, before water begins, independently of the latest active recipe. An espresso-target change before an Americano's water may change its resolved target using that frozen specification. Stored water-spec edits during a drink belong to the next setup and must not replace the current drink's specification; target edits obey D4's explicit event context. A long black captures water before espresso. A skipped step retains the planned target but records notDispensed; a refused/failed step must not claim an actual pour.

If the espresso row is saved first, the existing narrow late update writes the captured water result and resolved target together under shot ID/UUID. If water finishes first, initial save uses the same capture. Capture survives releasing the live water hold because the join retains its value for the associated operation. Later recipe edits never rewrite it. Clone, promotion and backup retain the designed ratio. Full normalized fixed snapshots remain independent of shared preset edits.

HTTP/native JSON and MCP keep the current field mappings and error code, including fixed volumeMl compatibility. Strip/ignore supplied derived targets when shaping a writable candidate; reads/history may expose them. Legacy HTML preserves the loaded ratio on unrelated edits; add no second ratio UI.

Alternative: maintain a second history-specific water-target cache. Rejected because it creates another authority to synchronize after target changes.

### D6. Verify transitions through production code

Use focused tests of WaterAmount, complete-candidate validation and RecipeWaterState, then at least one integration fixture exercising the production event/settings/start/history wiring. Do not rely on regex-extracted function bodies or copied handler logic as the sole proof of those transitions. Existing handler checks may remain supplementary.

Cover activation A→B, restoration, active fixed/ratio edits, preset saves/reselection, unchanged repeats, configured target changes, accepted bump up/down, invalid-to-valid recovery, both sequence orders, target edits before actual water entry, changes while pouring, abort/refusal, and water result arriving before/after shot save. Use real shared editor instances or existing QML interaction fixtures for editing an unselected vessel, clicking an already selected mode, and a custom ratio round trip.

The review gate precedes implementation: an independent agent reads the revised proposal/specs/design/tasks against current call paths, reports actionable gaps, and reviews any resulting corrections. Run OpenSpec validation. Existing green builds/tests are historical evidence, not validation of this refactor.

## Risks / Trade-offs

- [A new state object merely relocates complexity.] → Remove the old parallel caches/repair callbacks as it lands; test explicit transitions and keep the object free of framework dependencies.
- [A water request may never enter HotWater.] → Freeze at actual entry; preserve the sequencer's existing refusal handling and introduce no standalone request hold. Test eligible target edits before entry and a silent standalone non-start.
- [Next setup changes while another drink still owns water.] → Retain that drink's pending plan/cycle capture independently, defer all next-setup water writes through the adapter and test replacement/deactivation during espresso and water.
- [Preset Save and recipe Save are separate.] → Keep their effects visible, use confirmed request results and preserve drafts on failure.
- [Imported recipes can lack installed-profile context.] → Represent unknown context explicitly, preserve only structurally valid data and require resolution at activation.
- [Refactoring a shared flow regresses standalone or espresso editing.] → Retain the ratio chooser/wrappers, cover selection and unit invariants, and inspect matching native/Wasm controls.
- [Simulator acceptance does not establish physical firmware behavior.] → The user selected simulator-only acceptance; preserve existing firmware permissions/timing and validate both sequence orders with simulated DE1 and scale inputs. Physical-device acceptance is outside this change’s acceptance gate.

## Migration Plan

1. Revise these planning artifacts and close independent design-review findings before changing implementation.
2. Preserve the current JSON/API contract; land canonical parsing/complete-candidate validation and the single state owner with their production-code tests. Remove superseded paths in the same steps.
3. Simplify editor result handling and connect the one captured plan to existing machine/history boundaries. Keep activation queue and sequence/result join semantics.
4. Build native/Wasm and run the full suite through Qt Creator. After a user-started restart, validate matching native/browser behavior and restoration; complete the agreed simulator-only acceptance and record its results.
5. No data rewrite or SQL migration. Older binaries cannot interpret ratio recipes reliably; convert to explicit fixed amounts or restore a pre-change backup before downgrading.
