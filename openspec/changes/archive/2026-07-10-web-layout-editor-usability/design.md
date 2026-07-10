# Design — Web Layout Editor Usability & QML Parity

## Context

The web layout editor is one self-contained page emitted by `src/network/shotserver_layout.cpp` (~4.5k lines of embedded HTML/CSS/JS in C++ raw strings). It already shares its data model with the in-app editor: the widget catalog, readout capability schema, and display-mode defaults are injected from the same C++ tables (`SettingsNetwork::widgetCatalogJson/readoutCapabilitiesJson/displayModeDefaultsJson`), and every edit hits `/api/layout/*` endpoints that mutate `SettingsNetwork` live. The gaps are all in the editing surface, not the data.

Ground truth for behavior parity is the in-app editor: `qml/pages/settings/SettingsLayoutTab.qml` (selection, confirmations, editor lifecycle, `ensureSettingsAccessible`), `qml/pages/settings/LayoutEditorZone.qml` (chip anatomy: label + gear button + always-present ×), `qml/components/layout/ReadoutOptionsPopup.qml` (labeled option sections and wording), and `qml/components/layout/LayoutPreview.qml` (live 960×600 preview scaled to fit).

Observed defects being fixed (verified live):

- The instruction `<p>` is a direct flex child of `.main-wrapper`, stretching to ~780px wide and pushing the zones into a ~380px column; at ≤~1200px the sticky 340px library panel covers the zones entirely.
- `chipClick()` toggles selection and only opens the bespoke editor on the select transition, so alternating clicks are no-ops; the gear is a non-interactive `<span>`; deselect leaves the editor open.
- Readout options render as unlabeled `.chip-mode` micro-`<select>`s inside the selected chip.
- `removeItem()` deletes immediately (spec requires confirmation for configured widgets); the × exists only while selected, and selection resizes the chip (spec violations).
- `changeOffset`/`changeScale` buttons have no labels/tooltips.
- Web mutations bypass the QML-side `ensureSettingsAccessible()` guard.

## Goals / Non-Goals

**Goals**

- The web editor feels like the in-app editor: same affordances (gear button, hover ×), same option wording, same confirmations, and a live preview.
- Keep the single-source-of-truth architecture: capability schema, catalog, and defaults stay injected from C++; no new hand-maintained type lists.
- Page layout usable from ~1000px up without overlap.

**Non-Goals**

- Cross-zone drag-and-drop (neither editor has it; the `/api/layout/move` endpoint stays as-is).
- Pixel-identical preview rendering. The web preview approximates the QML components with HTML/CSS; faithful layout (zones, order, distribution, alignment, style, offset, scale, label colors), not faithful pixels.
- Undo/redo, framework adoption, or splitting the page into separate served assets.
- Touch-screen drag reorder on the web page (HTML5 DnD is already the existing mechanism).

## Decisions

**D1 — Fix layout with CSS grid, not markup surgery.** Restructure `.main-wrapper` into a grid: instructions span the full width as a top row; below it, `zones | right column (preview + library)` — mirroring the in-app editor's two-column split (`SettingsLayoutTab.qml` puts preview above library on the right). Add a breakpoint (~1100px) that stacks the right column below the zones instead of beside them. Alternative considered: absolute-positioning fixes on the current markup — rejected, the overlap is a symptom of the flex structure itself.

**D2 — Gear becomes a button; chip click stays selection-only.** The gear `<span>` gets an `onclick` that `stopPropagation()`s and always calls the type-appropriate opener (`openEditor` for custom, `openScreensaverEditor` for bespoke, new `openReadoutOptions` for readouts), independent of selection state (it also selects the chip for visual consistency). `chipClick()` no longer opens editors implicitly — matching in-app, where tap selects and the gear/long-press opens options. Deselect and remove both close any open editor. This removes the alternating dead-click entirely rather than special-casing it.

**D3 — Readout options move into the existing editor-panel pattern.** Reuse the `ssEditorPanel` card infrastructure (title, sections, Done, 200ms debounced auto-save) with a readout mode: sections generated from `WIDGET_CAPABILITIES[type]` in schema order, one section per option key, with the same header text, descriptive choice labels, and show-ratio hint as `ReadoutOptionsPopup.qml`. The label strings are a documented mirror of the QML popup (same pattern as the existing choice-list mirrors; noted in both files). The inline `.chip-mode` selects are removed. Alternative: keep inline selects and add labels — rejected, chips can't fit labeled controls without violating the stable-chip-size requirement.

**D4 — Web preview is client-rendered HTML from existing data.** A new preview pane renders a 960×600 (8:5) scaled box from the already-loaded `layoutData` + `WIDGET_CATALOG` + `WIDGET_COLORS`: status bar top, top-left/right bars, three center zones with offsets/scales, lower-mid bar, bottom bars — honoring distribution (packed/equal/spaced), alignment, zone style background, and per-item label/color/emoji. It re-renders from the same `loadLayout()` refresh every mutation already triggers, so live update is free. No new endpoints. Alternative considered: server-side screenshot of the actual QML screen (like `screencaptureservice`) — rejected: heavyweight, couples the editor to the app's current page, and fails when the tablet is mid-shot; revisit only if the HTML approximation proves misleading.

**D5 — Remove-confirmation driven by a `configured` flag in zone JSON.** `SettingsNetwork::itemIsConfigured(itemId)` already exists (used by QML). The `/api/layout` GET response adds `configured: true` per item where applicable; the web × shows a `confirm("Remove this widget and its settings?")` only for configured items. Alternative: an extra round-trip to `/api/layout/item` before removing — rejected, adds latency to every remove and the flag is one boolean.

**D6 — Settings-access guard moves to C++.** Add `SettingsNetwork::ensureSettingsAccessible()` (port of the QML function: scan all zones for a `settings` widget or `custom` with `action == "navigate:settings"`; if none, `addItem("settings","bottomRight")`). Call it from the layout mutation endpoints that can remove access (`remove`, `reset`/zone ops, `item` property writes on custom actions) after the mutation, and have `SettingsLayoutTab.qml` call the same invokable instead of its QML copy. One implementation, both surfaces. Alternative: duplicate the check in web JS — rejected, exactly the kind of drift this codebase avoids.

**D7 — Chip anatomy matches in-app.** The × is always rendered, faint (`opacity:.4`), brightening on chip hover or selection (CSS only); selection no longer appends controls to the chip (D3 removed the inline selects), so chip size is stable. Offset/scale buttons get `title` tooltips ("Move zone up/down", "Zone scale −/+") and the current-value spans get titles too.

## Risks / Trade-offs

- [Preview drift: HTML approximation diverges from real rendering as widgets evolve] → Scope the preview to layout-level truth (placement, order, sizing, colors) and label it "Preview"; widget internals render as simple labeled chips, same as the editor's mental model. The catalog injection means new widget types appear automatically with their chip label.
- [The embedded-JS file grows further (~4.5k → ~5k+ lines)] → Keep additions in clearly-sectioned blocks (`// ---- Readout Options Editor ----`, `// ---- Preview ----`) consistent with the file's existing organization; no splitting in this change (splitting is a separate refactor, see SHOTSERVER.md conventions).
- [Removing inline selects changes muscle memory for existing web users] → The gear button is a strictly more discoverable path to a superset of the same options; guidance text updated in the same change.
- [Guard recursion: `ensureSettingsAccessible` calling `addItem` inside a mutation endpoint] → It appends after the mutation completes and only ever adds (never removes); a one-shot re-entrancy flag is unnecessary since `addItem("settings", ...)` cannot itself remove access.
- [Label mirror drift between ReadoutOptionsPopup.qml and the web JS] → Same accepted trade-off as the existing choice-list mirrors; both sites carry a keep-in-sync comment pointing at each other.

## Migration Plan

No data migration: no stored-layout format changes; `configured` is additive in the GET payload. Ship as one PR; web page is regenerated on app update, no cache concerns (page is served fresh per request). Rollback = revert the PR.

## Open Questions

- Should the web preview also render on the phone-size layout variant, or is the 8:5 device reference enough for v1? (Default: 8:5 only, matching `LayoutPreview.qml`.)
- Tooltip-only labeling for offset/scale on touch browsers has no hover; acceptable for v1 or add a compact text label? (Default: tooltips + `aria-label`s; revisit if feedback warrants.)
