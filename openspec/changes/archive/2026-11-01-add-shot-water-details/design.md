# Design

## Context

See proposal.md for motivation and the three delta specs for the behavioral contract. The user clarified the record of interest: **dispensed amount plus configured temperature**, with no new configured-amount display.

- `MainController::currentHotWaterSpecJson()` snapshots vessel intent into `hotWaterJson`; `ShotRecord`, `ShotSaveData`, storage import and `ShotProjection` already carry it. `recipeWaterText()` currently prints its configured amount and temperature. These are not a pour result.
- `DrinkSequenceController` arms on first flow, sequences both orders, and ends on completion, skip, refusal or interruption. It exposes activity/pending state but no durable link between its water cycle and an espresso operation. Non-espresso presentation signals currently carry operation id 0.
- MainController saves espresso on `shotProcessingReady`, using `completedOperationId` through its asynchronous save callback. Americano water follows that settled espresso; long-black water precedes it. `m_lastSavedShotId` is therefore an unsafe association key.
- `MachineState` already integrates actual flowing volume and maintains a hot-water tare baseline. Its `scaleWeight()` can intentionally return a frozen SAW trigger value for display. Existing hot-water learning samples settled raw weight 1.5 seconds after flow stop, but only for SAW-triggered pours.
- Canonical `ShotComparison` owns amount/metric calculations and comparison JSON; `ShotComparisonModel`/`ComparisonService` transport it to shared native/Wasm QML. History carries `ShotProjection` maps. `ShotResultsCard` also consumes comparison metrics on review.
- The wiki documents vessel amounts in grams or millilitres and the existing review/compare flow: [Manual](https://github.com/Kulitorum/Decenza/wiki/Manual). `add-one-tap-americano-long-black` is implemented but retains hardware acceptance tasks; this change does not mark those passed.

## Goals / Non-Goals

**Goals:** Preserve a trustworthy result across both step orders and asynchronous save ordering; reuse the host's measurement and shared presentation paths; add only bounded per-drink capture state and a narrow result write.

**Non-Goals:** Measured outlet/drink temperature, automatic association of arbitrary standalone pours with old shots, manual kettle-water entry, water-only history records, new graphs, changed stop/sequence timing, or new external upload formats. Espresso yield and extraction calculations keep their existing meanings.

## Decisions

### D1. A shot-only result, separate from recipe intent

Add nullable `shots.hot_water_result_json` and one shared typed result/codec (for example `ShotWaterResult` in `src/history/`). Keep `hot_water_json` unchanged for promotion/activation. Carry the result through `ShotSaveData`, `ShotRecord`, `ShotProjection` and its map round-trip. Expose a structured `hotWaterResult` map from that codec to UI/MCP consumers, with this vocabulary:

| Field | Meaning |
|---|---|
| `status` | `dispensed`, `notDispensed`, or `interrupted` |
| `weightG` | Optional net physical-scale water measurement |
| `volumeMl` | Optional flow-derived volume, used when weight is unavailable |
| `amountSource` | `scale`, `flowEstimate`, or `unavailable` |
| `temperatureC` | Optional configured temperature at water flow start |

At most one amount field is populated. Known `notDispensed` is a status, not a fabricated sensor reading; no amount or poured-water temperature is required. NULL result means unrecorded, including legacy shots. A started pour can have a recorded temperature but an unavailable amount. Invalid/non-finite measurements are not zero-filled. Use the codec to validate and normalize every producer/consumer rather than parsing the JSON separately on each page.

The host codec projects legacy `hotWaterJson` as optional `hotWaterSetting` (`hasWater`, validated `temperatureC`) so shared QML does not parse or validate it again. The presentation helper may use that temperature for an old shot with no result, labeled as a saved setting; it never uses `hotWaterJson.volume` as measured water. A present result is authoritative, including explicit missing temperature and `notDispensed`.

**Alternative:** Extend the recipe-shaped `hotWaterJson` with results. Rejected because promotion currently copies it verbatim and intent/result fields would be easy to confuse. Separate per-field database columns would also work, but one small optional result keeps this shot-only aggregate together.

### D2. Measure once on the host, preserving the water reference

Extend the existing hot-water lifecycle in `MachineState` to produce a token-stamped pour result for all observed water flows. Capture configured temperature from effective brew water settings at flow start; do not use group/head temperature or report a sensor measurement. Prefer a valid connected physical scale regardless of the vessel's target mode; otherwise use the already integrated water flow as an explicit estimate.

Reuse and centralize hot-water net-weight handling for SAW and capture. The capture reference must survive a delayed tare acknowledgement and exclude the cup, preceding espresso, and any espresso-specific zero offset. Read the corrected physical data, not the frozen UI trigger value. Track the last valid water reading; a cup lift, scale disconnect or later tare must not replace it with a negative value, zero or another operation's weight. If no trustworthy full physical result exists, use the pour's full flow estimate; do not join a partial scale amount to estimated remainder.

Refactor the existing 1.5-second post-water measurement into one host-owned completion path shared by the result and SAW learning, extending it to manual and volume stops. This is the existing deliberate settling measurement interval, not a new sequencing guard. Continue accepting valid final drops in that interval. A new operation/tare closes capture immediately using the last valid reading before the reset; a token check prevents a delayed callback from sampling the next operation. Preserve the SAW learning eligibility/outlier rules independently. Disconnect while water is running finalizes an interrupted/partial result from already captured data. Disconnect after observed flow stop retains completed status. Result finalization leaves the completion display freeze intact until the next flow cycle. A pending tare clears only on an observed drop toward zero, preserving water added before a delayed reset even with a pre-zeroed cup.

**Alternative:** Save `scaleWeight()` at stop or read it when review opens. Rejected: the former can be the pre-stop frozen value, and the latter can be the next tare, a lifted cup or another drink.

### D3. Correlate sequence, water cycle and espresso operation explicitly

Give each armed sequence a monotonically unique session token and expose token-bearing events for its water cycle, espresso operation association and terminal outcome. Latch these synchronously at machine lifecycle edges before deferred presentation signals run. Continue using `RecipeEditFields::drinkSequence()` for eligibility; do not gate recording on the editable drink-type label or re-read the active recipe at completion.

MainController retains a small per-sequence context containing the water-cycle token, associated espresso operation id, optional result and the eventual saved shot id/UUID. For a long black, keep its water result until the sequenced espresso operation starts and saves. For an Americano, capture its espresso operation when the sequence arms and attach the later water result to that operation's save. An expected second step started manually while pending joins the same capture and cancels the automatic request. Skipping/refusing the water before flow produces `notDispensed`; a started water cycle retains its own completion even if recipe selection changes. A long black with no kept espresso drops the context and creates no water-only row.

Join the two completions in either order: include an available result in the initial shot save; otherwise, when both a successful save and a final result exist, issue one narrow asynchronous update. Never discover the destination from last-shot state or timestamps. Release context after success, failed/discarded save, deletion, or a sequence ending without espresso; an in-flight callback owns only its original token.

**Alternative:** Delay all espresso saves until the whole drink ends. Rejected because it would couple existing shot settling, upload and save behavior to a later water step and risk losing a valid espresso record when that step fails.

### D4. Patch only the water result and refresh without overwriting edits

Add a dedicated background result-update method in `ShotHistoryStorage`, using `withTempDb()` and a shot id/UUID predicate. Update only the result and the normal history export modification timestamp. A deleted shot is not re-created. Return a specific completion/failure and emit a dedicated water-result notification; do not reuse metadata-save completion signals or claim the user's current metadata request succeeded. Surface persistence failure through the existing storage error path, with no silent success or indefinite retry loop.

Forward a shot-specific result notification through `HistoryService` (`.rep`, source and facade together). Review merges only the read-only water result into its persisted/edit clone, preserving unsaved rating/notes fields. Refresh its outcome request; comparison re-queries when its selected shots change through this notification. Maintain existing request ids/stale-response checks, native in-process transport and Wasm reconnect refresh. No new browser HTTP adapter is needed.

### D5. Present amount as an outcome and temperature as a setting

Use `ShotComparison::metricsFor()` and its canonical result builder to add `waterWeightG` and `waterVolumeMl` outcome rows (one decimal), default-visible when supported. Add the normalized saved setting as `waterTemperatureC` in `diffInputs()` with the existing temperature tolerance, translated **Water temp (set)**. It follows changed/unchanged input rules after espresso inputs and is excluded from the advisor’s espresso dial-in input subset. Do not add the configured amount to changed inputs. The result assembler includes source/status so a partial amount stays visible but never receives a full-total delta. Only complete same-source/same-unit outcomes produce amount deltas; rebasing and paging use the same assembler.

On `PostShotReviewPage`, show a compact **Added water** block adjoining Shot results, using the same result projection/formatters, with the amount, source, set temperature and any incomplete state. The adjoining ShotResultsCard must not repeat these amount rows: add a narrow row-selection option for this host page while leaving the default comparison table complete. The recipe card's water context retains vessel identity, with its current unqualified target amount/temperature text removed. A legacy water snapshot gives “Amount not recorded” and its saved set temperature; a plain espresso gives no block. This layout makes water visible without requiring the recipe to resolve.

Keep the formatting in shared QML/`ComparisonText` and labels in `ShotComparisonText`, including mL units and Celsius/Fahrenheit absolute/delta conversion. Extend existing projection/service maps and MCP detail/compare serialization additively; document their unit-bearing fields. Preserve legacy HTML behavior and avoid building a second new-feature layout for `/legacy/`, per `SHARED_UI.md`.

**Alternative:** Only add values to the recipe card or graph summary. Rejected because the result must remain visible without a resolvable recipe and the graph summary already has a compact fixed set of extraction inputs.

## Risks / Trade-offs

- **Late scale tare or cup lift changes the reference** → Centralize water net-weight handling and finalize before the next reset; extend existing machine-state tests with delayed-tare and post-stop samples. The existing frozen display is not evidence of final amount.
- **Water finishes before or after asynchronous save** → Test both completion orders and a later shot starting before the first result write. Operation/sequence tokens, not recency, determine ownership.
- **Flow-derived amount is less direct than weighing** → Preserve mL and `flowEstimate`; never relabel it as measured grams or subtract it from grams.
- **Process termination before a result commits** → The espresso remains saved, with water unrecorded. Do not reconstruct a completed pour from its target. Durable recovery of an in-progress water capture is outside this change.
- **Legacy snapshots cannot prove whether water poured** → Show unavailable amount, never backfill from configured amounts. Keep their saved temperature clearly labeled as a setting.
- **A separate result update arrives during editing** → Merge the water field only and isolate its completion from metadata-save signals.
- **Hardware/GHC sequence acceptance varies** → Reuse existing sequence outcomes and validate this feature on the Mac simulator as requested by the user; record refusal as no dispense without claiming the original sequence change's outstanding hardware acceptance checks passed.

## Migration Plan

1. Add the next forward schema migration and nullable result column to fresh creation. Leave every historical row NULL; do not scan or infer old results.
2. Extend save/load, projection/map coercion, backup/device-transfer import and overwrite/merge paths. Old source databases lacking the column import NULL; current sources copy the result verbatim.
3. Deploy capture, result persistence and shared presentation together. Ship matching native/Wasm packages with the normal host/package checks. Existing third-party readers can ignore additive result data.
4. Rollback to a build that ignores the extra nullable column leaves existing espresso/spec data intact; retain the column/data rather than attempting destructive reverse migration. No external upload contract changes are required.
