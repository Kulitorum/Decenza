## Context

Two transient panels on `qml/pages/IdlePage.qml` open over other content instead of making room for it.

- **Inline carousel.** The idle center content is a `ColumnLayout` anchored by `anchors.verticalCenter`, unbounded, no explicit height (`IdlePage.qml:746-753`). It holds the status readouts, the `centerTop` tiles, and — as a sibling `Item` (`IdlePage.qml:779-`) — the carousel, whose height is `Layout.preferredHeight: activePresetFunction !== "" ? activePresetRow.implicitHeight : 0` (animated 200 ms, `clip: true`). The lower-mid band is a **separate `Rectangle` anchored to `bottomBar.top`** (`IdlePage.qml:1364-1388`). Centered-and-unbounded vs. bottom-anchored means no layout relationship: expanding the carousel grows the column about its centre and its lower edge crosses the band. The `lowerMidBarFits` guard (`IdlePage.qml:1360-1362`) is computed from `idlePage.height` alone and never reacts to `activePresetFunction`.
- **Bottom-nav pickers.** A floating `Dialog` (`EquipmentItem.qml:163-266`) positioned by a `y` derived only from its own button geometry (`EquipmentItem.qml:200-211`) — above-button returns `-height - Theme.spacingSmall`, clearing the *button* but nothing else, so its opaque top edge clips the Shot Plan sentence. The identical block is duplicated in `EspressoItem`, `SteamItem`, `FlushItem`, `RecipesItem`, `BeansItem`, `HotWaterItem`.

**The constraint that kills the obvious fixes:** the zone system lets a user pack the idle screen with widgets. On a fully populated layout there is **no free space** to relocate a panel into and no slack to shrink into. Any approach built on finding or reserving clearance works only on the roomy default layouts and fails precisely where it is needed.

## Goals / Non-Goals

**Goals:**
- A panel opens at its anchor, full size, on any layout — including a fully populated one.
- Nothing moves when the space the panel needs is already free.
- When that space is occupied, the underlying content slides by exactly the amount required, and restores precisely on close.
- Correct assistive-technology behavior via focus management, without changing appearance.
- One shared rule for all seven bottom-nav pickers instead of seven copies.

**Non-Goals:**
- **No scrim, dim, backdrop, or dialog chrome** — the current floating-card and inline-carousel looks are preserved exactly. Room is made by sliding, never by visually layering over content.
- No relocating a panel away from its anchor, and no shrinking it to fit.
- No redesign of the panels, the tiles, or `PresetPillRow` internals.
- Not `fix-brew-bar-widget-polish` item 4 (intra-zone stacking *inside* the lower-mid bar).
- No new Settings, no C++/BLE changes, no migration.

## Decisions

### Decision 1 — The opening widget stays anchored; everything else yields, directionally by zone
The widget/panel that opens does **not** move — the picker stays at its button, the carousel stays under its tile. All **other** idle content slides out of the way, in the direction the widget grows, determined by the widget's zone:
- widget in a **top** zone → push everything **below** it down;
- widget in a **center** zone → push everything **below** it down;
- widget in a **bottom** zone → push everything **above** it up.

The push is a **transient offset** applied to the yielding content, sized to exactly free the region the panel needs, animated, and removed on close. When the needed region is already free, nothing moves. This single rule covers any widget in any zone, not just the two current bugs (bottom-nav pickers push the content above up; the inline carousel pushes the content below down).

- **Chosen over "relocate the panel to free space"**: on a packed layout there is no free space, and relocating orphans the panel from the control that opened it — and the user's rule is explicitly that the opened thing does not move.
- **Chosen over "shrink/paginate the panel to fit"**: degrades the panel exactly when the screen is busiest.
- **Chosen over "raise the `lowerMidBarFits` threshold"**: only hides the brew band more aggressively (losing the readout) rather than preventing overlap.

### Decision 2 — Slide the content, not the configuration
The offset is a transient view translation applied to the idle content, reverted in full on close. Per-zone `offsets`/`scales`/`alignment` (`IdlePage.qml:98-118`) remain authoritative for where a zone sits; the slide is layered on top and never written back. This is what keeps the change complementary to the existing zone options rather than competing with them: static placement stays the user's, the transient state is the app's.

Implementation must decide *what* translates — the whole idle content region, or only the content between the panel and the screen edge. Prefer the smallest subtree that resolves the collision, so unrelated widgets stay still. The offset must be event/binding-driven (reacting to open/close and geometry), never timer-driven, per project convention.

### Decision 3 — Focus behaves like a dialog; appearance does not
The pickers are already `modal: true`. Add the missing dialog *semantics* — focus moves into the panel on open, returns to the invoking control on close, Esc/back and tap-outside dismiss — so a screen reader stays inside the open panel and never walks the offset background. Explicitly **do not** enable `dim`, add a backdrop, or introduce dialog chrome; this requirement is about focus only.

### Decision 4 — One shared open/slide/restore rule
Factor the duplicated float-above positioning out of the seven `*Item.qml` files into a single shared rule that owns: anchor position, the free-space test, the transient offset, and the restore. All pickers consume it, so behavior can't drift between them (the same consolidation lesson as the graph legend).

## Risks / Trade-offs

- **Motion feels unstable on a touch tablet** → the slide only runs when the space is genuinely occupied, so roomy layouts (the common case) never move; keep the existing 200 ms easing so it reads as deliberate.
- **Content slides off the top on a very full screen** → the offset must be bounded so essential content isn't pushed out of view; if the required offset cannot be satisfied, prefer sliding the maximum useful amount over pushing content off-screen, and verify the panel remains fully usable.
- **Restore drift** (content doesn't return exactly) → make the offset a single reversible property bound to panel state, not a sequence of imperative nudges, so close is exact by construction.
- **Binding loop between the offset and the geometry that triggers it** → the free-space test must read *unoffset* geometry; keep the dependency one-way (panel state → offset), never offset → test → offset.
- **Shared rule regresses a sibling picker** → all seven share the same block today, so factoring and fixing once should be behavior-preserving apart from the intended change; check each picker individually.

## Migration Plan

Pure UI behavior; no data or persisted state (the offset is never saved). Land the shared picker rule and the carousel case together. Roll back by reverting `IdlePage.qml` and the picker items. Verify on both a **roomy** layout (expect zero motion) and a **deliberately over-populated** layout (expect the slide, and exact restore).

## Open Questions

- What is the smallest subtree that should carry the transient offset — the whole idle content area, or just the band between the panel and the opposite edge? (Resolve in implementation; prefer the smallest that resolves the collision.)
- On an extremely full layout, is sliding preferable to letting the panel overlap a low-value region? Current answer: always slide, bounded so nothing essential leaves the screen — revisit if a real layout makes the bound impossible.
