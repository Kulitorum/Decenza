## Why

Removing the default shot rating (#1561) deleted the setting but left the sticky
`dyeEspressoEnjoyment` field that the setting used to feed. `MainController` still
read that field into every shot save, and because it is persisted, the last value
it ever held survived the upgrade and landed on the next shot saved — a real shot
came out rated 50 with nobody having rated it. That silently suppresses the AI
taste intake, whose gate treats any non-zero enjoyment as feedback the user
already gave: the exact failure #1561 set out to fix, moved one shot later.

The underlying cause is that no spec ever stated where a shot's rating comes from
at save time. `shot-rating-capture` documents the capture paths and `taste-intake`
documents the gate, but nothing said "a rating comes only from a person, and no
setting supplies one." A settings-sourced rating was therefore not obviously wrong
to anyone reading the specs, and the leak went unnoticed.

## What Changes

- Shots SHALL save unrated (`enjoyment 0`). No setting supplies a shot rating.
- **BREAKING**: `SettingsDye::dyeEspressoEnjoyment` removed — property, getter,
  setter, signal, backup serialize/restore, and both MCP surfaces
  (`settings_get`, `settings_set`). MCP clients that wrote
  `settings_set dyeEspressoEnjoyment` must use `shots_update enjoyment0to100`.
- Migration 16 resets `enjoyment_source = 'inferred'` rows to `0` instead of
  reading the now-dead `shot/defaultRating` key. Consistent with the removal, and
  the back-sync PATCHes those shots to **Unrated** on Visualizer (the PATCH
  builder sends `null` for 0), which is what an app-invented rating always should
  have been.
- Both dead keys (`shot/defaultRating`, `dye/espressoEnjoyment`) are evicted from
  the settings store on construction, so no bogus rating sits around waiting to
  leak into something.
- Shots already carrying a rating produced by the user's own configured default
  are left alone — see design.md for why that differs from the inferred rows.
- Fixes pre-existing drift in `shot-rating-capture`: it still documents "Layer 2 —
  Quick rating row" (a three-icon high/medium/low row with a per-shot dismiss).
  That component was removed in #1245 and replaced by the inline slider in #1243.

## Capabilities

### New Capabilities

<!-- none -->

### Modified Capabilities

- `shot-rating-capture`: adds a requirement that a saved shot is unrated and that
  no setting may supply a rating; corrects the stale Layer 2 quick-rating-row
  description to the shipped inline slider.
- `settings-architecture`: drops the removed `setDefaultShotRating` →
  `setDyeEspressoEnjoyment` cross-domain wiring example, which cited code that no
  longer exists on either side.

## Impact

- `src/core/settings_dye.{h,cpp}` — property and accessors removed
- `src/core/settings.cpp` — one-time eviction of both dead keys
- `src/core/settingsserializer.cpp` — backup serialize/restore entry removed
- `src/mcp/mcptools_settings.cpp`, `src/mcp/mcptools_write.cpp` — MCP surfaces removed
- `src/controllers/maincontroller.cpp` — three shot-save reads and two post-save resets removed
- `src/history/shothistorystorage.cpp` — migration 16 resets to 0, legacy key read removed
- `tests/tst_settings.cpp`, `tests/tst_dbmigration.cpp` — regression coverage
- `docs/MCP_TEST_PLAN.md` — `dyeEspressoEnjoyment` removed from the dye key list
- MCP consumers: `settings_get` no longer returns `dyeEspressoEnjoyment`;
  `settings_set` no longer accepts it
