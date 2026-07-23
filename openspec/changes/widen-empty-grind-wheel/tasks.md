## 1. Widen the empty/unparseable grind wheel (#1605)

- [x] 1.1 In `qml/components/GrindRowSource.qml`, add a median-anchor helper: from the injected grinder's observed settings (`getDistinctGrinderSettingsForGrinder(grinderModel)`), take the **numeric subset**, sort numerically, and return the middle value as a string; return `""` when there is no numeric observed history.
- [x] 1.2 In `grindRowsFor(cur)`, when generation collapses (`generated.length <= 2`) AND the median anchor is non-empty, generate the wide `±grindWindowSteps` window **around the anchor** (reusing the existing `stepGrind` lattice); if that window has > 2 rows, return it. Otherwise fall through to `_observedFallback`.
- [x] 1.3 Mark the anchor's canonical row `isCurrent` so `_centerWheels` centres deterministically on the median (not the array midpoint).
- [x] 1.4 Lift the fixed row cap in `_observedFallback` (`out.length < 11`) so the genuinely-unparseable-with-history path offers all observed settings, not ~10, and keep the unparseable current value as its centred `isCurrent` row.
- [x] 1.5 Confirm the text-mode trigger in `GrindPickerDialog.onAboutToShow` (`textMode = _grindRows.length < 2`) now resolves to text mode ONLY when there is no numeric basis (empty/unparseable AND no history) — i.e. the wide-wheel path keeps `_grindRows.length >= 2`.

## 2. Commit semantics

- [x] 2.1 Verify Done-without-spinning on the median-anchored wheel commits the median (grind's existing centred-value commit already does this — confirm no empty `grindPicked("")` is emitted from the anchored-empty state). Do **not** add a neutral-anchor placeholder gate for grind (per reporter: an untouched Done should write the median, not nothing).

## 3. Validation

- [x] 3.1 Extend the standalone row-generation simulation (`scratchpad/sim.js` equivalent) with the new anchor path: empty value on a DF83V-style history yields a wide window (hundreds of rows) centred on the median, with observed values present; a no-history numeric grinder yields a wide window on the default; an unparseable value with no history yields the text-mode signal (< 2 rows).
- [x] 3.2 Build via Qt Creator MCP (`mcp__qtcreator__build`); clean, no new QML AOT warnings.
- [x] 3.3 Run the full suite via Qt Creator MCP (`run_tests`, scope `all`) — no regressions. (No QML test harness; `GrindRowSource` logic is covered by the simulation in 3.1.)
- [x] 3.4 Manual verification on the real app (user-driven): create a NEW recipe with the DF83V selected and no bag grind → the grind picker opens on a wide, spinnable wheel centred on the median, not a ~10-item list; the RPM wheel is unaffected. Also spot-check that the brew bar, post-shot review, and change-beans pickers (values already set) are unchanged.

## 4. Docs / manual

- [x] 4.1 Check the wiki Manual's Grind / recipe-creation section (if present); confirm wording still matches (a new recipe opens on a spinnable range, not a short list). Update only if it describes the old capped behaviour.

## 5. Archive

- [ ] 5.1 After merge, archive this change and sync `grind-value-entry` specs as the final commit on the PR (per project convention).
