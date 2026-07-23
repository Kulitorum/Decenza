## Why

[#1605](https://github.com/Kulitorum/Decenza/issues/1605) (reporter @fredphoesh / Mark Palmos): when creating a **new recipe**, the grind picker's wheel offers only ~10 values — his own logged DF83V settings (`0.2, 03…09`) — instead of the wide, freely-spinnable range the same widget shows everywhere else. He confirmed the grinder *is* selected (the RPM wheel is present, so a real RPM-capable catalog grinder — his Turin DF83V — resolved), and that the grind wheel "should never be limited to 10 values."

It is **one shared control** on every surface (`GrindField` → `GrindPickerDialog` → `GrindRowSource`, from `replace-grind-inputs-with-picker`), so this is not a recipe-specific widget. A deterministic simulation of `GrindRowSource.grindRowsFor()` against his DF83V confirms the trigger precisely: for any **parseable** value the wheel generates 801 rows (wide); it collapses to the capped observed-history fallback in exactly **one** case — an **empty** grind value. Recipe creation is the only workflow that routinely opens the picker on an empty grind: a new recipe only inherits a grind if a linked bag carries one (`RecipeWizardPage.qml`), so with no bag — or a bag without a dialed grind — `fGrind` is `""`. Empty in → 0 generated rows → `_observedFallback("")` → his first ~11 settings, and the wheel centres on the middle one (which reads as "07 selected" — that green row is the centred selection band, not a stored value).

Every other surface edits a value that already exists (the live dial-in, a shot's recorded grind, a bag's grind), so it never hits this branch. The cap is not a per-widget miss; it is the empty-value fallback in the shared control.

The current [grind-value-entry](../../specs/grind-value-entry/spec.md) spec *deliberately* keeps an empty/unparseable value on a short observed-history wheel when history exists. This change narrows that: when the grinder has a numeric basis, the wheel SHALL synthesise a **wide window seeded at the median observed setting**, with observed history demoted to enrichment — never the sole, capped content.

## What Changes

- **A wide wheel on an empty grind.** When `grindRowsFor()` would collapse (empty or unparseable value) but the grinder has a numeric basis — observed numeric settings in history — the control generates the full ±hundreds-of-steps window around a numeric **anchor**: the **median** of the grinder's observed numeric settings. Because the wheel's step is derived from that same history, the user's habitual settings fall on the lattice and appear within the window naturally. The observed-history list is no longer the wheel's whole content and is no longer capped. A grinder with **no** numeric history has no basis and opens in text mode, as today.
- **Median commits on Done (no empty-grind path).** The wheel opens centred on the anchor; pressing Done without spinning commits the median. A recipe should end up with a grind, so an untouched Done writes the median default rather than nothing — the reporter's stated preference. No "placeholder" gate is added for grind.
- **Text mode only when there is no numeric basis at all** — no value and no history, or an unparseable value with no history — unchanged from today.
- **Unparseable value with history stays on the wheels, uncapped** — a lattice cannot be centred on an unparseable string, so its own value remains the centred row, but the offered history is no longer truncated to ~10.

## Capabilities

### New Capabilities

_None — this refines existing grind-picker behaviour._

### Modified Capabilities

- `grind-value-entry`: the empty/unparseable-value fallback becomes a **wide, median-anchored wheel enriched with observed history** (uncapped) whenever the grinder has a numeric basis, rather than a capped ~10-row observed-history list. Text-mode-on-open is narrowed to the no-numeric-basis case. Applies to every adopting surface; only the empty-open case (new recipe) changes in practice.

## Impact

- QML: `qml/components/GrindRowSource.qml` — `grindRowsFor()` collapse branch and `_observedFallback()` (add a median/default anchor + window synthesis; lift the 11-row cap). Behaviour is derived entirely from the injected grinder context, so no host changes are needed.
- No C++ change required: `grindStepForGrinder()` / `getDistinctGrinderSettingsForGrinder()` already expose the observed settings the anchor is derived from. (If a shared median helper is cleaner than computing it in QML, it can live beside `grindStepForGrinder` — decided during implementation.)
- No new Settings, no BLE/protocol change, no DB migration.
- QML is verified manually (no QML test harness) — the `GrindRowSource` row-generation logic is additionally covered by the standalone row-generation simulation used to diagnose this issue.
- Manual/wiki: confirm the Grind picker section of the wiki Manual, if any, still reads correctly (a new recipe now opens on a spinnable range, not a short list).
