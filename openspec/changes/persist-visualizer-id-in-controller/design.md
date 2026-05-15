## Context

`VisualizerUploader::uploadShot()` / `uploadShotFromHistory()` build JSON and call `sendUpload()`; on HTTP completion `onUploadFinished()` parses the returned UUID and emits `uploadSuccess(visualizerUuid, url)` (`visualizeruploader.cpp:436`). The uploader holds **no DB shot-id context** at any point in this chain. The only code that turns that signal into a DB write is QML: `PostShotReviewPage.qml:332-338` and `ShotDetailPage.qml:137`, each calling `ShotHistoryStorage::requestUpdateVisualizerInfo(pageShotId, …)` gated on a transient page id. Shot-end auto-upload is launched from `MainController` (`maincontroller.cpp:1981`); the HTTP round-trip resolves ~1 s after `saveShot()` (DB id lands in `m_lastSavedShotId` via the `shotSaved` lambda at `maincontroller.cpp:1907`). If no review/detail page is alive with the right id when `uploadSuccess` fires, the link is lost forever even though the cloud has the shot.

`requestUpdateVisualizerInfo(qint64 shotId, QString visualizerId, QString visualizerUrl)` (`shothistorystorage.cpp:1282`) already does the correct background-thread DB write and emits `visualizerInfoUpdated`. It just needs to be driven from C++ with the right id.

## Goals / Non-Goals

**Goals**
- Exactly one authoritative, UI-independent path that persists a successful upload's Visualizer id/url to the originating local shot row.
- A one-time, bounded, idempotent reconciliation that relinks already-uploaded-but-unrecorded shots and, on relink, corrects any stale cloud rating from the local source of truth.
- No timers/delays as race fixes (CLAUDE.md). Correctness from explicit id correlation.

**Non-Goals**
- Reworking the shipped migration-16 inferred-rating back-sync (change `remove-inferred-shot-ratings`). Out of scope; this change only makes `visualizer_id` reliably populated.
- A general two-way Visualizer sync. Reconciliation is one-directional (cloud → local link, then local → cloud rating correction) and run-once.
- A user-facing setting. The run-once flag is internal QSettings only.

## Decisions

### D1 — Thread the DB shot id through the uploader; new signal `uploadSucceededForShot(qint64 dbShotId, QString visualizerId, QString url)`
Add an explicit `qint64 dbShotId` parameter to `uploadShot()`, `uploadShotFromHistory()`, `uploadShotFromHistoryWithOverrides()`. Store it in a per-upload member (`m_uploadingDbShotId`) set at call time; `onUploadFinished()` emits the new signal carrying that id alongside the existing `uploadSuccess(uuid,url)` (kept for UI status text / back-compat). Rejected: relying on `MainController::m_lastSavedShotId` at signal time — fails for overlapping shots and for history re-uploads (`uploadShotFromHistory` with `shot.id != lastSaved`). Rejected: a correlation map keyed by reply pointer — callers (shot-end, manual re-upload, history re-upload) are mutually exclusive and never issue overlapping uploads, so a single member is sufficient and simpler. (`m_uploading` is a UI state flag, not a concurrency guard — the single-member correlation relies on caller discipline, documented at the member declaration.)

For the shot-end auto path, the id is captured in the existing `shotSaved` lambda (`maincontroller.cpp:1900`) and passed into the `uploadShot(...)` call there; uploads are deferred until after save in that path already.

### D2 — `MainController` owns persistence
`MainController` connects `VisualizerUploader::uploadSucceededForShot` → `m_shotHistory->requestUpdateVisualizerInfo(dbShotId, visualizerId, url)`. The QML `onUploadSuccess` persistence calls in `PostShotReviewPage.qml` / `ShotDetailPage.qml` are removed; any pure UI refresh (e.g. reload after `visualizerInfoUpdated`) stays. One writer, no UI dependency.

### D3 — Reconciliation: list-and-match by start time, run-once, bounded, self-correcting
A `ShotHistoryStorage` background helper:
1. Skip if QSettings `visualizerBackfill/doneV1` is set, or Visualizer credentials absent (no flag write → retried next boot once configured).
2. `GET https://visualizer.coffee/api/shots` with paging (auth as in `testConnection`), bounded to a recent window (default last 60 days) so it cannot sweep full cloud history.
3. Build a set of Visualizer-side `(clock → {uuid,url})` from the list response. The `GET /shots` list returns only `ShotSummary` = `{id, clock, updated_at}` (confirmed against OpenAPI 1.8.2): `clock` is the shot start unix-seconds, which is exactly what Decenza uploads as the shot epoch — so it is the correct correlator. The list does **not** carry `espresso_enjoyment`. For each local `shots` row with empty `visualizer_id` whose `timestamp` is in-window, match to a Visualizer shot whose `clock` is within ±`kReconcileToleranceSec` (2 s — rounding only). A Visualizer uuid already present on any local row, or already consumed in this pass, is never reused. Ambiguous match (≥2 cloud shots within tolerance for one local row, or vice-versa) → skip that row, log, leave for manual handling.
4. For each linked row: persist `visualizer_id`/`visualizer_url` (reuse the `requestUpdateVisualizerInfo` SQL/path), then **unconditionally** dispatch `updateShotOnVisualizer(...)` for it. Rationale: the list summary lacks `espresso_enjoyment`, so a "compare-then-maybe-PATCH" design would need an extra `GET /shots/{id}` per linked row purely to read the cloud rating. An unconditional PATCH is the strict superset (cloud always converges to the now-authoritative local value — corrected 0/default after #1155, or the user's real rating), is idempotent, and the linked set is bounded to the orphan cohort. The PATCH builder already sends JSON `null` for a 0/unrated value (per #1155). This is what makes the orphan-repair independent of migration 16's queue, with zero extra GET calls.
5. Set `visualizerBackfill/doneV1` only after a fully completed pass (network error mid-pass → no flag, retried next boot; partial links already persisted are idempotent).
6. Triggered from `MainController` on the existing post-boot Visualizer-ready path (same hook the migration-16 drain uses), off the main thread via `withTempDb` + queued callback.

### D4 — Matching key rationale
Decenza and Visualizer share no stable identifier (different UUID spaces; Decenza does not store the cloud id until this fix). Shot start epoch is the only reliable correlator: Decenza uploads `clock`/`timestamp` = shot epoch, and the API exposes the shot start time. Tolerance kept tight (2 s) and 1:1 (no reuse, ambiguity → skip) to bound mis-link risk.

## Risks / Trade-offs

- **[Mis-link via timestamp collision]** Two shots within 2 s, or clock skew between device-at-upload and API-reported start. → Tight tolerance, strict 1:1, ambiguity-skips-not-guesses, bounded window. A wrong link only mis-attributes a cloud rating PATCH for one shot; user-visible and correctable, not destructive to shot data.
- **[Cross-device / multi-account cloud shots]** The user's Visualizer library may include shots uploaded from de1app or another device that have no local row. → Only local rows drive matching; unmatched cloud shots are ignored. Reconciliation never creates local rows.
- **[Window cutoff]** Orphans older than the window stay unlinked. → Acceptable: the bug is ~2 weeks old; 60 days is generous. Window is a constant, tunable in a follow-up if needed; the run-once flag means widening it later needs a `doneV2` bump (note in tasks).
- **[API shape/paging]** `GET /api/shots` paging/field names per `apidocs.visualizer.coffee` (OpenAPI 1.8.2) — confirm the start-time field name during implementation; fail safe (no flag set) on any parse/HTTP error.
- **[Self-correct PATCH overwrites a user's cloud-side edit]** Same documented trade-off as #1155's PATCH semantics (local is source of truth). Scope is only freshly-linked orphan rows, a small bounded set.
