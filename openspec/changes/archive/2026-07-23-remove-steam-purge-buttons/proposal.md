## Why

The two "Purge" buttons on the Steam page do not earn their place. The settings-view
Purge is effectively dead — the settings view only renders while the machine is **not**
steaming, so tapping it writes `Idle` to an already-idle machine, produces no Steam→Idle
transition, and therefore never fires the firmware steam-wand purge. The live-view Purge
issues the exact same `DE1Device.requestIdle()` command as stopping steam: on headless
machines the Stop button beside it already covers that, and on GHC machines the physical
group-head control stops steam (and auto-purges) without any on-screen button. In both
cases the Purge button is redundant screen clutter rather than a distinct capability.

## What Changes

- Remove the settings-view Purge button (`settingsPurgeButton`) from the Steam page — a
  confirmed no-op control.
- Remove the live-steaming-view Purge button (`livePurgeButton`) from the Steam page. On
  headless machines the adjacent Stop button remains the stop-and-purge control; on GHC
  (non-headless) machines steam is stopped from the physical group-head button, which the
  user has confirmed works well and does not need an on-screen mirror.
- Repair the keyboard-navigation (`KeyNavigation` / tab / backtab) chains that referenced
  the removed buttons so focus order stays intact for keyboard and screen-reader users.
- Keep the `steam.label.purge` and `steam.accessible.purge` translation keys — they are
  still used by the headless Stop button's two-tap purge state.
- Update the wiki Manual's Steam page description to drop the Purge button.

## Capabilities

### New Capabilities
- `steam-stop-controls`: Defines which on-screen controls the Steam page presents for
  stopping steam and triggering the firmware steam-wand purge, and asserts that no dedicated
  "Purge" button is offered — purge occurs as a side effect of stopping (headless Stop
  button) or is handled by the machine's physical group-head control (GHC machines).

### Modified Capabilities
<!-- None. No existing spec documents the Steam-page purge buttons; the machine-maintenance
     spec's "air purge" is the unrelated Transport-mode drain feature. -->

## Impact

- **QML**: `qml/pages/SteamPage.qml` only — remove two `Rectangle` button blocks and their
  `AccessibleTapHandler`s, and fix the `KeyNavigation`/`Keys.onTab`/`onBacktab` references
  that pointed at `livePurgeButton` and `settingsPurgeButton`.
- **C++**: None. `DE1Device::requestIdle()` stays — it has many other callers (main.qml,
  layout items, the headless Stop button, the `machine_stop` MCP tool). No slots, signals,
  or MCP tools become unused.
- **Translations**: No key removed; `steam.label.purge` / `steam.accessible.purge` remain
  referenced by the headless Stop button.
- **Docs**: GitHub wiki Manual (Steam page) updated to match.
- **Behavior**: No in-app control lost on headless machines (Stop remains). On GHC machines
  the only change is the disappearance of a redundant on-screen button; steam stop + purge
  continues via the group-head control.
