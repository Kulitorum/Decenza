## Context

The zone layout editor exists in two forms backed by the same model (`SettingsNetwork`, layout JSON in QSettings):

- **In-app (QML):** `qml/pages/settings/LayoutEditorZone.qml` renders each zone's widgets as chips in a `Flow`. Tapping a chip selects it (`root.selectedItemId`), which reveals inline `◀`/`▶` move arrows (lines ~268–337) and a `×` remove control. Long-press (`onPressAndHold`, lines ~364–368) opens the options editor, but only for a **hardcoded list** of types. Routing of which popup opens lives in `SettingsLayoutTab.qml` `openCustomEditor()` (another copy of the type list). Only `custom` chips get a visible orange border; the other configurable types (`scaleWeight`, `shotPlan`, `sleep`, `machineStatus`, `temperature`, `steamTemperature`, screensavers) have **no** visible "I have options" cue.
- **Web:** `src/network/shotserver_layout.cpp` generates HTML/JS. It mirrors the in-app UX: select a chip, inline `&#9664;`/`&#9654;` arrows call `reorder(zone, from, to)` → `POST /api/layout/reorder`. Configurable widgets are edited via inline `<select>`s or side panels opened on click — again with a **third copy** of the configurable-type list.

Persistence already exists and is untouched by this change: `SettingsNetwork::reorderItem(zone, from, to)` and the item-property mechanism (`setItemProperty`/`getItemProperties`, `/api/layout/item`).

Constraints from CLAUDE.md that shape this design: no Unicode glyphs as icons (use SVG `Image` from `qrc:/icons/`); every interactive element needs `Accessible.role`/`name`/`focusable`/`onPressAction`; no timers as guards; fix a11y violations in files we touch.

## Goals / Non-Goals

**Goals:**
- Replace select-then-arrow reordering with drag-and-drop in both editors.
- Make it obvious, at a glance, which widgets have options (persistent indicator), and give an explicit visible way to open them.
- Collapse the duplicated "which types are configurable" lists into one source of truth.
- Keep the editors at parity and keep all persistence/model behavior identical.
- Remove the surrounding rough edges: stale guidance, unguarded destructive actions, an unscannable widget picker, raw/inaccessible controls, and stacked options popups.

**Non-Goals:**
- Cross-zone drag (moving a widget from one zone to another by dragging). Out of scope; reorder stays within a zone.
- Redesigning the per-widget option popups themselves (their contents are unchanged).
- Changing the layout JSON schema or the reorder/item-property APIs.
- Adding new user-facing settings.

## Decisions

### 1. Single source of truth for "type has options"

Add a single authority for configurable widget types and consume it everywhere (in-app indicator, in-app open gesture/affordance, web indicator, web open). Options considered:

- **(A) C++ helper on `SettingsNetwork`** — e.g. `Q_INVOKABLE bool typeHasOptions(const QString& type) const`, backed by a static set. Both editors already go through `Settings.network` / the C++ backend, so QML calls it directly and `shotserver_layout.cpp` calls the same C++ function when emitting HTML. **One definition, both editors.** ✅ Chosen.
- (B) QML singleton with the list — clean for QML but the web editor (C++) can't share it, so the list would still be duplicated. Rejected.
- (C) Leave lists inline but add a comment — fails the "stay in sync" goal. Rejected.

Chosen: **(A)**. The existing hardcoded lists in `LayoutEditorZone.qml onPressAndHold`, `SettingsLayoutTab.qml openCustomEditor()`, and `shotserver_layout.cpp` are replaced by calls to this helper. (`openCustomEditor` still needs per-type routing to the *right* popup, but the gate "does this open anything" comes from the helper.)

### 2. Reuse existing QML building blocks rather than hand-rolling

Before writing new interaction code, the in-app editor should adopt components the project already ships, which are a closer fit than the current raw `Text`+`MouseArea` chips:

- **`FavoritesListView.qml`** — the in-house reorderable list (profile favorites, bean presets). It already solves live drag-swap (`DelegateModel.items.move()`), displaced-row animation, edge autoscroll, a one-shot `rowMoved(from, to)` signal, a `trailingActionDelegate` slot, a delete column, and accessible long-press via `AccessibleTapHandler`. Its `rowMoved(from, to)` maps **exactly** onto `SettingsNetwork::reorderItem(zone, from, to)`, and `rowLongPressed`/the long-press action maps onto "open options."
- **`StyledIconButton`**, **`AccessibleTapHandler`**, **`AccessibleButton`** — replace the raw `Text`+`MouseArea` controls used today for the arrows/remove (which also violate the project's accessibility rules for touched files).
- **Icon assets already in `resources/icons/`**: `settings.svg` (has-options indicator / open-options button), `cross.svg` (remove), `list.svg` (drag handle), with `more-vertical.svg`/`edit.svg` as alternates. No new icon asset is required.

Orientation caveat and chosen approach: `FavoritesListView` is a **vertical `ListView`**; layout zones are **horizontal, wrapping `Flow`s**, and a horizontal `ListView` cannot wrap. Options considered:

- (A) Generalize `FavoritesListView` to a horizontal/flow mode — rejected: `ListView` doesn't wrap, and it's a shared component (regressing favorites/bean presets is a real risk for a cosmetic win).
- (B) Switch zones to a horizontal-scrolling `ListView` — rejected: loses the wrapping that lets a busy zone (e.g. the status bar) show all widgets at once; changes the editor's visual model.
- **(C) Keep the wrapping `Flow`, but rebuild the chip interactions on the same proven primitives** `FavoritesListView` uses — `DelegateModel` + `Drag.active`/per-chip `DropArea` live-swap + a `Flow.move` `Transition` (positioners support `move`, the Flow analogue of `moveDisplaced`). ✅ Chosen. We get the same drag feel and reuse the components, without disturbing the shared list or losing wrapping.

On drop, the live `DelegateModel` index is emitted to the existing reorder path (the `moveLeft`/`moveRight` plumbing collapses into a single `reorderItem(zone, from, to)` call). Selection state is kept for showing remove + options; the arrows are deleted.

Gesture model: **press-drag to reorder, tap to select, long-press opens options.** Use a small drag threshold so a quick tap selects and a sustained move drags; wire tap/long-press through `AccessibleTapHandler` so the behavior and the a11y actions share one code path.

### 3. In-app options affordance + indicator

When a chip's type `typeHasOptions(type)`, render the `settings.svg` icon on the chip as a persistent indicator (always visible, even unselected) per the new spec, implemented as a `StyledIconButton` so it is both the indicator **and** the explicit open-options affordance. Activating it calls `editCustomRequested(id, zone)` → existing `openCustomEditor` routing. Long-press remains as a shortcut. `StyledIconButton`/`AccessibleTapHandler` already provide `Accessible.role`/`name`, so no raw-glyph or bare-`MouseArea` controls are introduced.

### 4. Web drag-and-drop + indicator

Replace the arrow `<span>`s with draggable chips. Use the browser's native drag-and-drop (`draggable="true"`, `dragstart`/`dragover`/`drop`) computing the target index and calling the existing `reorder()` JS → `/api/layout/reorder`. Add a persistent options glyph/button on chips where the C++ side marks the type configurable (the generator already runs in C++, so it calls `typeHasOptions`). The glyph click keeps the existing `openEditor`/`openScreensaverEditor` behavior.

### 5. Accessibility fallback for reordering

Drag is not operable by assistive tech, so each chip keeps accessible "move toward start / move toward end" actions that call the same reorder path (this is essentially the old arrow logic surfaced only as `Accessible.onPressAction`, not as visible arrow buttons). This satisfies the reordering-fallback requirement without bringing the arrows back visually.

### 6. Guidance text

Update the `settings.layout.instructions` string (and the web editor's equivalent help text) to describe the new model: drag to reorder, tap the options icon (or long-press) to configure. New/changed translation keys follow the existing `Tr`/`TranslationManager` pattern. Pure content change, no structural risk.

### 7. Destructive-action confirmation

Reuse the existing confirmation-dialog pattern already in `LibraryPanel.qml` (`deleteConfirm` `Dialog` with Cancel/Delete `AccessibleButton`s) rather than inventing a new one:

- **Reset to Default** — wrap the `Settings.network.resetLayoutToDefault()` call (currently fired directly in `SettingsLayoutTab.qml` `onClicked`) behind a confirm dialog.
- **Remove configured widget** — `FavoritesListView` already models this with `removeConfirmFn`; the editor's remove path gains the same gate, but **only** when the widget is configured (its type `typeHasOptions`, or it carries non-default item properties). Unconfigured widgets stay one-tap to avoid friction. The "is configured" test reuses the single-source-of-truth helper plus a getItemProperties check.

No undo stack is introduced (heavier than warranted); confirmation is the minimal root-cause guard.

### 8. Widget picker: categories, sorting, filter

The picker model is currently an inline array ordered loosely by color group. Restructure to explicit categories with headers and deterministic within-category sorting (alphabetical by display name), and add a filter `StyledTextField` that narrows by name. Options considered:

- Keep the flat `ListView`, inject section-header delegates and sort the array — simplest, works with the existing `Dialog`+`ListView`. ✅ Chosen.
- Switch to a `section`-grouped `ListView` (`section.property`) — clean but requires a flat pre-sorted role-bearing model; equivalent effort, marginal benefit.

The same category/sort definition should be shared with the web picker so both editors match (the category mapping can live alongside the `typeHasOptions` helper as editor metadata, keeping one C++ source for "widget catalog" facts).

### 9. Single active options editor

`SettingsLayoutTab.qml` owns all the options popups (`customEditorPopup`, `screensaverEditorPopup`, `scaleWeightEditorPopup`, `displayModeEditorPopup`, `sleepEditorPopup`, plus `zoneOptionsPopup`). `openCustomEditor()` is the single routing point, so it closes any currently-open options popup before opening the target one — a few lines at one chokepoint, no new state machine.

### 10. Accessible / consistent controls (touched files)

Per CLAUDE.md ("fix pre-existing violations in any file you touch"), the controls in `LayoutEditorZone.qml` (zone `▲`/`▼` move, `−`/`+` scale, add `+`, zone-options) and the `▣` grid toggle in `LibraryPanel.qml` move from raw `Rectangle`+`Text(glyph)`+`MouseArea` to `AccessibleButton`/`StyledIconButton` with SVG icons. Icon inventory (`resources/icons/`): `plus.svg` and `minus.svg` exist → reuse for scale `+`/`−` and the add button; `ArrowLeft.svg` exists → reuse rotated 90°/270° for zone up/down (avoids new assets), or add `ArrowUp.svg`/`ArrowDown.svg` if rotation looks off; **no grid icon exists** → add a `grid.svg` for the Library view-mode toggle (or swap that toggle to two existing list/grid glyphs). Scope is bounded to the files this change already edits; we do not sweep the whole settings page.

## Risks / Trade-offs

- **[Gesture conflict in-app: tap-select vs. drag vs. long-press-options]** → Use a drag threshold so a quick press selects and a deliberate move drags; keep long-press for options and add the visible options icon so users aren't forced onto the ambiguous gesture. Verify on a real tablet (touch), not just desktop.
- **[`Flow` layout + drag reorder math]** → Wrapping flows make "insert index from drop point" non-trivial. Mitigate with per-chip `DropArea`s (each chip reports its own index) rather than computing from coordinates.
- **[Web native DnD on touch devices]** → HTML5 drag-and-drop is unreliable on mobile browsers. If the web editor is used from a phone/tablet, fall back to a pointer-events based drag (pointerdown/move/up) instead of native DnD. Decide during implementation based on where the web editor is actually used; keep the accessible move actions regardless.
- **[Flow `move` transition + DelegateModel live-swap]** → The drag mechanics are proven on `ListView` (`moveDisplaced`); on a `Flow` the equivalent is the positioner `move` `Transition`. Confirm displaced chips animate correctly when a swap crosses a wrap boundary (row to row), and roll back live swaps on drag-cancel exactly as `FavoritesListView.onCanceled` does.
- **[Reusing `FavoritesListView` internals without the component itself]** → We borrow the pattern, not the file (orientation differs). Keep the borrowed logic faithful (start-index capture, emit-before-clearing-drag-flag, cancel rollback) so we don't reintroduce bugs that file already fixed.
- **[Three call sites must all switch to the helper]** → If one is missed, indicator and behavior diverge again. The spec's "indicator and open behavior agree" scenario is the test for this.

## Migration Plan

No data migration. Layout JSON, `reorderItem`, and item-property APIs are unchanged, so existing user layouts load and behave identically. The change is purely editor-interaction; rollback is reverting the editor code with no stored-data implications.

## Open Questions

- Is the web layout editor used from touch devices often enough to require the pointer-events DnD fallback in v1, or is native HTML5 DnD acceptable initially?
- Should the chip drag-reorder be extracted into a small reusable `ReorderableChipFlow.qml` (sibling to `FavoritesListView`), or kept inline in `LayoutEditorZone.qml`? There is currently only one consumer, so inline is the leaner default; extract only if a second horizontal-reorder surface appears.

Resolved during design:
- Options/gear icon — use the existing `resources/icons/settings.svg`; no new asset needed.
- Gesture model — tap selects, long-press opens options, and a persistent `settings.svg` `StyledIconButton` is the explicit open affordance (so users never depend on the hidden gesture).
