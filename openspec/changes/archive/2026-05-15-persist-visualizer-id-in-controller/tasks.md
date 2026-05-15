## 1. Thread DB shot id through the uploader (Component A)

- [x] 1.1 In `src/network/visualizeruploader.h`, add a new signal `uploadSucceededForShot(qint64 dbShotId, const QString& visualizerId, const QString& url)` (keep existing `uploadSuccess(QString,QString)` for UI status). Add a private `qint64 m_uploadingDbShotId = 0;` member.
- [x] 1.2 Add a `qint64 dbShotId` parameter to `uploadShot(...)`, `uploadShotFromHistory(const ShotProjection&)`, and `uploadShotFromHistoryWithOverrides(...)` in `.h`/`.cpp`. Each sets `m_uploadingDbShotId = dbShotId` before `sendUpload(...)`. (`uploadShotFromHistory` can default to `shotData.id`.)
- [x] 1.3 In `visualizeruploader.cpp:onUploadFinished()`, after parsing a non-empty `shotId`, also `emit uploadSucceededForShot(m_uploadingDbShotId, shotId, m_lastShotUrl)` (alongside the existing `uploadSuccess`). Reset `m_uploadingDbShotId = 0` after emit. On failure/no-id, do not emit; leave it reset.
- [x] 1.4 Update all `m_visualizer->uploadShot(...)` / `uploadShotFromHistory*(...)` call sites in `src/controllers/maincontroller.cpp` (the shot-end auto-upload at ~1981, the manual path at ~2046, and any history re-upload) to pass the correct DB shot id. For the shot-end path, pass the id captured in the `shotSaved` lambda (`maincontroller.cpp:1900`); ensure the `uploadShot` call is sequenced after the id is known (it already is on that path).
- [x] 1.5 Update QML callers `uploadShotFromHistory` / `uploadShotFromHistoryWithOverrides` (e.g. `ShotDetailPage.qml`, `PostShotReviewPage.qml`) to pass the shot's DB id, or rely on the `shotData.id` default — verify each call site compiles and passes the right id.

## 2. MainController owns persistence (Component A)

- [x] 2.1 In `MainController` (constructor / boot wiring, near the existing visualizer connections added by #1155), `connect(m_visualizer, &VisualizerUploader::uploadSucceededForShot, this, ...)` → `m_shotHistory->requestUpdateVisualizerInfo(dbShotId, visualizerId, url)` when `dbShotId > 0` and `m_shotHistory` is ready.
- [x] 2.2 In `qml/pages/PostShotReviewPage.qml:332-338`, remove the `requestUpdateVisualizerInfo(editShotId, shotId, url)` call from `onUploadSuccess` (keep any UI-only refresh; the reload still happens via `onVisualizerInfoUpdated`).
- [x] 2.3 In `qml/pages/ShotDetailPage.qml:137`, remove the equivalent persistence call from its `onUploadSuccess` handler; keep UI refresh.
- [x] 2.4 Grep for any other QML/C++ caller of `requestUpdateVisualizerInfo` to confirm the C++ connection is now the sole persistence trigger (manual "re-upload from detail" is fine to keep if it still passes the right id, but it must not be the *only* path).

## 3. One-time reconciliation backfill (Component B)

- [x] 3.1 In `src/network/visualizeruploader.{h,cpp}`, add a method to fetch a bounded page set of the user's shots: `GET https://visualizer.coffee/api/shots?page=N&items=M` + the existing `authHeader()`. Response is `ShotListResponse` = `{data:[{id,clock,updated_at}], paging}` (confirmed OpenAPI 1.8.2 — `clock` is shot start unix-seconds; list has NO `espresso_enjoyment`). Page until older than the window cutoff or `paging` exhausted. Expose results as a list of `{visualizerId, url, clockEpoch}` (url = `VISUALIZER_SHOT_URL + id`). Emit `shotListFetched(QVariantList)` / `shotListFailed(QString)`; fail safe on any HTTP/parse error.
- [x] 3.2 In `src/history/shothistorystorage.{h,cpp}`, add a static/background helper `reconcileVisualizerLinks(...)` that, given the fetched cloud list, runs on a background thread via `withTempDb`: for each local `shots` row with empty `visualizer_id` and `timestamp` in-window, match `timestamp` to a cloud `clockEpoch` within `kReconcileToleranceSec = 2`, strict 1:1 (skip if a candidate uuid is already on any row or already consumed this pass; skip ambiguous). Write `visualizer_id`/`visualizer_url` for matched rows (reuse the `requestUpdateVisualizerInfo` UPDATE SQL). Return the list of linked `{dbShotId, visualizerId}`.
- [x] 3.3 Self-correct on relink (unconditional): in `MainController`, for each linked row returned by 3.2, dispatch `m_visualizer->updateShotOnVisualizer(visualizerId, projection)` (load the row via `requestShot`, same serial pattern as the migration-16 drain) so the now-authoritative local rating (cleared → JSON `null`, per #1155) reaches the cloud. Unconditional per linked row — the list API does not return the cloud rating and the PATCH is idempotent over the bounded orphan set; do not add a per-shot GET just to compare. Serial dispatch; do not hammer the API.
- [x] 3.4 Gating + run-once: add QSettings key `visualizerBackfill/doneV1`. Trigger `reconcileVisualizerLinks` from `MainController` on the existing post-boot Visualizer-ready hook (the same path the migration-16 drain uses — no new timer guard). Skip if flag set; skip WITHOUT setting the flag if credentials absent; set the flag only after a fully completed pass (network/parse error → leave unset for retry; already-written links are idempotent).
- [x] 3.5 Bound the window: define `kReconcileWindowDays = 60` constant; only consider local rows newer than `now - window`. Add a code comment that widening the window later requires bumping the run-once key to `doneV2`.

## 4. Tests

- [ ] 4.1 `tests/` (visualizer uploader or maincontroller harness): successful upload with NO QML page present persists `visualizer_id` to the correct DB row via the C++ connection. Assert wrong-row isolation (history re-upload of row M while `m_lastSavedShotId == L` writes M, not L).
- [x] 4.2 Reconciliation unit tests — `tst_dbmigration.cpp`: `reconcileVisualizerLinks_matchingContract` (in-window match / already-linked-not-reused / out-of-window / ambiguous / id-already-used / idempotent / `ok` return), `reconcileVisualizerLinks_toleranceBoundary` (±2 s inclusive both sides, +3 s excluded — the load-bearing skew line), `reconcileVisualizerLinks_multiRowAndEmpty` (the real many-orphan production scenario: distinct rows claim distinct ids, cross-row no-reuse, mid-pass ambiguous skipped; plus empty-cloud-list successful no-op). Static signature now returns `bool ok` so SQL-failure is distinguishable from "0 matched".
- [x] 4.3 Reconciliation idempotency — covered (re-run → no-op, still `ok`). The DB-failure → `ok=false` → run-once flag NOT set path is covered end-to-end by the review fix (C-2): `reconcileVisualizerLinksStatic` returns false on seed/row SELECT failure, the threaded wrapper ANDs `withTempDb` open success, `MainController` only sets `visualizerBackfill/doneV1` when `ok`. Credential-skip / boot wiring still inspection + manual §6.3 (no `tst_maincontroller` harness — same limitation as #1155 5.0c).
- [ ] 4.4 DEFERRED (no MainController/uploader signal harness in the suite). The self-correct path reuses the already-shipped, #1155-reviewed migration-16 serial drain (`dispatchNextPendingVisualizerSync` → `updateShotOnVisualizer`, cleared → JSON `null`); covered by inspection + manual device check §6.2. Add a focused harness in a follow-up if one is introduced.
- [ ] 4.6 FOLLOW-UP (not blocking): extract the pure paging stop predicate from `fetchShotListPage` into a `shouldStopPaging(...)`-style helper so the newest-first-sort early-stop and the ceiling/exhaustion logic become unit-testable without a network harness. The ceiling-hit case is now fail-safe (emits `shotListFailed`, see review fix H-1) so the residual risk is bounded; this is a testability improvement, deferred to a follow-up change.
- [x] 4.5 Build via Qt Creator MCP (`mcp__qtcreator__build`), run full suite (`mcp__qtcreator__run_tests`), resolve any failures. Confirm no new WARN-on-expected-path lines (TESTING.md).

## 5. Docs

- [x] 5.1 Update `docs/CLAUDE_MD/VISUALIZER.md`: document the authoritative C++ writeback path (MainController ← `uploadSucceededForShot` → `requestUpdateVisualizerInfo`) and the one-time reconciliation (matching key, window, run-once flag, self-correct-on-relink, independence from migration-16).

## 6. Manual verification (device)

- [ ] 6.1 On the affected tablet, with a build containing this change: confirm a fresh shot pulled with the post-shot review page disabled gets `visualizer_id` recorded (re-query via `shots_get_detail`).
- [x] 6.2 Confirm the reconciliation relinks the known orphans (DB shots ~901–923) to their cloud copies, and that their stranded `espresso_enjoyment=75` on visualizer.coffee is corrected to unrated (the user's `defaultShotRating` is 0). Spot-check shot 923 ↔ `e81fcb49-f9d1-4f7c-aaf1-bf07c19f2f26`.
- [ ] 6.3 Reboot the app; confirm the reconciliation does not run again (run-once flag honored, no Visualizer list call in the debug log).

## 7. Release / wrap-up

- [x] 7.1 Commit (`fix: persist Visualizer shot id from controller + one-time reconciliation backfill`), open PR referencing this OpenSpec change and the writeback root cause (shot 923 / `e81fcb49`).
- [x] 7.2 Run the automated PR review (`/pr-review-toolkit:review-pr`) between open-PR and merge.
- [x] 7.3 Squash-merge + delete branch (`/merge-pr`).
- [x] 7.4 Archive this OpenSpec change after merge.
- [x] 7.5 Sequencing note for the release: ship this BEFORE relying on the migration-16 inferred-rating back-sync on-device — reconciliation (Component B) is the authoritative repair for the orphaned cohort and is order-independent, but landing it first means the device update that carries #1155's migration also carries the relink/​self-correct so the stranded 75s are actually fixed.
