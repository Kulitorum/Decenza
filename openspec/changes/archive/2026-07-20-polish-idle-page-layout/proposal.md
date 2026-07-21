## Why

Two transient panels on the Idle page open **on top of** other page content instead of making room for it, so text renders superimposed and unreadable:

1. **Inline quick-pick carousel.** Tapping a category tile (Recipes / Beans / Steam / Hot Water) expands an inline chip carousel. The idle center content is a **vertically-centered, unbounded `ColumnLayout`** (`IdlePage.qml:746-753`) while the lower content — the Shot Plan sentence and the lower-mid brew-detail band — is **bottom-anchored** (`IdlePage.qml:1364-1388`). The two are positioned by independent schemes with no layout relationship, so when the carousel expands the centered column grows about its center and its lower edge slides over the bottom-anchored band. The one guard (`lowerMidBarFits`, `IdlePage.qml:1360-1362`) is a coarse viewport-height heuristic that never reacts to the carousel opening; its own comment concedes it is "not a hard guarantee."

2. **Bottom-nav quick-picker popups.** Tapping Equipment (or Profiles / Flush / Steam / Hot Water / Recipes / Beans) opens a floating `Dialog` that clears only `Theme.spacingSmall` from its button (`EquipmentItem.qml:200-211`), so its opaque top edge lands on and clips the Shot Plan sentence above it. The same float-above block is duplicated across seven `*Item.qml` files.

The naive fixes do not survive a **fully populated layout**. The zone system lets a user pack the idle screen with widgets; on such a screen there is no free space to relocate a panel into, and shrinking the panel to fit degrades it to uselessness. Any fix that assumes slack works only on the roomy default layouts and fails exactly where it is needed most.

## What Changes

A single rule governs both panels: **a panel opens where it belongs; the content yields only when it must.**

- A transient panel SHALL open at its natural position — the bottom-nav picker anchored to its button, the inline carousel beneath its tile. The panel SHALL NOT be relocated away from its anchor to hunt for free space (which would orphan it from the control that opened it).
- If the space the panel needs is unoccupied, it SHALL simply open and **nothing else moves**.
- If that space is occupied, the underlying idle content SHALL **slide just enough to free it**, animated, and SHALL restore to its exact prior position when the panel closes. The slide is a transient view offset only — it never persists into the layout configuration.
- The modal pickers SHALL adopt dialog-like **focus** behavior only: focus moves into the panel on open and returns to the invoking control on close, so the content motion beneath is never traversed by a screen reader.
- **The panels' current appearance is preserved.** No scrim/dim, no backdrop, no dialog chrome (header/footer/frame) is added — the existing lightweight floating-card and inline-carousel looks stay exactly as they are today. Making room is achieved by the transient slide, never by visually layering over content.
- No change to *which* widgets, tiles, or chips appear, their order, styling, or the `PresetPillRow` contents.
- **Complements the existing per-zone layout options.** Each idle zone already carries user-configurable position (`offsets.<zone>`), `scales.<zone>`, and `alignment`/`distribution`/`style`/`itemSize`; those remain authoritative for where a zone sits. The transient slide is a temporary visual offset applied on top and fully reverted — it never rewrites, re-centers, or re-scales a zone the user positioned.

## Capabilities

### New Capabilities
- `idle-page-panel-clearance`: transient idle-page panels (the inline category quick-pick carousel and the bottom-nav quick-picker popups) open at their anchor and make room by transiently sliding the underlying content only when the space they need is occupied, restoring it on close — correct on any home-layout composition, including a fully populated screen.

### Modified Capabilities
<!-- none — new layout-invariant behavior. Does NOT touch fix-brew-bar-widget-polish item 4 (intra-zone widget stacking *inside* the lower-mid bar). -->

## Impact

- **Primary file**: `qml/pages/IdlePage.qml` — the transient content offset, the space test, and the inline carousel's relationship to the bottom-anchored zones.
- **Bottom-nav pickers**: `qml/components/layout/items/EquipmentItem.qml` plus the duplicated float-above positioning in `EspressoItem.qml`, `SteamItem.qml`, `FlushItem.qml`, `RecipesItem.qml`, `BeansItem.qml`, `HotWaterItem.qml` — factored so the open/slide/restore rule lives in one place.
- **Unaffected**: `qml/components/PresetPillRow.qml` (the chip component is fine — the defect is in the containers/positioning), the widget catalog, Settings, and `fix-brew-bar-widget-polish`.
- No new Settings, no C++/BLE changes, no migration.
