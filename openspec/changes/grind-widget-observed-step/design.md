# Design

## Context

Two independent code paths touch "grind step" today, and they don't share:

- **AI / MCP.** `ShotHistoryStorage::queryGrinderContext()` ([shothistorystorage_queries.cpp:940](../../../src/history/shothistorystorage_queries.cpp)) selects the distinct `grinder_setting` values for the grinder (resolved via `equipment_id`) and computes `smallestStep` as the raw minimum gap between sorted numeric values. This flows into `DialingBlocks::buildGrinderContextBlock()` → the `grinderContext` MCP block and the AI user-prompt enrichment (`aimanager.cpp`).
- **Widget.** `GrindQuickSelectItem.qml` builds its picker rows with `stepGrind(current, n, step)` for `n ∈ -5..+5`, where `step` comes from `Settings.brew.grindQuickSelectStep` (default `1.0`). It never sees the history-derived step, so it shows whole numbers.

The setting lives in four places: `settings_brew.h`/`.cpp` (QSettings key `espresso/grindQuickSelectStep`), the "Grind step" control in `SettingsMachineTab.qml`, and two lines in `settingsserializer.cpp` (backup + restore).

Goals: one shared, noise-filtered step estimator; the widget consumes it; the AI benefits from the same improvement; the manual setting goes away; and the Grind/Ratio pills stop painting an opaque white capsule over a background image.

## Goals / Non-Goals

**Goals**
- Single source of truth for the derived step, used by widget + AI.
- Robustness to a lone mistyped history value.
- Sensible behavior when no grinder is selected (derive from full history; fall back to `1.0`).
- Remove `grindQuickSelectStep` cleanly (property, UI, serialization).
- Grind + Ratio pills honor the `hasBackgroundImage` transparency convention, text included.

**Non-Goals**
- No new catalog "detent size" field on `GrinderEntry`. History is the only numeric-step source; compound-notation grinders keep their existing `stepGrinderSetting` path with the resolved step. (Catalog-declared detents are possible future work but out of scope.)
- No change to how letter/compound settings are stepped — only the numeric `step` magnitude's source changes.

**In scope (added after review):** RPM-mode stepping is also history-derived (see Decision 7), for parity with burr stepping — the per-shot RPM data already exists.

## Decisions

### 1. Robust step estimator (modal gap)

Replace "minimum gap" with "most common gap," which is what makes it noise-tolerant. Given the sorted, de-duplicated numeric settings `V` (size ≥ 2):

- Compute consecutive gaps `g[i] = V[i] - V[i-1]`, dropping zeros.
- Round each gap to 2 decimals (absorbs float dirt like `0.4999999`).
- Pick the **modal** gap (highest frequency). Break ties toward the **smaller** gap — if the user has demonstrated a finer move as often as a coarser one, offer the finer.
- Clamp to a floor (`0.05`) so a cluster of fine typos can't yield a micro-step.
- With exactly 2 distinct values there is one gap → that gap is the step.
- With fewer than 2 distinct numeric values → **no step** (sentinel `0`), and callers apply their own default (`1.0` for the widget; field omitted for MCP).

Worked example (the user's Niche Zero): `7.5, 8, 8.5, 8.75, 9` → gaps `0.5, 0.5, 0.25, 0.25` → tie between `0.5` and `0.25` → smaller wins → **0.25**. Add a mistyped `8.1`: gaps `0.5, 0.1, 0.4, 0.25, 0.25` → `0.25` is modal (2×) → **0.25**; the outlier's `0.1`/`0.4` each occur once and lose.

Why modal, not GCD: GCD is *destroyed* by an outlier (`gcd(0.5,0.1,0.4,0.25)=0.05`), the exact failure we're fixing. Modal frequency is the robust choice.

Implementation: a static free function `deriveGrindStep(const QList<double>& sortedDistinct) -> double` in the queries TU, returning `0` when it can't derive. Both `queryGrinderContext()` and the new QML-facing invokable call it, so the AI and the widget can never disagree.

### 2. No-grinder scope

`queryGrinderContext()` currently early-returns when `grinderModel` is empty. For the widget's "no grinder selected" case we need a derivation over *all* grinders' numeric settings. Rather than widen `queryGrinderContext` (it also carries bean-brand scoping and MCP semantics), add a focused invokable:

```cpp
// ShotHistoryStorage
Q_INVOKABLE double grindStepForGrinder(const QString& grinderModel);
```

- Non-empty model → distinct numeric settings for that grinder.
- Empty model → distinct numeric settings across all grinders (`getDistinctGrinderSettings()`), the same all-history list the widget's `_observedFallback` already uses.
- Runs `deriveGrindStep` on the parsed-numeric subset; returns `0` when it can't derive.
- Reuses the existing async distinct-value cache (`requestDistinctValueAsync` + `distinctCacheReady`) so QML re-evaluates when history finishes loading — the widget already listens via `_distinctCacheVersion`. Returning `0` on a cold cache is correct: the widget uses `1.0` until the cache warms, then recomputes.

Caveat (accepted): the empty-model list mixes grinders. Non-numeric (letter/compound) values are excluded from the numeric estimator, and the modal-gap approach tolerates a mixed numeric set better than min-gap. This is the explicitly requested behavior; a precise per-grinder step returns as soon as a grinder is selected.

### 3. Widget wiring

In `GrindQuickSelectItem.qml`, replace:

```qml
readonly property double grindStep: (Settings.brew.grindQuickSelectStep > 0)
    ? Settings.brew.grindQuickSelectStep : 1.0
```

with a history-derived, cache-reactive value:

```qml
readonly property double grindStep: {
    var __ = root._distinctCacheVersion            // re-evaluate when cache warms
    var s = (MainController.shotHistory)
        ? MainController.shotHistory.grindStepForGrinder(root.grinderModel) : 0
    return s > 0 ? s : 1.0
}
```

Everything downstream (`stepGrind`, `_stepDecimals`, row de-dup, `finerHint`) already handles arbitrary decimal steps, so no other widget logic changes. Compound/registry grinders still route through `Settings.dye.stepGrinderSetting(...)` with `n * step`.

### 4. Field rename `smallestStep` → `stepSize`

The value is no longer the minimum gap, so the name would mislead an AI reading the payload (MCP convention: field names must be self-describing). Rename in `GrinderContext` (`shothistory_types.h`), in the emitter (`dialing_blocks.cpp`), and in the AI enrichment label (`aimanager.cpp`, "Smallest step" → "Typical step"). Update the `mcptools_dialing.cpp` description prose. This is a payload-breaking change, but MCP consumers are LLMs and tolerant, and the field was newly added.

### 5. Removing the setting

The step is intrinsically per-grinder — a Niche Zero's 0.25 detent says nothing about the increment on a different grinder or a swapped burr set. A single *global* `grindQuickSelectStep` is therefore a category error: it forces one grinder's increment onto every grinder. The per-grinder derivation makes the global value not just redundant but meaningless, so it is removed rather than merely defaulted.

Delete `grindQuickSelectStep` property/getter/setter/signal from `settings_brew.*`, the "Grind step" `ValueInput` block in `SettingsMachineTab.qml`, and the two `settingsserializer.cpp` lines. Old backups that still contain `espresso/grindQuickSelectStep` are harmless — the restore path simply won't have a key to read. No migration needed.

### 6. Pill transparency

Adopt the house convention in both pills:

```qml
readonly property bool hasBackgroundImage: Settings.theme.backgroundImagePath.length > 0
color: hasBackgroundImage ? "transparent" : Theme.surfaceColor   // pill fill
border.width: hasBackgroundImage ? 0 : 1
```

Both pills currently fill with `root.zoneTextColor` (opaque, ~white on an image) and draw value text in `Theme.primaryColor` chosen to contrast against that fill. When transparent, that text must move to a background-legible color (`root.zoneTextColor`, matching Beans/Milk), otherwise it becomes accent-on-photo. So this is a fill **and** text-color change, gated on `hasBackgroundImage`. In the no-image case, keep the current opaque look so nothing regresses for users without a background image.

### 7. RPM step derivation

Variable-RPM grinders dial in by motor RPM, and the widget's RPM mode previously stepped by a fixed `rpmStep = 50`. For parity with burr stepping, RPM now derives from the user's own history too: `grindRpmStepForGrinder(model)` runs the *same* `deriveGrindStep` estimator over the distinct `shots.rpm` values for the active grinder (the per-shot RPM data already exists as an integer column), falling back to `50` when it can't derive (cold cache or <2 distinct RPMs). The widget rounds the result to an int.

Caveat worth stating: an RPM dial is continuous — it has no detents the way a burr collar does — so the "typical increment" is inferred from whatever RPMs the user happened to log rather than from a physical step. The modal-gap estimator still resists a lone outlier, and the `50` fallback covers a fresh grinder, but a user who logs scattered RPMs will see a step that reflects their most common move rather than a round number. That was the accepted trade for parity. The estimator is scale-agnostic, so no algorithm change is needed — RPM just feeds it a different column; the 0.05 floor never binds at RPM magnitudes.

### 8. Combined grind + RPM pill (widget redesign)

The widget was mutually exclusive: `isRpmMode = isKnownRpmGrinder(...)` made it step *either* the burr setting *or* the RPM, hiding the other. A variable-RPM grinder has *both*, so the redesign shows both.

- **One combined pill.** For an RPM-capable grinder with an RPM set, the pill renders both values as `"<grind> · <rpm>"` (e.g. `8.75 · 900`); a non-RPM grinder (or one with no RPM recorded) shows the grind alone, exactly as today. The label stays "Grind".
- **One picker, two sections.** Tapping opens `GrindPickerDialog` with a **Grind** section (burr-increment rows, current highlighted, Finer/Coarser end labels) and, when RPM-capable, an **RPM** section (RPM-increment rows). Picking a row in a section sets only that half — `dyeGrinderSetting` or `dyeGrinderRpm` — via the same write-through as before; the picker stays open across a grind pick so the user can also pick an RPM (or closes on either, TBD in impl — default: close on pick, matching today).
- **Gate reconciliation.** The RPM section/half shows on the broad `grinderRpmCapable(brand, model)` (what BrewDialog and every paired surface uses), not the narrow `isKnownRpmGrinder`. Pairing removes the reason the narrow gate existed (it existed to avoid forcing an unknown grinder into RPM-*only* mode; with grind always shown, that risk is gone). Both steps stay history-derived: grind via `grindStepForGrinder`, RPM via `grindRpmStepForGrinder`.

`GrindPickerDialog` grows from a flat `rows` list to two labeled row groups (grind rows + optional rpm rows) with a section header each; the empty-state and accessibility rules carry over per section.

### 9. Pairing RPM with grind across read/display/serialize surfaces

The audit found the *storage* layer already paired (shot/bag/recipe/equipment structs, DB columns, and the recipe/bag/equipment JSON APIs all carry `rpm` next to grind), but ~30 *read/projection/display* sites thread grind through and drop rpm. The fix is uniform: **wherever a grind value is emitted, emit the sibling `rpm` too, sparsely (only when `rpm > 0`)**, and wherever a grind input is accepted, accept `rpm`.

- **Linchpin first — `ShotProjection` JSON round-trip.** `toVariantMap()` must emit `m["rpm"]` (sparse) and `fromVariantMap()` must read it. This one fix restores rpm to MCP `shots_get_detail`/`shots_compare`, the QML history list row (`model.rpm`), and the clone/coerce round-trip (post-shot metadata edits stop zeroing rpm).
- **AI blocks** (`dialing_blocks.cpp`): per-shot `rpm` on `dialInSessions` entries, `bestRecentShot`, and the adherence `userResponse`; an `int rpm` on `ShotDiffInputs` so `changeFromPrev`/`changeFromBest` diff RPM moves; the prose grind line (`shotsummarizer.cpp`) renders `@ <rpm> RPM` (the already-computed-but-dead `summary.rpm`); `buildShotBlock`/`buildHistoryContext`/`aiconversation` change-detection pair rpm.
- **grinderContext observed RPM.** `GrinderContext` gains RPM summary fields (observed RPMs / min / max / rpmStepSize) populated by `queryGrinderContext` (add an rpm branch), emitted next to `settingsObserved`. Reuses `deriveGrindStep` for the RPM step.
- **MCP inputs/reads**: `shots_list` SQL+emit; `settings_get`/`settings_set` `dyeGrinderRpm`; `shots_update` `rpm` (storage whitelist already maps it); `bag_create`/`bag_update` `rpm`.
- **Comparison model**: add `rpm` to `ComparisonShot`, populate from `record.rpm`, emit in `getShotInfo`, and add an RPM row to `ComparisonShotTable.qml` (shown when any compared shot has rpm).
- **Bag-from-history**: `unifiedbeansearchmodel` history query selects + emits rpm.
- **Network**: fix the Visualizer PATCH to send `grinderSettingWithRpm(...)` (stop clobbering) + add `rpm` to its override maps; ShotServer shot-list SQL, shot-detail JSON, and the shot/bag/recipe edit forms accept/show rpm.
- **QML display**: history row + filter chips, AutoFavorite info/list group data, and a `%RPM%` token in `CustomItem.qml` alongside `%GRIND%`.
- **Shot file import** (`shotfileparser`): for the Visualizer/DYE convention `"<setting> <rpm>rpm"`, split the trailing `rpm` back into `record.rpm` on import so a re-imported shot round-trips; the `.shot` path stays grind-only unless a native key exists.

Sparse emission (`rpm > 0`) keeps legacy/non-RPM shots unchanged — no `rpm: 0` noise appears where a grinder has no RPM axis. RPM stays a **shot-variable** field (like `grinderSetting`): in `dialInSessions` it belongs on the per-shot entry, never the hoisted session `context`.

### 10. RPM only exists for RPM-capable grinders (invariant already holds — no new code)

RPM must only appear for grinders the grinder DB marks RPM-capable (`GrinderAliases::variableRpm`). This invariant is **already maintained by the shipped app**, so the pairing work adds no extraction or gating code — the surfaces simply read the existing `shots.rpm` column:

- The **grind→rpm extraction already happened** in the shipped equipment-packages migration (`migrateFromGrinderColumnsStatic`), which split legacy `"…rpm"`-annotated grind settings into the `rpm` column on upgrade. That migration has run on users' devices; re-touching it would be moot (already migrated) and risky. It is left exactly as shipped.
- **New shots** carry RPM only for RPM-capable grinders because `dyeGrinderRpm` is reset per active package (`SettingsDye::applyPackage` → `setDyeGrinderRpm(pkg.lastRpm)`, which is 0 for a fixed-RPM grinder), and the active package *is* the grinder. So `dyeGrinderRpm > 0` already implies RPM-capable at save time.

Validated against a real user database (Turin DF83V `variableRpm=true` + Eureka Mignon Specialita `variableRpm=false`, ~5.3k shots, RPM historically embedded in the grind field): every grind carrying an RPM token belongs to the DF83V; the Mignon Specialita (2843 shots, settings like "0.5", "1.25", "-0.25") and Niche Zero have zero. The invariant holds on real data with no added code — the database given was a *pre-upgrade* snapshot used only to confirm the shape; the extraction it would need was already done by the migration on real devices.

(An optional belt-and-suspenders save-time gate — `metadata.rpm = grinderRpmCapable(...) ? dyeGrinderRpm : 0` — was considered and left out: it is a no-op under normal operation and would be defensive dead code. It remains an available opt-in if a non-UI writer path, e.g. an MCP `settings_set`, is ever shown to set RPM on a non-RPM grinder.)

### 11. RPM as a first-class coaching + override axis

Surfacing RPM (decisions 8–9) lets the advisor *see* it; two further pieces let it *act* on it, treating RPM as an axis parallel to grind rather than a special case:

- **Recommend + score.** The `nextShot` structured-output schema gains an optional `rpm` (integer), gated in the prompt to variable-RPM grinders and explicitly independent of `grinderSetting` (either, both, or neither may move). `summarizeStructuredNext` renders it, and `computeAdherence` scores it via `rpmMatches` — the same "within tolerance AND the user actually moved from prior" discipline as `grinderMatches`, with a ±25 RPM window (an RPM dial is coarse; the window absorbs rounding without rewarding a shot that ignored the advice). The `grinderCalibration` `coffeeAnchor` also carries the anchor shot's actual RPM, so a variable-RPM recommendation can name the concrete RPM instead of only the existing prose caveat.
- **Override at shot start.** `activateBrewWithOverrides` takes an `int rpm = -1` (< 0 leaves the live RPM untouched — the common case; >= 0 sets it), and `machine_start_espresso` forwards an optional `rpm` override. Independent of the grind override, so a caller can move one, both, or neither for a single shot.

This keeps the "RPM and grind are two independent axes" model consistent from the widget through the payload, the recommendation loop, and the start-a-shot override.

## Risks / Trade-offs

- **Mixed-grinder step when no grinder is selected.** Mitigated by numeric-only + modal-gap; fully resolved once a grinder is chosen. Acceptable per the request.
- **Losing the manual override.** A poisoned step is now fixed by correcting the offending shot in history rather than a setting. This is rarer than the everyday benefit of correct auto-stepping, and aligns with the project's "prefer smarter defaults over settings" stance. If an override is ever needed it belongs *per grinder* (readout option schema on the widget), never as a global setting — which is the category error this change removes.
- **Payload rename.** One-time break for MCP consumers; low blast radius.

## Migration Plan

No data migration. Ship widget + shared estimator + setting removal + pill transparency together. Update tests and the wiki manual in the same change.

## Open Questions

- Floor value (`0.05`) and gap rounding (2 decimals) are reasonable defaults; confirm against the regression corpus if any grinder in test data uses a genuinely finer detent.
