## Context

The May-4 `add-shot-rating-capture` change shipped three layers to combat advisor "rating starvation" (a user database with 199 unrated shots was the originating evidence). Layers 1 and 2 (conversational capture, QuickRatingRow) directly elicit ratings from the user and have proven their worth in practice. Layer 3 — auto-stamping 75 on clean shots — was the cheap fallback layer, intended to keep `bestRecentShot` warm when the elicitation layers haven't yet collected user ratings.

Two weeks of real-world data showed Layer 3 was a net negative. Issue [#1150](https://github.com/Kulitorum/Decenza/issues/1150) makes the user-visible failure concrete: the Visualizer "Default Shot Rating" setting (a long-standing user preference) is silently overwritten by the auto-rating. On the advisor side, the inferred score is paired with `confidence: "inferred"` and a system-prompt instruction telling the LLM to treat it as a hint requiring user confirmation — i.e., the value is engineered to be ignored. Meanwhile the LLM already receives the underlying detector signals (`verdictCategory`, `channelingSeverity`, `grindDirection`) which carry the same information directly.

This change removes Layer 3 entirely and restores the pre-Layer-3 contract: a shot's rating is what the user said it is, full stop.

## Goals / Non-Goals

**Goals:**
- Stop auto-stamping `enjoyment=75` on saved shots.
- Restore the Visualizer "Default Shot Rating" setting as the single source of truth for unrated-shot rating.
- Remove all `enjoymentSource` / `confidence: "inferred"` plumbing from the C++ surface, MCP tools, AI system prompts, and `ShotProjection`.
- Drop the `shots.enjoyment_source` column from the SQLite schema cleanly.
- Reset the ~11 days of auto-stamped 75s already in user databases to the user's configured default, so they stop polluting `bestRecentShot` selection and Visualizer uploads.
- Keep Layer 1 and Layer 2 untouched.

**Non-Goals:**
- Reworking `bestRecentShot` selection beyond removing the inferred tier (it falls back to the pre-Layer-3 "highest user-rated in last 90 days" rule).
- Changing the elicitation prompts in Layer 1 / Layer 2.
- Building a richer rating provenance model (`source`, `confidence`, etc.) — the experiment showed we don't need it.
- Downgrade compatibility. Downgrading to a Layer-3 build after this lands will re-add the column at default `'none'`; the reset enjoyment values stay reset.

## Decisions

### Drop the column, don't leave it dormant

Considered: keep `shots.enjoyment_source` in the schema as a deprecated column, stop reading/writing it, deal with cleanup later.

Chose: drop the column in the same migration that resets affected rows.

**Rationale:** Jeff's call — "drop the column as that is generally a safe thing and it could cause confusion later." Dormant columns rot: a future engineer reading the schema will wonder what `enjoyment_source` is for, find archived spec text referencing it, and waste time. SQLite ≥ 3.35 supports `ALTER TABLE ... DROP COLUMN`, which is already used by migration 15 (dropping `temperature_unstable`). No new platform constraint.

### Migrate `enjoyment_source = 'inferred'` rows to the user's configured default, not 0

Considered:
- Reset to `0` (simplest).
- Reset to `75` (preserve the inferred score as if a user had rated it 75).
- Reset to the user's `defaultShotRating` setting (Jeff's call).

Chose: reset to `defaultShotRating`.

**Rationale:** The bug report's pain point is exactly that the user's configured default was overridden. The most coherent rollback is to land each affected row on the value it *would have had* if Layer 3 had never run — which is the user's default at save time. We don't have per-row history of that setting, so the current value of `shot/defaultRating` (read directly from QSettings during migration) is the best proxy. Resetting to `0` is wrong because most users have a non-zero default and it would create a different surprising "Visualizer shows 0% on real shots" failure. Keeping `75` is wrong because it bakes the bug into history.

Edge case: a user changes `defaultShotRating` between save time and migration time. We use the value at migration time. This is a one-time fix-up; if it lands a few rows on the wrong number, the user can re-rate those shots via the existing UI. The alternative (per-row reconstruction) would require shot timestamps and a setting-change audit log neither of which we have.

### Migration reads QSettings directly, not the `Settings` façade

Migrations run inside `ShotHistoryStorage::initializeDatabase` on the worker thread. The `Settings` C++ object may or may not be fully constructed at that point depending on init order. `QSettings` itself is thread-safe and the key is stable (`shot/defaultRating`, default `75`). The migration reads it via a fresh `QSettings` instance and the same hard-coded default as `SettingsVisualizer::defaultShotRating()`. No new dependency wiring.

### `bestRecentShot` falls back to "highest user-rated in last 90 days"

Pre-Layer-3 behavior: `bestRecentShot` resolved to the highest `enjoyment0to100` shot in the 90-day window with a non-null rating. We restore this exactly: drop the user/inferred tier-preference, drop the `confidence` field, keep the 90-day window and the existing per-profile filter. A user with no rated shots in 90 days simply doesn't get a `bestRecentShot` block — same as before May 4. This is acceptable; the elicitation layers (1 and 2) are now responsible for keeping that pool populated.

### No grace period / feature flag

Considered shipping the code change behind a flag and removing it after a release cycle.

Chose: no flag. Layer 3 has shipped to production for ~11 days; rolling it back cleanly is preferable to a tri-state code path. The DB migration is one-way per the proposal, and the C++ removal is local and well-contained.

## Risks / Trade-offs

- **Risk:** A migration-time crash leaves the DB in an intermediate state — column dropped but enjoyment reset only partially, or reset done but column drop failed. **Mitigation:** Wrap the migration in a transaction (Qt's `QSqlDatabase::transaction()`); the existing migration runner pattern already uses transactions per migration step. If the transaction rolls back, the migration retries on next launch.
- **Risk:** Existing inferred rows that the user has *since* re-rated via the editor (so `enjoyment_source = 'user'`) won't be touched — correct behavior, but worth noting that the migration's scope is narrow (`WHERE enjoyment_source = 'inferred'` only).
- **Risk:** `bestRecentShot` immediately goes dark for users who had been relying on inferred fallback. **Mitigation:** Acceptable. The elicitation layers now do their job; the dark block was the failure mode that motivated Layer 3 in the first place, but Layer 3's "fix" was making the LLM ignore its own output. Going dark surfaces the real problem (user hasn't rated) instead of hiding it.
- **Trade-off:** Loses the ~70 lines of telemetry-like signal that "this shot looked detector-clean." The data is fully reconstructible from the existing detector columns (`channeling_detected`, `pour_truncated_detected`, etc.) — no information is lost, only a derived field.
- **Risk:** Tests in `tests/shotanalysis_tests`, `tests/dialing_blocks_tests`, `tests/aimanager_tests`, and `tests/mcptools_tests` reference `enjoymentSource` / `confidence`. The task list enumerates each test to delete or update — failing to update one will break the build. **Mitigation:** post-implementation `grep -rn enjoymentSource\\\\|enjoyment_source\\\\|inferred-good\\\\|bestRecentShot.confidence src/ tests/` should return empty.

## Migration Plan

1. **Code lands** (PR): all C++ / MCP / system-prompt code is removed in a single PR alongside the migration. Build green, tests green, manual smoke (run an unrated shot, confirm it saves with the Visualizer default and not 75).
2. **Migration runs on first launch** of the new build: each user's DB picks up migration 16. Affected rows reset to `shot/defaultRating`; column dropped.
3. **Verification:** the MCP `shots_list` tool no longer accepts `enjoymentSource` as a filter; the dialing-context envelope no longer carries `confidence` on `bestRecentShot`; new shots saved clean no longer auto-rate. (Manual + the migration test in `tests/shothistorystorage_tests`.)
4. **Rollback strategy:** not supported. Downgrading to a Layer-3 build re-runs migration 14 which adds the column back at `'none'` — no schema crash, but users would see clean shots auto-rated again. We accept this; the Decenza release cadence is forward-only via auto-update.

## Open Questions

None — Jeff's two clarifications (drop the column, reset to user's default rather than 0) resolved the remaining ambiguity. Implementation can proceed.
