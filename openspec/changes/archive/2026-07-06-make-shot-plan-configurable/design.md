# Design — Make the Shot Plan Widget Configurable

## Context

The Shot Plan widget (`qml/components/layout/items/ShotPlanItem.qml` wrapping `qml/components/ShotPlanText.qml`) renders the upcoming brew as a sentence ("Brew 42.0g of Espresso, using D-Flow / 42 at 88°C") followed by a separator-joined tail of optional segments. Content is gated by seven per-instance booleans (`shotPlanShow*`) stored via the layout item-property mechanism (`Settings.network.setItemProperty` / `getItemProperties`, mirrored by the web editor's `/api/layout/item` endpoint). The in-app editor is `ScreensaverEditorPopup.qml` (the shared per-widget options dialog); the web editor's checkboxes live in `src/network/shotserver_layout.cpp` (~lines 3041–3097).

User feedback (v1.81): the sentence prose wastes width, the plan overflows and **clips mid-word** at the screen edge (the `Text`'s `elide` never activates — it is never width-bound inside its plain `Row`), and there is no control over item order or density.

The layout page already has a complete chip drag-reorder implementation in `qml/pages/settings/LayoutEditorZone.qml`: DelegateModel live-swap during drag, `Flow` move transitions, and accessible move-left/right fallback buttons when a screen reader is active.

## Goals / Non-Goals

**Goals:**
- User-composed, ordered shot-plan content: add / remove / reorder seven display items via chips.
- Sentence style becomes an explicit per-instance toggle; fragments in user order are the base format.
- Split Profile and Temperature into independent items.
- Editors (in-app popup and web layout editor) stay in parity; configurations round-trip.
- Existing saved layouts render identically without migration writes.
- Overflow wraps onto a second line and elides only after that — never clips.

**Non-Goals:**
- No finer splitting of the remaining compound items (Dose & yield stays one chip; Grind keeps its RPM suffix).
- No changes to the Steam Plan sentence content or the page-aware steam swap (the `shotPlanShowSteamPlan` toggle stays as-is).
- No changes to other configurable widgets or the layout page itself.
- No auto-degrade-by-width between sentence and fragment modes (explicit toggle instead; can be revisited).

## Decisions

### D1 — Item keys and canonical order

`shotPlanItems` is an ordered JSON string array. Item keys: `"doseYield"`, `"profile"`, `"temperature"`, `"roaster"`, `"coffee"`, `"grind"`, `"roastDate"`.

Canonical (default) order matches today's fragment fallback order in `ShotPlanText._build()`: `["doseYield", "profile", "temperature", "roaster", "coffee", "grind", "roastDate"]` — with `roastDate` **not** in the default list (the only legacy toggle that defaults OFF). Default list: `["doseYield", "profile", "temperature", "roaster", "coffee", "grind"]`.

*Why:* keys mirror the legacy boolean names minus the `shotPlanShow` prefix, so migration and web-editor code read naturally. Canonical order reproduces today's rendering.

### D2 — Rendering semantics (sentence × order)

`ShotPlanText` gains `itemOrder` (list of keys) and `sentence` (bool) properties, replacing the six `show*` booleans. One shared `_build(fmt, sep)` continues to produce both plain (a11y) and rich text.

- **Fragment mode** (`sentence: false`): each present item renders its segment (same formatters as today — "18.0g in", "grind 22 · 90 rpm", "roasted {date}", yield-override arrow, temperature-override highlight) joined by the separator **in `itemOrder` order**. `doseYield` renders as two adjacent segments (yield, dose-in) at its slot, as today.
- **Sentence mode** (`sentence: true`): the scaffold consumes `doseYield`, `profile`, `temperature` (wherever they sit in the order) plus the beverage word; every other present item trails after the sentence in `itemOrder` order. Scaffold variants:
  - profile + temperature + yield → "Brew {yield} of {beverage}, using {profile} at {temp}" (existing `shotplan.sentence`)
  - profile + temperature, no yield → "Brew {beverage}, using {profile} at {temp}" (existing `shotplan.sentenceNoYield`)
  - profile, **no temperature chip** → new templates "Brew {yield} of {beverage}, using {profile}" / "Brew {beverage}, using {profile}" (new translation keys)
  - **no profile chip** → no sentence anchor; render fragment mode for all items (matches today's degrade rule when the profile name is missing).
- **Cleaning/descale profiles** keep the existing warning sentence unconditionally (it is a safety notice, not a plan) in both modes.

*Why:* chip order is authoritative where it can be (fragments, sentence tail); the sentence's internal word order belongs to the translated template, not the chips. Alternative — disable reordering in sentence mode — rejected: the tail is still meaningfully orderable.

### D3 — Two-line wrap + elide (the clip fix)

`ShotPlanText`'s `Text` becomes width-bound: the component fills the width its parent grants (minus the icon), sets `wrapMode: Text.Wrap`, `maximumLineCount: 2`, `elide: Text.ElideRight`, `horizontalAlignment: AlignHCenter`. `implicitWidth` remains the natural single-line width (so narrow content stays centered and compact in zones that size-to-content); the parent zone's width cap is what triggers wrapping. `ShotPlanItem` must propagate the granted width down (its full/compact content Items currently only `anchors.centerIn` — they need explicit width binding to `Math.min(implicit, available)`).

*Why:* wrapping preserves all content in the common overflow case (user's normal position has vertical room); elide after two lines is the never-clip backstop. Rich text with `<b>` spans works with Wrap+maximumLineCount+Elide in Qt 6's `Text.StyledText`.

### D4 — Storage and migration (read-time derivation, no writes)

Per-instance keys:
- `shotPlanItems`: JSON array of item-key strings (order = display order).
- `shotPlanSentence`: bool, default `true`.
- `shotPlanShowSteamPlan`: unchanged bool, default `true`.

Read path (in `ShotPlanItem.qml`, single derivation function): if `modelData.shotPlanItems` is a non-empty array → use it. Otherwise derive from legacy booleans in canonical order: `shotPlanShowDoseYield≠false → doseYield`, `shotPlanShowProfile≠false → profile, temperature` (one legacy boolean expands to both chips), `shotPlanShowRoaster≠false → roaster`, `shotPlanShowCoffee≠false → coffee`, `shotPlanShowGrind≠false → grind`, `shotPlanShowRoastDate===true → roastDate`. `shotPlanSentence` absent → `true`.

Legacy `shotPlanShow*` display keys are never written by the new editors; both editors write only the new keys on save. A layout never touched by the new editor keeps only legacy keys and keeps rendering identically.

*Why read-time over write-time migration:* zero risk to users who never open the editor; layouts shared/exported between app versions stay compatible both directions (an old app ignores the new keys and uses the still-present legacy booleans only if the user never re-saved — acceptable; after a re-save an old app falls back to all-defaults, a minor and rare regression).

Verify the item-property mechanism round-trips a JSON array value end-to-end (QML `setItemProperty` → layout JSON → `getItemProperties` → QML, and the web `/api/layout/item` POST). If the web endpoint only accepts scalars today, extend it to pass arrays through.

### D5 — In-app editor: mini chip bar inside ScreensaverEditorPopup

For `itemType === "shotPlan"`, the toggle column is replaced by:
- **"Shown" row**: a `Flow` of chips in `shotPlanItems` order. Drag to reorder (reusing the LayoutEditorZone pattern: DelegateModel live-swap + `move` transition + persist on release), ✕ to remove. Accessible fallback: move-left/right buttons per chip when a screen reader is active, mirroring `LayoutEditorZone`.
- **"Available" row**: chips for the unused items; tap (or a ＋ affordance) appends to Shown.
- **Toggles**: "Sentence style" and the existing "Steam plan (while steaming)".
- **Live preview line**: a read-only `ShotPlanText` bound to the popup's working state, so reorder/toggle effects are visible before Save.

Dialog edits a working copy; Save writes `shotPlanItems` + `shotPlanSentence` + `shotPlanShowSteamPlan` via `setItemProperty`. Widen the dialog for `shotPlan` (today's fixed `Theme.scaled(320)` is too narrow for a chip bar; use something like `Math.min(Theme.scaled(560), parent.width - margins)` for this item type).

The chip drag logic is small enough to implement inside the popup; extracting a shared reorderable-chip-row component out of `LayoutEditorZone` is *optional* and only worth it if it falls out naturally — `LayoutEditorZone`'s chips carry zone-specific behavior (selection, options gear, palette cross-zone drag) that we don't want.

*Why a working copy:* the popup already has Cancel semantics; live-writing item properties on each drag would make Cancel a lie.

### D6 — Web editor parity

`shotserver_layout.cpp`'s shot-plan section replaces the six display checkboxes with an ordered list UI: the seven items rendered as rows or chips with up/down (or HTML drag-and-drop) reordering and add/remove, plus the Sentence style and Steam plan checkboxes. On load: same derivation rule as D4 (prefer `shotPlanItems`, else derive from legacy booleans, expanding `shotPlanShowProfile` to profile + temperature). On save: POST the new keys only.

Keep the web implementation deliberately simpler than the QML one (up/down buttons are fine; HTML5 drag is optional polish) — parity of *capability*, not of interaction flourish.

## Risks / Trade-offs

- [Rich-text wrap/elide combination misbehaves on some Qt styles/platforms] → Verify `Text.StyledText` + `wrapMode` + `maximumLineCount` + `elide` on Android and iOS early (task 1 of implementation); if elide fails with StyledText, fall back to hiding trailing items until the two-line fit succeeds (measure via `TextMetrics`), which is also the better-looking degrade.
- [Old app versions reading a re-saved layout lose the shot-plan config (legacy keys stale-but-present)] → Accepted: legacy keys are left untouched (not deleted), so an old app sees the configuration as of the last old-editor save, or defaults. Rare, cosmetic, self-heals on upgrade.
- [Item-property mechanism may not round-trip JSON arrays] → Checked first in implementation; if it is scalar-only, either extend it (preferred) or encode the list as a comma-joined string (fallback; still one key).
- [Sentence mode with unusual chip sets reads oddly (e.g. temperature without profile)] → Covered by D2's explicit degrade table; specs pin each variant with scenarios.
- [Two editors × new UI = drift risk] → Both editors share the storage contract (D4) and the same derivation rule; spec scenarios test round-tripping in both directions.

## Migration Plan

Pure read-time derivation (D4) — no data migration step, no version gate. Rollback = revert the code; layouts saved with new keys degrade to defaults on old builds (legacy keys still present from before, or absent → defaults). Nothing to clean up.

## Open Questions

- Should the "Available" row offer a hint when empty ("all items shown")? — cosmetic, decide in implementation.
- Exact new translation keys for the temperature-less sentence variants — finalize wording during implementation (`shotplan.sentenceNoTemp`, `shotplan.sentenceNoYieldNoTemp` proposed).
