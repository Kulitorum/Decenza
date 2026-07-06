# Make the Shot Plan Widget Configurable

## Why

User feedback on v1.81 (PR #1419 discussion): the shot-plan sentence introduced in #1396 spends ~25–30 characters on connective prose ("Brew … of Espresso, using … at"), and on a 2000 px tablet the plan overflows the screen and is clipped mid-word — even with display options turned off. Users have no control over the *order* of plan items and no way to opt out of the sentence scaffolding. The seven fixed toggles gate content but can't express "show these items, in this order, densely".

## What Changes

- **Replace the seven Shot Plan visibility toggles** (in-app `ScreensaverEditorPopup` and web layout editor) with an ordered **chip bar**: a "Shown" row of draggable chips (drag to reorder, ✕ to remove) and an "Available" row of unused items (tap/＋ to add) — the same interaction pattern as the layout page's zone editor.
- **Seven chips**: Profile, Temperature, Roaster, Coffee, Grind, Roast date, Dose & yield. Profile & temperature — one compound toggle today — split into two independent chips.
- **New "Sentence style" toggle**: ON renders the existing sentence — the scaffold consumes yield/beverage/profile/temperature, remaining chips trail in user order; OFF renders all chips as separator-joined fragments in user order. Default ON (current behavior preserved). With the Temperature chip removed, the sentence drops its "at {temperature}" clause; with the Profile chip removed the sentence has no anchor and rendering falls back to fragments.
- **"Steam plan (while steaming)" stays a separate toggle** — it is a mode behavior, not a display item.
- **New per-instance storage**: ordered `shotPlanItems` string list + `shotPlanSentence` bool. When `shotPlanItems` is absent, derive it from the legacy `shotPlanShow*` booleans in canonical order — the legacy `shotPlanShowProfile` boolean expands to both the Profile and Temperature chips (legacy keys read, never written back). Defaults reproduce today's widget exactly.
- **Web layout editor parity**: the shot-plan checkboxes in `shotserver_layout.cpp` are reworked to edit the same ordered list + sentence toggle, so configurations round-trip between editors.
- **Overflow fix**: the shot-plan text currently clips mid-word at the screen edge (its `elide` never activates because the Text is never width-bound). It SHALL wrap onto a second line — there is vertical room in the widget's normal position — and elide only at the end of the second line.

## Capabilities

### New Capabilities

(none — both affected areas have existing specs)

### Modified Capabilities

- `plan-widgets`: the "sentence content follows the display toggles" requirement becomes "content follows the ordered item list + sentence toggle" — fragment mode joins chips in user order; sentence mode keeps its fixed scaffold with remaining chips trailing in user order, degrading gracefully when Temperature or Profile chips are absent; text wraps to two lines and elides only after that.
- `layout-widget-instance-config`: the "Shot plan display option set" requirement is replaced — ordered `shotPlanItems` list (seven item keys, Profile and Temperature independent) + `shotPlanSentence` + `shotPlanShowSteamPlan` instead of seven booleans; chip-bar editing UI in both editors; legacy-boolean migration on read.

## Impact

- **QML**: `ScreensaverEditorPopup.qml` (chip-bar UI for `shotPlan`), `ShotPlanText.qml` (ordered rendering, sentence toggle, two-line wrap + elide), `ShotPlanItem.qml` (pass-through of the new properties, legacy-boolean derivation).
- **C++**: `src/network/shotserver_layout.cpp` (web editor UI + `/api/layout/item` values become a JSON array for `shotPlanItems`; verify the item-property mechanism round-trips arrays).
- **Persistence**: layout JSON gains `shotPlanItems` / `shotPlanSentence` per shot-plan instance; existing saved layouts keep working unchanged via the read-time derivation.
- **Translations**: chip labels reuse the existing `shotPlanEditor.*` keys; new keys for "Sentence style", "Shown", "Available" and the a11y reorder announcements.
- **No breaking changes**: default configuration renders pixel-identical (modulo the wrap fix) to today's widget.
