## Why

When an espresso finishes, the cup fill view blanks instantly — the coffee disappears while the espresso page is still on screen for the ~3 s stop-overlay window, and while the weight text above it still reads the final dose. The cup is supposed to hold the coffee it just drew until the screen changes.

The cause is a phase gate in `cupGeometry()` ([`qml/components/CupFillView.qml:87`](../../../qml/components/CupFillView.qml)): `hasExtraction` lists only `Preinfusion`, `Pouring` and `Ending`. When the DE1 leaves `State::Espresso`, `MachineState` maps the machine to `Phase::Ready` (or `Idle`/`Heating`), the gate goes false, and `fillRatio` collapses to 0. The comment above that gate says "during/after extraction", but the "after" half was never implemented — the gate was added (#855) to keep a *pre*-flow residual scale weight from drawing a filled cup during `EspressoPreheating`.

A second blank path exists independently: `if (root.currentWeight <= 0) return` in the liquid canvas. Lifting the cup or portafilter during the hold empties it even with the phase gate fixed.

## What Changes

- Latch a per-instance `extractionSeen` flag when the view first observes `Preinfusion` or `Pouring`, and keep drawing the fill while it is set, so the cup survives the phase transition out of the espresso cycle.
- Capture the final weight into a held value at that transition and render the fill, crema and the view's own weight text from the held value, so the cup and its number stay consistent and survive the cup being lifted off the scale.
- Clear both on entry to a new espresso cycle, so a subsequent shot on the same view instance starts empty.
- Keep the pre-flow behaviour that #855 introduced: with no extraction yet seen, `EspressoPreheating`/`Ready` still render an empty cup regardless of what the scale reports.
- No animation restart. `animTimer` already stops when the phase leaves the espresso range; the hold is a frozen final frame, not 3 s of extra repaints.

Not in scope: the espresso page's own info-bar weight readout, which continues to track the live scale, and any change to `MachineState`'s published `scaleWeight`.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `cup-fill-view`: adds a requirement that the rendered fill persists from the end of extraction until the view is destroyed, and that it is drawn from a held weight rather than the live scale reading during that window.

## Impact

- `qml/components/CupFillView.qml` — the only file changed. Roughly ten lines: two new properties, two derived readonly properties, a phase-change handler, and routing the existing fill/crema/text reads through the held value.
- No C++ change, no schema change, no new QML file, so no `CMakeLists.txt` edit.
- No automated test. QML behaviour in this project is verified manually; the change is confirmed by running a shot (or an aborted shot) with the cup fill view selected.
- No wiki manual entry — this restores intended behaviour rather than adding a user-visible feature.
