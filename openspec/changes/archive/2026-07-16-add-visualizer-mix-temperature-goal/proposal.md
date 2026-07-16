## Why

Visualizer added a second temperature goal series, `temperature.mix_goal`, and now draws "Basket Temperature Goal" and "Mix Temperature Goal" as two distinct lines (visualizer commit `0bba67e`). Decenza cannot populate it: the DE1 shot sample carries both `SetHeadTemp` and `SetMixTemp`, but `DE1Device::onShotSampleReceived` only decodes `SetHeadTemp` (as `ShotSample::setTempGoal`) and drops `SetMixTemp` on the floor. Every Decenza shot on Visualizer is therefore missing a goal line that the platform is now ready to render.

The mix goal is also the missing half of a diagnostic Decenza already half-shows: the "Mix temp" line exists as an advanced series, but with nothing to compare it against, a user cannot see how far the machine's water-in temperature is from what the profile asked for — which is exactly what reveals preheat and temperature-stability problems.

Note that Decenza does **not** share the goal-mislabelling bug that reaprime fixed in tadelv/reaprime#472: Decenza's `temperature.goal` already carries `SetHeadTemp`, matching de1app and Visualizer's "Basket Temperature Goal" label. This change is purely additive.

## What Changes

- Decode `SetMixTemp` from the DE1 shot sample (new BLE spec bytes 11–12, old spec bytes 8–9) into a new `ShotSample::setMixTempGoal` field.
- Carry the mix goal through `ShotDataModel` as a new per-sample goal series, alongside the existing temperature goal series.
- Persist the mix goal series with the shot so re-uploads and queued uploads of past shots include it, and so the shot detail chart can plot it after reload.
- Send it to Visualizer as `temperature.mix_goal`, interpolated onto the elapsed timeline exactly like the existing goal series. Shots with no mix goal data (imported or pre-change) omit the key entirely rather than sending zeros.
- Plot a "Mix temp goal" line on the live shot graph, the history/shot-detail graph, and the comparison graph, following the existing advanced-series pattern: **flagged `advanced: true`, backed by `graph/showTemperatureMixGoal` defaulting to `false`, and only rendered when `shotReview/advancedMode` is on.** It is off by default and invisible to users who never open advanced mode.
- No new settings page entry and no new user-facing toggle beyond the existing graph legend checkbox that every other advanced series already uses.

## Capabilities

### New Capabilities
<!-- None. This extends existing charting and Visualizer upload behavior. -->

### Modified Capabilities
- `charting`: adds the mix temperature goal as an advanced, default-off graph series across the live, history, and comparison graphs, including legend, inspect-bar, and comparison-table entries.
- `visualizer-upload-persistence`: the uploaded shot JSON gains `temperature.mix_goal`, and the persisted shot record gains the mix goal series so pending/re-uploads carry it.

## Impact

**BLE / sampling**
- `src/ble/de1device.h` — `ShotSample` gains `setMixTempGoal`.
- `src/ble/de1device.cpp` — decode both BLE spec layouts; no new BLE traffic, the bytes are already in the packet.
- `src/simulator/de1simulator.cpp` — populate the new field so simulated shots exercise the path.

**Model / plumbing**
- `src/models/shotdatamodel.{h,cpp}` — new goal point vector with matching reserve/clear/trim/flush handling and a QML accessor; `addSample()` signature grows a parameter.
- `src/controllers/maincontroller.cpp` — pass the new sample field through at both `addSample()` call sites.

**Persistence**
- No DB migration: `shot_samples.data_blob` is a qCompress'd JSON object whose series keys are already read optionally (`if (root.contains(...))`). The change adds one `temperatureMixGoal` key to `compressSampleData()` and an optional read in `decompressSampleData()`.
- `src/history/shothistory_types.h` (`ShotRecord`), `shothistorystorage_serialize.cpp`, and `shotprojection.{h,cpp}` (`ShotProjection`, the history→upload and history→chart carrier) each gain the series.
- Backward compatibility: existing shots have no mix goal; the series stays empty and every consumer must treat empty as "not available", never as zeros.

**Upload**
- `src/network/visualizeruploader.cpp` — emit `temperature.mix_goal` next to `temperature.goal` in **both** JSON builders: the live one (`buildShotJson`, ~line 799) and the history/re-upload one (~line 1403).
- Pre-existing gap to fix while here: the history builder never emits `temperature.mix` at all, so re-uploading a past shot silently drops the mix line the live upload sent. The two builders should agree.

**UI**
- `qml/components/ShotGraph.qml`, `HistoryShotGraph.qml`, `ComparisonGraph.qml`, `GraphLegend.qml`, `GraphInspectBar.qml`, `ComparisonDataTable.qml` — register the series, legend row, inspect readout, and comparison column.
- `qml/Theme.qml` — a mix-goal line color (Visualizer uses `#AA3477` against `#EE3377` for basket goal; Decenza's analogue is a dimmed partner to `temperatureMixColor`, matching how `temperatureGoalColor` relates to `temperatureColor`).

**Docs**
- `docs/CLAUDE_MD/VISUALIZER.md` and the wiki manual's graph/advanced-mode section, per the project's user-visible-feature rule.

**Not affected**
- Profile upload, stop limits, shot analysis detectors, and MCP tool payloads.
