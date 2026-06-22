## Context

The composable bars ([#1368](https://github.com/Kulitorum/Decenza/issues/1368) / [#1369](https://github.com/Kulitorum/Decenza/issues/1369)) introduced a family of small readout widgets — `doseWeight`, `milkWeight`, `ratioQuickSelect`, `scaleWeight` — rendered into configurable zones (`lowerMidBar`, status bar, bottom bars) by `LayoutBarZone` → `LayoutItemDelegate` → the per-type `*Item.qml`. Per-instance options (`dataMode`, `displayMode`, `color`) are stored via `Settings.network.setItemProperty` and edited in `ScaleWeightEditorPopup.qml` (in-app) and `shotserver_layout.cpp` (web). Field testing ([#1379](https://github.com/Kulitorum/Decenza/issues/1379)) found four polish issues, all confined to these widgets. This change fixes them without touching the zone/storage architecture.

Key existing wiring relevant here:
- `MilkWeightItem.qml` binds only to `Settings.brew.lastSteamMilkG` (last *committed* session). The live in-session value already exists as `sessionMeasuredMilkG` on the app root (`main.qml`), written from `IdlePage`/`SteamPage` during steaming and committed to `lastSteamMilkG` at session end.
- `ScaleWeightItem.weightText()` appends `" 1:" + ProfileManager.brewByRatio` whenever `ProfileManager.brewByRatioActive`, unconditionally.
- The seeded status-bar preset `csb_scale` already uses `displayMode: "icon"`; the seeded lower-mid-bar preset `lmb_scale` uses `dataMode: "contextAware"` but leaves `displayMode` unset (→ dot + number, no label).

## Goals / Non-Goals

**Goals:**
- Milk widget gives live feedback during a steam, falling back to the committed value when idle.
- Scale widget's `1:X.X` suffix is suppressible per instance, defaulting to today's behaviour.
- A compact `scaleWeight` is identifiable as a scale reading without its full-mode label.
- Lower-mid bar widgets never overlap (item 4 — lands after a repro is available).

**Non-Goals:**
- No new `Settings` properties or domain objects (per-instance options use the existing item-property mechanism).
- No change to the zone model, storage format, or migration path.
- No behaviour change to existing layouts except where a user opts into `showRatio`.
- Not redesigning the readout widgets or the ratio pill.
- The lower-mid bar overlap (#1379 item 4) stays in this change but lands after items 1–3, once a concrete repro is available (see Decision 4).

## Decisions

### 1. Milk widget reads the live session value with a committed-value fallback

`MilkWeightItem` will source its value from the app-root `sessionMeasuredMilkG` when greater than zero, otherwise `Settings.brew.lastSteamMilkG` — exactly the precedence the issue suggests and the same source `SteamPage` already uses (`Window.window.sessionMeasuredMilkG`). Access is via `root.Window.window.sessionMeasuredMilkG` (the widget is not always a descendant of a page), guarded for the case where `Window.window` or the property is absent so the widget degrades to the committed value and never errors.

- **Alternative — add a new `liveMilkG` settings property:** rejected. The live value already exists on the window root; duplicating it into `Settings` adds state to keep in sync for no benefit, and violates "prefer fewer settings / smarter defaults."

### 2. `showRatio` is a per-instance boolean, default-on

Add a `showRatio` property to `scaleWeight` read from `modelData` (defaulting to `true` to preserve current behaviour) and gate the `" 1:" + ratio` suffix in `weightText()` on it. Surface a toggle in `ScaleWeightEditorPopup.qml` and the web editor widget list. This matches the established `dataMode`/`displayMode` per-instance pattern exactly.

- **Alternative — auto-suppress when a `ratioQuickSelect`/status-bar ratio is present in the same layout:** rejected. Cross-widget awareness is fragile (which zones count? what about the status bar on another page?) and surprising; an explicit per-instance toggle is predictable and consistent with the other scale options.
- **Alternative — global setting:** rejected per "settings go in their place, prefer fewer" — and a per-instance toggle is strictly more flexible (one scale can show the ratio while another hides it).

### 3. Compact `scaleWeight` self-identifies via the existing `icon` display mode

Rather than introduce a new label setting, make the compact scale recognizable through the `icon` `displayMode` (scale icon ahead of the value), which the widget already supports. The fix is to seed the self-identifying presentation where the layout system places a compact scale by default — i.e. the `lmb_scale` preset gains `displayMode: "icon"`, matching the status bar's `csb_scale`. Existing user layouts keep their stored `displayMode` untouched.

- **Alternative — add a short always-on text label to compact mode:** rejected as the default. It adds horizontal width pressure in already-tight bars and duplicates the icon's job; the issue offers it only as an "or." The icon default is lighter and consistent with sibling widgets (`temperature`, `machineStatus`) that use icon mode in compact zones.
- **Alternative — flip the global compact default from `text` to `icon`:** rejected. That would change every existing compact `scaleWeight` instance, breaking "default preserves current behaviour"; seeding the preset only affects newly-created/default layouts.

### 4. Lower-mid bar overlap — reproduce from the screenshots, then fix the layout, not with a timer/guard

The reported overlap (size controls + "Ratio" label + ratio pill stacking) must be reproduced from the issue's screenshots before committing to a fix; it lands after items 1–3 within this change. Candidate mechanisms, in order of suspicion:
- **`LayoutBarZone` scale + anchored row interaction:** `itemsRow` is anchored `left`/`right` *and* carries a `scale` transform with an alignment-pinned `transformOrigin`. A non-default `zoneScale` scales content around an edge while the row stays anchored full-width, which can push scaled children over neighbours. Likely fix: drive width from content (`implicitWidth`) under scale rather than anchoring full-width while scaling, or apply scale to a properly-sized inner container.
- **`RatioQuickSelectItem` internal sizing:** its `ColumnLayout` uses `anchors.centerIn: parent; width: parent.width` while reporting `implicitWidth: col.implicitWidth`; if the delegate allocates less than implicit width, the centered pill can overflow its cell.
- **Vertical collision (most likely):** the reported "size buttons" don't map to any lower-mid bar widget in the palette, which hints at a collision between the lower-mid bar and the in-page preset/size row — driven by `anchors.bottomMargin: -lowerMidBarYOffset` and the `lowerMidBarFits` clearance — rather than intra-zone overlap.

The fix will be event-/layout-driven (correct sizing and transform origin), never a timer or post-hoc nudge, per project conventions. The screenshots decide which mechanism applies — this is the open question below.

## Risks / Trade-offs

- **[#4 root cause unconfirmed without the screenshots]** → Treat reproduction as the first task; do not ship a speculative layout change. The spec states the invariant ("no overlap"); the design lists hypotheses rather than asserting one. (This repo has a history of mis-rooted layout/visual diagnoses — verify before fixing.)
- **[Live milk could flicker between live and committed values at session boundaries]** → Precedence is strict (live > 0 wins; else committed), and `sessionMeasuredMilkG` is reset to 0 on session end/ pitcher change in existing code, so the fallback is deterministic, not racy.
- **[Seeding `displayMode: "icon"` on `lmb_scale` only affects new/default layouts]** → Accepted and intended: existing layouts must not silently change. Users on older layouts can opt into icon mode via the instance editor.
- **[Web editor and in-app editor must agree on the new `showRatio` control]** → Add it to both in the same change (the "single source of truth for configurable widget types" requirement already governs this); tasks cover both sites.

## Open Questions

- Does issue #4 reproduce as intra-zone overlap (within `LayoutBarZone`) or as the lower-mid bar colliding with the in-page size/preset row? The screenshots from the issue author will decide which of the two fix paths in Decision 4 applies. Until then the spec's invariant holds regardless of mechanism.
