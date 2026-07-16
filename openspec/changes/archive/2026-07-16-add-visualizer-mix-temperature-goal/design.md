## Context

The DE1's `ShotSample` BLE packet carries two temperature setpoints. Decenza decodes only one:

| Field | New spec (19B) | Old spec (17B) | Decenza today |
|---|---|---|---|
| `SetMixTemp` | bytes 11–12 | bytes 8–9 | **not decoded** |
| `SetHeadTemp` | bytes 13–14 | bytes 10–11 | `ShotSample::setTempGoal` (`de1device.cpp:448,460`) |

`setTempGoal` is already SetHeadTemp, matching de1app (`de1_de1.tcl:540`, `gui.tcl:3530` both feed `espresso_temperature_goal` from `SetHeadTemp`) and Visualizer's "Basket Temperature Goal" label. **Decenza does not have the goal-mislabelling bug that tadelv/reaprime#472 fixes** — that PR both corrected reaprime's `goal` and added `mix_goal`; only the latter half applies here.

Relevant current state:
- `ShotDataModel` holds parallel `QVector<QPointF>` series. `m_temperatureGoalPoints` is appended in `addSample()` (`shotdatamodel.cpp:271`) with reserve/clear/trim/flush handling scattered across the file.
- Sample series persist as a `qCompress`'d JSON object in `shot_samples.data_blob`, **not** as columns. `decompressSampleData()` already guards optional series with `if (root.contains(key))` (`shothistorystorage.cpp:1725–1739`).
- Two independent Visualizer JSON builders exist: live (`buildShotJson`, from `ShotDataModel*`, ~line 799) and history/re-upload (from `ShotProjection`, ~line 1403). They have already drifted — the history builder omits `temperature.mix`.
- Advanced graph series follow a fixed pattern: a `graph/show<Series>` bool setting defaulting `false`, an `advanced: true` flag in the series/legend/table descriptors, and rendering gated on `advancedMode` (`shotReview/advancedMode`, also default `false`).

## Goals / Non-Goals

**Goals:**
- Populate Visualizer's `temperature.mix_goal` for both new uploads and re-uploads of shots recorded after this change.
- Make the mix goal available as an advanced, default-off line on the live, history, and comparison graphs.
- Keep the two Visualizer JSON builders in agreement.

**Non-Goals:**
- Changing `temperature.goal` semantics. It is already correct; touching it would corrupt every existing Decenza shot on Visualizer.
- Back-filling mix goal for shots recorded before this change, or for imported `.shot` files. de1app has no `espresso_temperature_mix_goal` vector, so imported shots will never carry one.
- A settings-page entry, or any new user-facing control beyond the legend checkbox every advanced series already has.
- Feeding the mix goal into shot analysis detectors, MCP payloads, or the temperature-stability badge.

## Decisions

**1. Decode `SetMixTemp` into a new `ShotSample::setMixTempGoal` rather than reusing `setTempGoal`.**
The two setpoints differ by design (the DE1 drives mix temperature above basket target to compensate for group heat loss), so they are genuinely two series. Alternative considered: derive mix goal from the profile's frame temperature instead of BLE. Rejected — the profile value is what was *asked for*, while `SetMixTemp` is what the machine's controller *actually targeted* after its own compensation, which is the number worth plotting against measured mix temp.

**2. Persist via a new optional blob key, no DB migration.**
Add `root["temperatureMixGoal"]` in `compressSampleData()` and a `contains()`-guarded read in `decompressSampleData()`. Old blobs simply lack the key and yield an empty vector. Alternative considered: a `shot_samples` column or a schema-version bump. Rejected — the blob is explicitly designed for additive series, and every existing optional series (`temperatureMix`, `darcyResistance`, …) already works this way.

**3. Empty means "unavailable", never zeros.**
Every consumer must gate on `!isEmpty()`. This is the load-bearing rule of the change: `interpolateGoalData()` returns an array of zeros when handed an empty goal vector (`visualizeruploader.cpp:85–90`), so an unguarded call would upload a flat 0 °C mix-goal line for every pre-change and imported shot — visibly wrong on Visualizer and worse than sending nothing. The existing `temperature.mix` handling shows the correct shape: `if (!temperatureMixData.isEmpty()) { ... }`. Same rule for the graphs: no data → series not rendered, rather than a line pinned at zero.

**4. Fix the history builder's missing `temperature.mix` in the same change.**
The two builders must produce the same shape or re-upload silently degrades a shot. Adding `mix_goal` to only one would deepen the existing drift; the file is being touched anyway.

**5. Follow the existing advanced-series pattern verbatim; no new pattern.**
`graph/showTemperatureMixGoal`, default `false`, `advanced: true`, gated on `advancedMode` — exactly how `showTemperatureMix` behaves. This satisfies "off by default, advanced only" without inventing a mechanism, and the series appears in the legend right beside "Mix temp", which is the line it is meant to be read against.

**6. Color: a dimmed partner to `temperatureMixColor`.**
`Theme.qml` already encodes the relationship "goal = washed-out version of measured" (`temperatureColor #e73249` → `temperatureGoalColor #ffa5a6`). The new `temperatureMixGoalColor` applies that to `temperatureMixColor #ce93d8`, and like every other Theme color it must route through `_c()` + `Settings.theme.customThemeColors` so custom themes can override it. Visualizer's own `#AA3477` is not adopted — Decenza's palette is its own.

**7. `addSample()` grows a parameter rather than gaining an overload.**
It has only two call sites (`maincontroller.cpp:3407`, `:3762`) plus tests; an overload would leave the goal series silently empty at whichever site wasn't updated. A compile error at both sites is the safer outcome.

## Risks / Trade-offs

- **Byte-offset error in the old-spec (17B) decode path** → The two layouts disagree, and old-spec hardware is rare enough that a mistake could ship unnoticed. Mitigate with unit tests decoding a known byte buffer for *both* layouts, asserting mix and head goals are distinct and not transposed.
- **Zero-filled mix_goal reaching Visualizer for old shots** → Guard on `!isEmpty()` in both builders; add a test that a projection with no mix goal produces JSON with no `mix_goal` key at all.
- **Series desync in `ShotDataModel`** → The vectors are kept parallel by hand across reserve/clear/trim/flush; missing one causes stale points to survive a shot boundary or a chart to lag. Mirror `m_temperatureGoalPoints` at *every* site it appears rather than only in `addSample()`.
- **Chart clutter for advanced users** → Two nearly-overlapping temperature goal lines. Accepted: default-off, and the pairing with "Mix temp" is the point.
- **Simulator not updated** → Simulated shots would upload an empty mix_goal and the line would never appear in testing, hiding regressions. Populate `setMixTempGoal` in `de1simulator.cpp:416`.

## Migration Plan

No data migration. Shots recorded before the change keep working and omit the series; shots recorded after carry it. Rollback is a straight revert — a reverted build ignores the extra blob key it doesn't know about, and Visualizer treats a missing `mix_goal` as legacy data by design.

## Open Questions

- Should the comparison graph/table carry the mix goal too, or only the live and history graphs? Currently planned for all three for consistency with `showTemperatureMix`, but the comparison table is column-tight (`ComparisonDataTable.qml:24` already shrinks columns in advanced mode) and a fifth advanced column may not earn its space.
