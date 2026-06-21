## 1. Single source of truth for configurable types

- [x] 1.1 Add `Q_INVOKABLE bool typeHasOptions(const QString& type) const` to `SettingsNetwork` (`src/core/settings_network.h` / `.cpp`), backed by a single static set of configurable widget types (`custom`, `scaleWeight`, `shotPlan`, `sleep`, `machineStatus`, `temperature`, `steamTemperature`, screensaver types, `lastShot`).
- [x] 1.2 Replace the hardcoded type list in `LayoutEditorZone.qml` `onPressAndHold` with a call to `Settings.network.typeHasOptions(modelData.type)`.
- [x] 1.3 Replace the hardcoded list driving routing in `SettingsLayoutTab.qml` `openCustomEditor()` so the gate uses `typeHasOptions` (keep the per-type popup routing).
- [x] 1.4 Web editor consumes the same definition via a JS `typeHasOptions()` mirror in `shotserver_layout.cpp` (client-side; documented "keep in sync" with the C++/QML source).

## 2. In-app: adopt reusable components + has-options affordance

- [x] 2.1 Replace the chip's raw `Text`+`MouseArea` controls with project components: `StyledIconButton` for remove (`cross.svg`) and for the options button (`settings.svg`).
- [x] 2.2 Render the `settings.svg` `StyledIconButton` on every chip where `Settings.network.typeHasOptions(modelData.type)` is true, visible whether or not the chip is selected (serves as both the has-options indicator and the open affordance). Configurable chips also get a coloured border.
- [x] 2.3 Wire the options button's `onClicked` and the chip's long-press to `editCustomRequested(modelData.id, root.zoneName)` — one shared path.
- [x] 2.4 `StyledIconButton` supplies accessible role/name/focus/press; added translated accessible names for the options button.

## 3. In-app: drag-and-drop reordering (FavoritesListView pattern on the Flow)

- [x] 3.1 Remove the `◀`/`▶` move-left/move-right arrow blocks from the chip in `LayoutEditorZone.qml`.
- [x] 3.2 Wrap the zone's items in a `DelegateModel` and drive the existing wrapping `Flow` from it; add a `Flow.move` `Transition` so displaced chips animate.
- [x] 3.3 Port the `FavoritesListView` drag mechanics to the chip: `Drag.active` on the chip, a per-chip `DropArea` performing live `visualModel.items.move(from, to)`, a small drag threshold so a quick tap still selects, capture of `_startIndex` on press, emit-once on release, `preventStealing`.
- [x] 3.4 On release, call the existing reorder path `reorder(from, to)` → `reorderItem(zone, from, to)`; on cancel, roll back live swaps as `FavoritesListView.onCanceled` does.
- [x] 3.5 Add accessible "move toward start" / "move toward end" buttons on each chip (gated on `AccessibilityManager.enabled`) as the assistive-tech fallback, hidden at the ends — drag has no TalkBack/VoiceOver equivalent.
- [ ] 3.6 Verify tap-select, long-press-options, options-button, and drag-reorder do not conflict, including a swap that crosses a wrap boundary; test on a touch device per CLAUDE.md. *(needs runtime)*

## 4. Web editor: indicator + drag-and-drop

- [x] 4.1 In `shotserver_layout.cpp`, add a persistent gear indicator on chips where `typeHasOptions(type)` is true (clicking the chip keeps the existing open/select behavior).
- [x] 4.2 Make chips draggable (`draggable="true"` + `dragstart`/`dragend`/`dragover`/`drop`) and compute the drop target index.
- [x] 4.3 On drop, call the existing `reorder(zone, from, to)` JS → `/api/layout/reorder`; removed the arrow `<span>` generation.
- [ ] 4.4 Non-drag/keyboard reorder fallback for the web editor (touch + keyboard). *(deferred — see open question in design.md; HTML5 DnD is the v1 mechanism)*

## 5. Editor guidance text

- [x] 5.1 Update the `settings.layout.instructions` string in `SettingsLayoutTab.qml` to describe drag-to-reorder and the visible options affordance; removed the "select + arrows / long-press Custom" wording.
- [x] 5.2 Added equivalent help text in the web editor (`shotserver_layout.cpp`).

## 6. Destructive-action confirmation

- [x] 6.1 Wrap "Reset to Default" in `SettingsLayoutTab.qml` behind a confirmation `Dialog`; only reset on confirm.
- [x] 6.2 Gate widget removal so a *configured* widget (`itemIsConfigured`) prompts for confirmation; unconfigured widgets stay one-tap.
- [x] 6.3 Web editor reset already confirms via `confirm("Reset layout to default?")` (verified; unchanged).

## 7. Widget picker: categories, sorting, filter

- [x] 7.1 Restructure the add-widget picker model in `LayoutEditorZone.qml` into explicit categories (Actions, Readouts, Utility, Screensavers) with visible section headers (`ListView.section`).
- [x] 7.2 Sort widgets alphabetically by display name within each category.
- [x] 7.3 Add a `StyledTextField` filter that narrows the list by name; clearing it (on open) restores the full categorized list.
- [x] 7.4 Apply the same categories + within-category sort + filter to the web editor picker; shared catalog shape documented to stay in sync.

## 8. Accessible / consistent controls (touched files)

- [x] 8.1 Replace the zone move (`▲`/`▼`) and scale (`−`/`+`) controls in `LayoutEditorZone.qml` with `StyledIconButton`s using SVG icons (`minus.svg`/`plus.svg`; `ArrowLeft.svg` rotated 90/270 for move).
- [x] 8.2 Replace the add-widget `+` and zone-options controls with `StyledIconButton` + SVG icons.
- [x] 8.3 Replace the `▣` Unicode grid-toggle glyph in `LibraryPanel.qml` with a new `grid.svg` (registered in `resources.qrc`) on `StyledIconButton`; list toggle too.
- [x] 8.4 Converted controls expose accessible role/name/focus/press via `StyledIconButton`.

## 9. Single active options editor

- [x] 9.1 In `SettingsLayoutTab.qml`, `openCustomEditor()` closes any open options popup (`closeOptionEditors()`) before opening the target so only one is active.

## 11. Live home-screen preview (in-app editor only)

- [x] 11.1 Add `qml/components/layout/LayoutPreview.qml` — a scaled 800×480 canvas that reuses the real `LayoutBarZone`/`LayoutCenterZone`/`LayoutItemDelegate` to render all zones (statusBar, top, center, lowerMidBar, bottom) from the live `Settings.network` layout config; re-renders reactively via a `layoutConfiguration` dependency. Applies per-zone scale and zone style/distribution/alignment.
- [x] 11.2 Register `LayoutPreview.qml` in `CMakeLists.txt`.
- [x] 11.3 Embed the preview at the top of the editor's left column in `SettingsLayoutTab.qml` (bordered card, capped height, "Preview" caption).
- [ ] 11.4 Verify the preview matches the home screen and updates live as widgets are added/removed/reordered/configured; check heavy widgets (screensavers) don't bog down the editor. *(needs runtime)*

## 12. Chip remove affordance: no reflow jump + discoverable

- [x] 12.1 Make the chip remove (`×`) control always occupy its slot (opacity-toggled, not visibility-toggled) so selecting a chip no longer resizes it / makes the row jump.
- [x] 12.2 Reveal the remove control on hover (desktop, via `HoverHandler`) and on selection (touch), faint otherwise — so it's discoverable without a click.

## 10. Verification

- [x] 10.1 Built via Qt Creator (Qt 6.11.1 macOS): C++ (`settings_network.cpp`, `shotserver_layout.cpp`) recompiled and all three QML files passed qmlcachegen — 0 errors, 0 warnings.
- [ ] 10.2 Manually verify in-app: indicator shows only on configurable widgets; tapping it opens the correct popup; drag reorders and persists across restart; arrows are gone; a11y move actions work. *(needs runtime)*
- [ ] 10.3 Manually verify web editor: indicator shows on configurable widgets; drag reorders and persists; arrows are gone; parity with in-app. *(needs runtime)*
- [ ] 10.4 Confirm existing layouts load unchanged and per-widget options still persist per instance. *(needs runtime)*
- [ ] 10.5 Verify guidance text matches actual interactions in both editors. *(needs runtime)*
- [ ] 10.6 Verify "Reset to Default" and configured-widget removal both prompt for confirmation and that Cancel leaves the layout intact. *(needs runtime)*
- [ ] 10.7 Verify the widget picker shows category headers, sorts within categories, and filters by typing; opening a second widget's options closes the first. *(needs runtime)*
- [ ] 10.8 With a screen reader, verify every converted editor control announces a role/name and activates; confirm no Unicode-glyph icons remain in the touched files. *(needs runtime)*
