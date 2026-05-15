## REMOVED Requirements

### Requirement: Inferred-good shots SHALL be auto-rated with `enjoymentSource == "inferred"`

**Reason:** Failed experiment. Two-week trial after the May-4 `add-shot-rating-capture` ship surfaced two failures: (1) issue #1150 — the auto-rating silently overwrites the user's configured Visualizer "Default Shot Rating" (a long-standing pre-existing setting), producing visibly-wrong shot ratings; (2) the AI advisor receives the inferred score paired with `confidence: "inferred"` and a system-prompt instruction to treat it as a hint requiring user confirmation, while it already has the underlying detector signals (`verdictCategory == "clean"`, `channelingSeverity == "none"`, `grindDirection == "onTarget"`) that the inferred score is derived from. The synthesized rating adds no decision-relevant information beyond what the LLM already sees.

**Migration:** The post-shot analysis pipeline SHALL no longer evaluate inferred-good criteria. The Visualizer "Default Shot Rating" setting (`shot/defaultRating`, exposed via `SettingsVisualizer::defaultShotRating`) becomes the sole default for unrated shots, restoring the pre-Layer-3 contract. Existing rows in the `shots` table where `enjoyment_source = 'inferred'` SHALL be reset to the user's configured default via a one-time schema migration that also drops the now-unused column (see the `enjoymentSource SHALL persist as a ShotProjection field` requirement's migration entry below).

### Requirement: `enjoymentSource` SHALL persist as a `ShotProjection` field with values `"none" | "user" | "inferred"`

**Reason:** With Layer 3 removed, the only meaningful states are "the user has rated this shot" (`enjoyment0to100 > 0`) and "they haven't" (`enjoyment0to100 == 0`). A separate provenance string adds no information beyond the boolean already encoded by the rating value, and propagates plumbing across `ShotProjection`, `ShotHistoryStorage`, the metadata-update path, the MCP `shots_list` filter and `updateShot` validator, and the import/export paths.

**Migration:** A one-time schema migration (numbered 16, immediately following the existing migration 15) SHALL:

1. Read the user's `shot/defaultRating` value from `QSettings` (fallback default `75`, matching `SettingsVisualizer::defaultShotRating`).
2. `SELECT id, visualizer_id FROM shots WHERE enjoyment_source = 'inferred' AND visualizer_id IS NOT NULL AND visualizer_id != ''` — for each result, append `{shotId, visualizerId}` to the `migration16/pendingVisualizerSync` `QSettings` list so the Visualizer cloud copy can be corrected after the app finishes booting.
3. `UPDATE shots SET enjoyment = <default> WHERE enjoyment_source = 'inferred'` — restoring the auto-stamped rows to the value they would have had if Layer 3 had never run.
4. `ALTER TABLE shots DROP COLUMN enjoyment_source` — SQLite ≥ 3.35 (already required by migration 15's `DROP COLUMN temperature_unstable`).
5. All four steps SHALL run inside a single `QSqlDatabase::transaction()` so any failure mid-flight rolls back cleanly and retries on next launch.
6. The migration SHALL be idempotent: re-running it (e.g., after a downgrade-then-upgrade cycle that re-added the column via migration 14) SHALL detect the column's presence, repeat steps 1-4, and finish without error.

**Visualizer back-sync (post-migration, on app boot):** `MainController` SHALL drain the `migration16/pendingVisualizerSync` `QSettings` list once Visualizer credentials are confirmed available, calling `VisualizerUploader::updateShotOnVisualizer(visualizerId, projection)` for each pending entry. On successful PATCH, the entry SHALL be removed from the list. On failure (no credentials, network error, server 4xx/5xx) the entry SHALL remain in the list and be retried on next boot. The drain SHALL process entries serially (one PATCH in flight at a time) and SHALL NOT use a timer guard — it hooks into the existing Visualizer-ready signal path. When the list is empty, the drain is a noop.

`ShotProjection::enjoymentSource` SHALL be removed. `ShotHistoryStorage::updateShotMetadataStatic` SHALL no longer accept an `enjoymentSource` key in its metadata map. The MCP `shots_list` tool SHALL no longer expose an `enjoymentSource` filter argument or emit the field in shot entries. The MCP `updateShot` tool SHALL no longer expose or validate an `enjoymentSource` argument. The import path (`import_history`) SHALL skip the `enjoyment_source` field when present in source data (forward-compat with older exports).

### Requirement: `bestRecentShot` SHALL surface a `confidence` field and prefer user ratings over inferred

**Reason:** With Layer 3 removed there are no inferred candidates to distinguish from user-rated ones. The pre-Layer-3 contract — `bestRecentShot` resolves to the highest user-rated shot in the 90-day window, omitted entirely when no rated candidate exists — is restored. The `confidence` field becomes meaningless under that contract.

**Migration:** The `bestRecentShot` block SHALL no longer carry a `confidence` field. Candidate selection SHALL revert to "highest `enjoyment0to100` in the 90-day window where `enjoyment0to100 > 0`, scoped to the relevant profile per the existing rules." Shots with no user rating SHALL NOT be eligible (same as the pre-Layer-3 contract). The system prompt SHALL no longer teach `bestRecentShot.confidence` semantics or the "treat inferred as a hint" framing.

### Requirement: `currentBean` and `dialInSessions[].shots[]` SHALL surface `enjoymentSource` only when "inferred"

**Reason:** No shots are ever stamped `"inferred"` after this change, so the field is always absent. Keeping the conditional emission code is dead weight.

**Migration:** The `currentBean` block SHALL NOT carry an `enjoymentSource` key in any condition. Per-shot entries in `dialInSessions[].shots[]` SHALL NOT carry an `enjoymentSource` key in any condition. The dialing-block builders (`buildCurrentBeanBlock`, `buildDialInSessionsBlock`, `buildBestRecentShotBlock` in `src/ai/dialing_blocks.h`) SHALL remove the conditional emission code paths. The system prompt SHALL remove the paragraphs teaching `currentBean.enjoymentSource: "inferred"` and `dialInSessions[].shots[].enjoymentSource: "inferred"`.
