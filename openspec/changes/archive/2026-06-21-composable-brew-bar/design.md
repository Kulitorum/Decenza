## Context

The app's home/idle screen is composed from a **zone-driven layout system**: a JSON layout config (`Settings.network.layoutConfiguration`) describes zones (`statusBar`, `topLeft`, `topRight`, `centerStatus`, `centerTop`, `centerMiddle`, `bottomLeft`, `bottomRight`), each holding an ordered list of widget instances `{type, id, ...props}`. `IdlePage.qml` renders each zone via `LayoutCenterZone`/`LayoutItemDelegate`; the in-app editor (`SettingsLayoutTab.qml` + `LayoutEditorZone.qml`) and the web editor (`shotserver_layout.cpp`) let users add/remove/reorder widgets and edit per-instance properties.

PR #1364 introduced a "simplified home" that **bypasses** this system: a hardcoded `brewStatusBar` (Profile / Scale / Ratio / Beans / Milk) anchored above the bottom action bar, plus the `centerMiddle` zone re-pinned above it, all gated behind a new `simplifiedHome` theme flag. To get that look the PR also had to add two things the zone system can't express: an **accent background** (`color: Theme.primaryColor`, text in `Theme.primaryContrastColor`) and **equal-width cells** (`Layout.preferredWidth: 1`) — the latter via a hardcoded `distributeItems` branch in `LayoutCenterZone` gated to simplified mode.

Four gaps therefore block reproducing the bar in the editor:
1. **No zone** exists for a full-width band above the bottom action bar.
2. **Missing widgets**: no profile-name, dose, milk, or ratio-quick-select widgets.
3. The existing `scaleWeight` widget shows only **gross** weight.
4. **No per-zone presentation/layout options**: zones can't be told to distribute equally or carry a background.

Existing mechanisms we reuse:
- **Per-zone settings** already persist as zone-keyed maps in the layout JSON: `layout["offsets"][zone]` (`getZoneYOffset`/`setZoneYOffset`) and `layout["scales"][zone]` (`getZoneScale`/`setZoneScale`), with the web editor exposing offset controls via `/api/layout/zone-offset`. New zone options follow this exact pattern.
- **Per-instance widget properties** persist via `setItemProperty`/`getItemProperties` and the web `/api/layout/item` endpoints; per-instance editing already exists for `custom`/screensaver widgets (`openCustomEditor` in-app, `openEditor` on web).
- The `statusBar` zone was added after the others with a migration block — a working precedent for adding a zone.

## Goals / Non-Goals

**Goals:**
- Make the PR #1364 bar reproducible **pixel-for-pixel** through the in-app and web editors.
- Introduce a general-purpose, location-named zone (`lowerMidBar`) invisible and zero-cost when unused.
- Add **per-zone** layout + appearance options usable on **every** zone, opened by double-click/long-press in the editor.
- Keep zone appearance theme-driven (no hardcoded colors) so it tracks light/dark/custom palettes.
- Extend the existing `scaleWeight` widget with a per-instance data mode instead of a parallel widget.
- Keep default layouts byte-for-byte unchanged.

**Non-Goals:**
- Shipping a specific "simplified home" preset is out of scope (can follow as an optional default layout once the primitives exist).
- Redesigning the top `statusBar` or the bottom action bar.
- The `simplifiedHome` theme flag and hardcoded `brewStatusBar` — removed/superseded, not extended.
- Per-zone options that don't generalise sensibly (e.g. vertical distribution for the inherently-vertical center zones) — options apply where meaningful and no-op otherwise.

## Decisions

### D1: Add a general-purpose `lowerMidBar` zone, not a content-specific "brew bar"

The zone is named by **location** (lower-middle band, above the bottom action bar), so users may place any widgets there. Registration touch points (same set every zone uses): `defaultLayoutJson()` (empty `lowerMidBar` array + migration), `IdlePage.qml` (render it, anchored `anchors.bottom: bottomBar.top`), `SettingsLayoutTab.qml` zone array, `shotserver_layout.cpp` zone list (`{key:"lowerMidBar", label:"Lower Mid Bar", hasOffset:false}`).

**Alternative considered:** a content-named `brewBar`. Rejected — the band is just a location; naming it for brew content under-sells it and dates badly.

### D2: Empty-collapse + runtime height-gate instead of a mode flag

Zero height when empty (content-driven `implicitHeight`, `visible:false` at item count 0) → default users unaffected, no flag. When populated, a runtime gate hides it on short viewports: `visible = itemCount > 0 && availableCenterHeight >= barHeight + minCenterContent`. **Runtime-adaptive** (keys off measured height, not a device class), per the project rule against regressing capable hardware.

### D3: Per-zone options as a new `layout["zoneOptions"]` map, edited via a zone gesture

Add `getZoneOption(zone,key,default)` / `setZoneOption(zone,key,value)` in `SettingsNetwork` over a `layout["zoneOptions"][zone] = { distribution, alignment, background }` object — mirroring the existing `offsets`/`scales` maps. Options:
- **distribution**: `packed` (default = today), `equalWidth` (generalise the PR's `distributeItems`: every item gets `Layout.fillWidth` with equal share), `spaced` (justified).
- **alignment**: `left`/`center`/`right` for un-filled content.
- **background**: theme role `transparent` (default) / `surface` / `accent`.

Editor entry point: **double-click or long-press on the zone body** (not a widget) opens a zone-options panel. The in-app editor already distinguishes widget vs. empty-zone taps; the panel reuses the popup style of the existing custom-item editor. The web editor adds a zone-options control next to the existing offset control and a `/api/layout/zone-options` endpoint.

This is deliberately **general** — it lands on all zones at once, so equal-width cells, alignment, and backgrounds become reusable primitives rather than one-off code for the new bar.

**Alternative considered:** keep `distributeItems` special-cased to the bar. Rejected — that's the exact hardcoding we're removing; generalising it is cheaper than maintaining a second path and gives every zone the capability.

### D4: Named, theme-defined zone style presets (not raw colors)

Zone appearance is a **named style preset** defined in `Theme.qml`, not a raw color, satisfying the "always use Theme.qml, never hardcode colors" rule. `Theme.qml` gains style components bundling background + label/value text + emphasis:
- `standard` — transparent background, normal theme text (today's look, the default).
- `accentBar` — the PR #1364 look: accent fill (`Theme.primaryColor`), contrast text (`Theme.primaryContrastColor`), bold values.

A theme defines its own values for these presets, so they track light/dark/custom palettes; a contrast helper keeps text readable on any background. Modeling appearance as presets (rather than a low-level background role) is what the user asked for — it gives one selectable "look" per zone and a natural home in Theme for adding more.

**Alternative considered:** a low-level background-role option + separate text settings. Folded into presets instead — a single named style is simpler to pick and matches "configure the look" intent. **Alternative considered:** per-zone hex color picker. Rejected — breaks theming, fails on custom palettes, violates the Theme rule.

### D4a: One-tap "populate from preset" to drop in the PR view

The zone-options panel offers a "populate from preset" action that writes a built-in arrangement (widgets + matching zone options) into a zone in one step. The built-in **"Brew bar"** preset reproduces PR #1364: `profileName` · `scaleWeight`(contextAware) · `ratioQuickSelect` · `doseWeight` · `milkWeight`, with `equalWidth` distribution and the `accentBar` style. This is the user-facing "give me the PR's bar without hand-placing five widgets" path; the result is ordinary layout data, so every widget stays individually editable afterward. It also subsumes the earlier open question about shipping a "simplified home" — the preset *is* that, expressed as data through the editor rather than a code path.

**Alternative considered:** ship the PR bar as a fixed default layout. Rejected — an opt-in populate action keeps default layouts empty/unchanged while still giving one-tap setup.

### D5: New widgets are thin readouts; `ratioQuickSelect` is the only interactive one

`ProfileNameItem`/`DoseWeightItem`/`MilkWeightItem` mirror existing display items (label+value, theme-driven, StaticText, "—" placeholder). `RatioQuickSelectItem` is a pill bound to `Settings.brew.lastUsedRatio` that opens `RatioPresetDialog`; selecting a preset sets **only** `lastUsedRatio` (no `brewYieldOverride` side effect). Each registered in the four standard places. `RatioPresetDialog` and the three `ratioPreset` brew settings from #1364 are retained and reused.

### D6: Extend `scaleWeight` with a per-instance `dataMode`

Per request, the existing Scale widget gains a `dataMode` per-instance property (`gross`|`netBeans`|`netMilk`|`contextAware`) read via `getItemProperties(itemId)`, default = current behaviour. Editing reuses the per-instance editor path (long-press in-app, `openEditor` on web) with a mode picker writing through `setItemProperty` / `/api/layout/item`. Context detection reuses the existing `activePresetFunction === "steam"` signal.

### D7: Supersede the PR #1364 mode flag and hardcoded bar

Once zone + zone-options + widgets exist, `simplifiedHome` and the hardcoded `brewStatusBar` are removed; the "simplified home" becomes user-composed or an optional default layout pre-filling `lowerMidBar` (with `equalWidth` distribution + `accent` background). The layout system stays the single source of truth.

## Risks / Trade-offs

- **[Runtime-hidden zone confuses users]** Configured on a large display, vanishes on a small one → Mitigation: editor always exposes the zone, config preserved, editor labels it hidden-when-short.
- **[Zone-options scope creep]** "Other obvious options" can balloon → Mitigation: ship exactly distribution + alignment + background now; the `zoneOptions` map is extensible later without schema churn.
- **[Editor gesture collision]** Double-click/long-press on a zone must not clash with widget selection or scrolling → Mitigation: trigger only on the zone body (empty area / header), not on widgets; keep widget long-press = per-instance editor.
- **[Web/in-app parity drift]** Two editor codebases → Mitigation: both read/write the same `zoneOptions` map and `/api/layout/*` endpoints; parity is an explicit verification task.
- **[Theme contrast on custom palettes]** Accent background + text could fail contrast on user themes → Mitigation: derive contrast text from the theme's existing contrast tokens (the same ones the PR used), not a computed guess.
- **[Migration]** Older configs lack `lowerMidBar`/`zoneOptions` → Mitigation: migration adds an empty `lowerMidBar`; absent `zoneOptions` default to current behaviour, so nothing changes.

## Migration Plan

1. Add empty `lowerMidBar` to `defaultLayoutJson()` + migration ensuring existing configs gain it empty (mirrors `statusBar`).
2. Add `zoneOptions` storage + accessors; absent values default to today's behaviour (no-op for existing layouts).
3. Land widgets, zone rendering, zone-options editor, and theme roles behind no flag — empty zone + default options = no-op for all existing users.
4. Remove `simplifiedHome` + hardcoded `brewStatusBar` (PR #1364 rework). Optionally ship a "Simplified home" default layout pre-filling `lowerMidBar`.
5. Rollback: revert rendering + registrations; leftover empty `lowerMidBar`/default `zoneOptions` in saved configs are inert.

## Open Questions

- Exact `barHeight` / `minCenterContent` thresholds for the height gate — pick during implementation, confirm on the smallest supported tablet.
- Should "populate from preset" be offered on all zones or only where a preset makes sense (e.g. "Brew bar" only on bar-shaped zones)? Default: offer presets filtered by zone shape.
- Which "other obvious" zone options beyond distribution/alignment/background are worth the first cut (e.g. per-zone spacing, padding)? Default: start minimal, extend the `zoneOptions` map as needed.
