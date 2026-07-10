# Web Layout Editor Usability & QML Parity

## Why

The web layout editor (`/layout`) is materially harder to use than the in-app editor and should feel the same. Live testing found: the instruction paragraph flex-grows to fill half the page (crushing the zone cards into a narrow column, and at ≤1200px the library panel fully covers them); the gear icon looks like a button but is decorative, while chip clicks toggle select/deselect so every other click on a configurable widget (e.g. Shot Plan) is a dead click — making per-item settings feel absent even though the editors exist; readout options render as unlabeled micro-dropdowns inside the chip instead of the app's labeled options dialog; and there is no home-screen preview, so users edit blind. Several behaviors also violate already-ratified requirements in `layout-editor-usability` (remove-confirmation for configured widgets, stable chip size, hover-revealed remove control).

## What Changes

All changes are to the web editor (`src/network/shotserver_layout.cpp`) unless noted; the in-app editor is the reference behavior.

- **Fix the page layout**: the instruction text no longer consumes the content area; the zones column, preview, and library panel are all visible and usable at common desktop/tablet widths, with no panel overlap.
- **Make the gear a real button**: clicking a chip's gear always opens that widget's options editor (no select-toggle fall-through). Chip click still selects/deselects; deselecting or removing a chip closes its editor. Instruction text updated to match ("tap the gear").
- **Labeled options editor for readouts**: replace the inline unlabeled `<select>`s on selected chips with a popup that mirrors the in-app `ReadoutOptionsPopup` — section headers ("Scale data mode", "Display", "Color"), the same descriptive choice labels ("Net beans (minus dose tare)", …), and the ratio-toggle hint. Sections still derive from the shared capability schema.
- **Live home-screen preview on the web**: an 8:5 preview pane rendered from the same `/api/layout` data (zones, order, distribution/alignment/style, offsets, scales, widget labels/colors), updating after every edit — the web analog of `LayoutPreview.qml`. This removes the web exemption in the existing preview requirement.
- **Remove-confirmation for configured widgets** on the web (already required by spec; currently violated): removing a widget with options or non-default settings asks first; bare widgets remove directly.
- **Stable, discoverable chips** (already required by spec; currently violated): remove control revealed on hover, selection does not resize the chip or reflow the zone.
- **Label the zone position/scale controls**: the ▲/▼ offset and −/+ scale controls get visible labels or tooltips in the web editor (and accessible names in-app already exist).
- **Settings-access guard for web edits**: layout mutations made via the web API cannot leave the home screen with no path to Settings — the same guarantee the in-app editor enforces via `ensureSettingsAccessible()`, which web edits currently bypass.

## Capabilities

### New Capabilities

_None — all changes strengthen existing layout-editor capabilities._

### Modified Capabilities

- `layout-editor-usability`:
  - New requirement: the web editor page presents its zones, preview, and library usably at common viewport widths (no dead half-page, no panel overlap).
  - Preview requirement: remove the web exemption — the web editor SHALL also show a live home-screen preview driven by the current layout configuration.
  - Guidance requirement: web instruction text describes the gear as the options affordance and matches actual behavior.
  - New requirement: zone offset/scale controls carry visible labels or tooltips identifying their function.
  - New requirement: layout mutations from any editing surface (in-app or web) preserve at least one path to Settings on the home screen.
- `layout-widget-instance-config`:
  - Strengthen the web affordance requirement: the has-options indicator SHALL itself be an activatable control that opens the instance editor; selection toggling SHALL NOT swallow the open action; the editor SHALL close when its item is deselected or removed.
  - New requirement: the web readout options editor presents the same labeled sections and descriptive choice labels as the in-app `ReadoutOptionsPopup`, derived from the shared capability schema.

### Spec-compliance fixes (no delta needed)

- `layout-editor-usability` / "Confirmation for destructive layout actions": web remove of a configured widget currently skips confirmation.
- `layout-editor-usability` / "Stable, discoverable chip remove control": web chips resize on selection and hide the remove control until clicked.

## Impact

- `src/network/shotserver_layout.cpp` — nearly all work: CSS grid/flex fix, gear button + editor lifecycle, readout options popup, preview pane (HTML/JS render of zones), remove confirmation, hover-remove/stable chips, control tooltips. The file is large (~4.5k lines of embedded HTML/JS); expect sizeable but localized diffs.
- `src/core/settings_network.{h,cpp}` — possible new helper(s): expose `itemIsConfigured` to the web API for the remove-confirmation gate, and a settings-access guard usable from the layout mutation endpoints (the QML-side `ensureSettingsAccessible()` logic moves to or is duplicated in C++).
- `qml/pages/settings/SettingsLayoutTab.qml` — only if the settings-access guard is centralized in C++ (QML then calls the shared helper).
- Descriptive option labels currently live in `ReadoutOptionsPopup.qml`; the web mirrors them in its embedded JS (documented mirror, same pattern as the existing choice lists).
- No BLE, database, or machine-control surfaces touched. No new endpoints expected beyond possibly a `configured` flag in existing layout item JSON.
