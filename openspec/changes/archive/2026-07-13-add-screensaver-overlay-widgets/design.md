## Context

The screensaver (`ScreensaverPage.qml`) picks one of five full-screen backgrounds (Videos, 3D Pipes, Flip Clock, Strange Attractors, Shot Map) plus a "Turn Screen Off" mode. Today the only overlay content is a clock, and it isn't one setting — it's four separate C++ booleans on `ScreensaverManager` (`videosShowClock`, `pipesShowClock`, `attractorShowClock`, `shotMapShowClock`), each independently checked by a hardcoded `Text` element in `ScreensaverPage.qml` anchored bottom-right. Flip Clock has no toggle (the background is inherently a clock).

This is architecturally distinct from the home screen's zone/library layout system (`SettingsLayoutTab.qml`, `LayoutEditorZone.qml`, widget catalog) — that system positions many widget instances across nine named zones on the idle page. The screensaver isn't a multi-zone canvas; it's one full-screen background with a small, fixed amount of glanceable overlay content. Reusing the full zone system here would be solving a problem this feature doesn't have.

Two GitHub issues motivate widening the overlay beyond the clock: [#1284](https://github.com/Kulitorum/Decenza/issues/1284) (water level, so a refill need is visible before waking the machine) and [#1310](https://github.com/Kulitorum/Decenza/issues/1310) (a link to an external page — e.g. a recipe site — reachable without waking the machine).

## Goals / Non-Goals

**Goals:**
- Let each screensaver background (except Flip Clock/Turn Screen Off, as applicable) show Clock, Water Level, Shot Plan, Battery, and a configurable Link Button. Clock stays scoped per background type (its existing storage); the other four are single global toggles/config shared across every background.
- Reuse existing data sources and existing compact-widget rendering — no new telemetry plumbing.
- Keep the settings surface small: a chip group, not a new drag-and-drop editor.
- Mitigate OLED/AMOLED burn-in risk for the now-larger set of fixed-position overlay elements, without adding a user-facing setting.

**Non-Goals:**
- Reordering or repositioning overlay items by the user — positions are fixed by design (clock bottom-right, readouts top-right, link button bottom-left).
- Any new overlay item beyond these four readouts + one link button in this change.
- An in-app browser/WebView — the link button opens the system browser, same as every other external link in this app (`Qt.openUrlExternally`).
- Any change to the home-screen zone/library layout system or the 5-type background selector.

## Decisions

### Chip group, not a drag-and-drop chip editor
The candidate set of "things meaningful to show while the DE1 is asleep" is small and fixed: of every readout in the app's widget catalog and Custom-widget token list, only water level, the next-shot preview (Shot Plan), the tablet's own battery, and the clock hold any information while the machine is idle — temperature, pressure, flow, scale weight, shot timing, scale battery, and connection state all read as zero/meaningless with nothing brewing. A fixed, small set doesn't need the reorderable Shown/Available chip editor already built for the Shot Plan widget (`ScreensaverEditorPopup.qml`) — a simple multi-select chip group (tap to toggle membership) in the existing Display settings card covers it with far less UI.

**Alternative considered**: reuse the full drag-and-drop chip editor pattern. Rejected — it solves a reordering problem this feature doesn't have, at added implementation and interaction cost.

### Fixed positions, grouped by content type
Clock stays bottom-right (unchanged, avoids touching its existing per-type storage). Water Level, Shot Plan, and Battery render as a compact icon row along the top, right-aligned — reusing the same compact rendering (`isCompact`) each widget already has for the home screen's `StatusBar` (which is itself hidden during the screensaver via `visible: !root.screensaverActive` in `main.qml`). This keeps the readout row visually consistent with what users already see on the home screen, and — being a thin top row rather than a stacked block — covers less of the moving background than a corner stack would. The Link Button, being interactive rather than a passive readout, gets its own bottom-left position so it isn't a mis-tap target buried among text/icon chips.

**Alternative considered**: single stacked corner (e.g. all items top-right). Rejected in favor of reusing the StatusBar's existing horizontal compact layout — less visual footprint, more visual consistency, and no new rendering path needed for the three readouts.

### Global toggles for the four new items; Clock stays the one per-type exception
Water Level, Shot Plan, Battery, and the Link Button are each a single global setting (on/off, plus Label/URL for the button) shared across every background, rather than duplicated per background type. Unlike Clock — which genuinely diverged per type over time (Attractor defaults off, the others on) — there's no product reason a user would want Water Level on Pipes but not on Shot Map. Global settings mean ~6 new properties instead of 24, a smaller settings surface, and no risk of a user enabling something on one background and being confused it's off on another. Clock remains the one exception, keeping its existing per-type storage unchanged (per the decision not to touch it).

**Alternative considered**: full per-background-type independence for all five items, matching Clock exactly. Rejected as unnecessary duplication once the toggles are just "on/off," not the kind of thing that plausibly needs to differ by background.

### Clock unavailable on Flip Clock background
The Flip Clock background is inherently a clock; offering a redundant Clock overlay chip on top of it has no value and only invites visual duplication. The chip is simply not offered (not just disabled) when Flip Clock is the active background.

### Link button opens the system browser, one only
Matches the existing pattern for every other external link in this app (`Qt.openUrlExternally`, used ~15 places already) — no new WebView dependency. Scoped to exactly one configurable button (Label + URL) to match the literal ask in #1310 ("a named button"); a repeatable list is not built now.

**Interaction detail**: `ScreensaverPage.qml` currently wakes the machine on any tap via a full-screen `MouseArea` (z: 3) plus `Keys.onPressed`. The Link Button must sit above that in z-order and its tap handler must not propagate to the wake handler — tapping the button opens the URL and does nothing else; it must not also wake the DE1/scale.

### Anti-burn-in: slow positional drift, no new setting
The reference/shipped hardware (Samsung SM-X210 / Galaxy Tab A8, per `docs/CLAUDE_MD/PERFORMANCE_BASELINE.md`) is LCD and not subject to burn-in, but Decenza runs on BYO tablets and some are OLED/AMOLED. The codebase already treats this as a real concern — `main.qml` proactively closes any open popup before entering the screensaver specifically to avoid a static element sitting over it. Widening the overlay from one static element (today's clock) to four fixed anchor points increases that same existing exposure. The cheapest mitigation — each anchor point (clock, top row, link button) drifts a few pixels within its corner/edge on a slow cycle — is applied uniformly and requires no user-facing setting, consistent with this project's preference for fewer settings over more toggles.

## Risks / Trade-offs

- **Shot Plan's compact form is terser than its usual full-sentence rendering** → acceptable; it already has a compact mode built for bar placement, and terseness suits a thin top row.
- **Four fixed anchor points (vs. one today) marginally raises burn-in exposure on the OLED minority of devices** → mitigated by the slow positional drift; further mitigated by the existing dim/auto-sleep settings, which remain user-configurable.
- **Link Button's tap-consumption must not regress the existing wake-on-any-tap behavior for the rest of the screensaver** → covered by an explicit scenario in the spec; needs a manual pass across platforms during implementation since MouseArea z-ordering/event propagation can behave subtly differently between touch (Android/iOS) and mouse (desktop) input.

## Migration Plan

No data migration: `videosShowClock`/`pipesShowClock`/`attractorShowClock`/`shotMapShowClock` are read as-is to seed the Clock chip's initial state per background type on first load after upgrade, then continue to be the storage backing that chip (unchanged). The three new toggles and the link button default to off/unset for all users, new and existing — no prior state to migrate.

## Open Questions

- Exact drift bounds/period (e.g. ±4px over a few minutes) — left to implementation to tune against what's visually imperceptible at typical tablet viewing distance.
