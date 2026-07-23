## Context

Both Purge buttons live entirely in `qml/pages/SteamPage.qml`, which contains two views
selected by state:

- **Live view** (`// === STEAMING VIEW ===`, ~line 544): shown while steaming (and, for the
  action row, on headless machines). Its `liveActionRow` (lines 1099–1263) holds two
  controls:
  - `livePurgeButton` (Rectangle, lines 1119–1169) — `visible: isSteaming`, handler
    `DE1Device.requestIdle()`.
  - `steamStopButton` (Rectangle, lines 1175–1261) — `visible: DE1Device.isHeadless`, a
    one- or two-tap Stop that also ends in `requestIdle()` (plus `root.goToIdle()`).
- **Settings view** (`// === SETTINGS VIEW ===`, line 1268): shown when not steaming.
  `settingsPurgeButton` (Rectangle, lines 1651–1691) — handler `DE1Device.requestIdle()`.

Investigation confirmed: `requestIdle()` writes `Idle` unconditionally
(`src/ble/de1device.cpp`), and the firmware performs the wand purge only on a live
Steam→Idle transition. The settings view renders only when the machine is not steaming, so
`settingsPurgeButton` can never cause a purge — it is dead. `livePurgeButton` issues the same
command as Stop; on headless machines Stop already covers it, and on GHC machines the
physical group-head button stops-and-purges. The user has confirmed the GHC path is
sufficient and wants no on-screen mirror.

Focus-order plumbing links these buttons to their neighbors and must be repaired, not just
deleted:
- `steamStopButton` targets `livePurgeButton` via `Keys.onBacktabPressed` (line 1206).
- `livePurgeButton`'s own `KeyNavigation.tab`/`backtab` (lines 1133–1141) route to
  `steamStopButton` / preset pills / `steamingFlowSlider`.
- `settingsPurgeButton` is a `KeyNavigation` target of neighboring settings controls
  (e.g. `durationSlider` backtab at line 1722; `addPitcherButton` region ~line 1574).

## Goals / Non-Goals

**Goals:**
- Delete `livePurgeButton` and `settingsPurgeButton` (Rectangles + their `AccessibleTapHandler`s).
- Preserve a coherent keyboard/screen-reader focus order across both views.
- Leave `DE1Device::requestIdle()` and the `steam.label.purge` / `steam.accessible.purge`
  translation keys in place (still used by the headless Stop button).
- Keep the change contained to `SteamPage.qml` plus the wiki Manual.

**Non-Goals:**
- No change to `steamStopButton` behavior (one-/two-tap stop, soft-stop, auto-purge).
- No change to `DE1Device`, MCP tools, or any C++.
- No removal of translation keys.
- No new in-app steam-stop affordance for GHC machines (physical control is sufficient).

## Decisions

**1. Delete both buttons rather than hide them.**
Hiding (`visible: false`) would leave dead controls, stale focus targets, and confusing
source. The buttons have no other role, so they are removed outright. Alternative (feature
flag / setting) rejected — no one has asked to keep purge, and a toggle for a redundant
button is over-building.

**2. Re-point every focus reference to the nearest surviving control.**
For the live view, with `livePurgeButton` gone the action row holds only `steamStopButton`
(headless-only). Its `Keys.onBacktabPressed` (line 1206) must no longer fall back to
`livePurgeButton`; it should backtab to `steamPage.lastVisiblePresetPill()`. Any tab chain
that entered the row via `livePurgeButton` should now enter via `steamStopButton` (when
visible) or the preset pills. For the settings view, controls that tabbed to/from
`settingsPurgeButton` (`durationSlider` backtab, `addPitcherButton` neighbor) are re-linked
directly to each other so the chain stays closed.
Alternative (leave references and rely on `?? fallback` guards) rejected — dangling `id`
references to a deleted element are a QML error/warning and violate the "fix it in the file
you touch" rule.

**3. On non-headless machines the live action row simply renders empty during steaming.**
`liveActionRow`'s `visible: isSteaming || DE1Device.isHeadless` and its spacing already
tolerate a single- or zero-child state; with `livePurgeButton` removed and `steamStopButton`
headless-only, a GHC machine shows no button in that row while steaming — matching the intent
(steam stop is a group-head action). No layout placeholder is added.

## Risks / Trade-offs

- **[Broken focus order / QML warnings from dangling `id` references]** → Grep the whole file
  for `livePurgeButton` and `settingsPurgeButton` after deletion; every hit must be removed or
  re-pointed. QML tested manually (no QML harness) — verify tab/backtab by keyboard on both
  views, headless and non-headless.
- **[GHC users lose the only on-screen steam-stop during steaming]** → Accepted and confirmed
  by the user; the physical group-head control stops and auto-purges. Headless machines are
  unaffected (Stop remains).
- **[Accidentally removing a still-used translation key]** → `steam.label.purge` and
  `steam.accessible.purge` are also referenced by `steamStopButton` (lines 1225, 1241); leave
  both keys untouched. Only the button QML is deleted.
- **[Manual/app drift]** → Update the wiki Manual's Steam section in the same change so the
  documented UI matches.

## Migration Plan

Pure UI removal; no data, settings, or protocol migration. Rollback is reverting the single
QML file (and the wiki edit). No version-gated behavior.

## Open Questions

None.
