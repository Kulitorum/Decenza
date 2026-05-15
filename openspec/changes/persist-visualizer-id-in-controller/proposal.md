## Why

Auto-upload to visualizer.coffee at shot end works and succeeds server-side, but the returned Visualizer shot UUID is frequently never written back to the local `shots.visualizer_id` column. Confirmed on a live device (app v1.7.5 build 3381, tablet SM-X210): DB shot 923 uploaded successfully — debug log line `Visualizer: Upload successful, ID: "e81fcb49-f9d1-4f7c-aaf1-bf07c19f2f26"`, and `https://visualizer.coffee/shots/e81fcb49-f9d1-4f7c-aaf1-bf07c19f2f26` returns HTTP 200 — yet `shots_get_detail(923)` reports `visualizerId: "", hasVisualizerUpload: false`. DB shots 901–923 are all in this state; shot 900 (May 10) was recorded. Not a credentials/config problem: auto-upload is on and working.

**Root cause:** the only code that persists the Visualizer ID to the DB is the QML `onUploadSuccess` handler in `PostShotReviewPage.qml:332-338` (and a twin in `ShotDetailPage.qml:137`), both gated on a transient page-level shot id. There is no C++ writeback path: `VisualizerUploader::uploadSuccess` is emitted (`visualizeruploader.cpp:436`) but nothing in `MainController` connects it to `ShotHistoryStorage::requestUpdateVisualizerInfo`. Auto-upload is launched by `MainController` at shot end; the HTTP round-trip resolves ~1 s later. The writeback only lands if a review/detail page is still alive with its shot id set at that moment, so it silently fails whenever `visualizer/showAfterShot == false`, the post-shot review auto-closes, or the user navigates away before the round-trip finishes.

**Urgency / interaction:** the just-merged migration-16 back-sync (change `remove-inferred-shot-ratings`, PR #1155) corrects bogus inferred-75 ratings on visualizer.coffee only for rows where `visualizer_id != ''`. Because this bug leaves uploaded shots with an empty `visualizer_id`, those stranded 75s on the cloud are unreachable by that back-sync. Fixing the writeback going forward is necessary but not sufficient — the already-orphaned uploads also need to be relinked.

## What Changes

Two components in one change:

**A. Authoritative C++ writeback (go-forward fix).**
- Thread the originating DB shot id through `VisualizerUploader` so a successful upload reports *which local shot* it was for.
- `MainController` owns the persistence: connect the uploader's success signal to `ShotHistoryStorage::requestUpdateVisualizerInfo(dbShotId, visualizerId, url)`, independent of any UI page.
- The QML page handlers become redundant for persistence; they are removed (or reduced to UI refresh only) so there is exactly one authoritative writeback path.
- No timers / delays to "wait out" the race (per CLAUDE.md no-timers-as-guards) — correctness comes from explicit id correlation, not timing.

**B. One-time reconciliation backfill (recover existing orphans, self-correcting).**
- A bounded, idempotent, run-once pass that lists the user's shots from the Visualizer API and back-fills `visualizer_id` / `visualizer_url` for local `shots` rows that were uploaded but never recorded.
- Matching key: local `shots.timestamp` (shot epoch, which Decenza sends as the Visualizer `clock`/start time) correlated to the Visualizer shot's start time within a small tolerance; only rows with empty `visualizer_id` are eligible, and a Visualizer shot already linked to some local row is never reused.
- **Self-correcting on relink:** for each row it links, if the local `enjoyment` (the corrected value — 0/default after migration 16 ran, or the user's real rating) differs from what the cloud copy carries, the reconciliation immediately PATCHes the local value up via the existing `updateShotOnVisualizer` path (which already sends JSON `null` to clear a 0 rating). This makes Component B fully repair the stranded inferred-75 ratings on visualizer.coffee **on its own**, with no dependency on migration 16's queue.
- Gated by a QSettings run-once flag and by Visualizer credentials being present; bounded to a recent window so it cannot sweep the user's entire cloud history. Skips silently when credentials are absent (retries on a later boot once configured).
- Runs off the main thread (network + DB write), same threading discipline as the rest of `ShotHistoryStorage`.

**Sequencing (why B is order-independent of the migration-16 back-sync).**
Migration 16 (shipped in #1155) runs inside `ShotHistoryStorage::initialize()` at DB-init — very early in boot — and builds its `migration16/pendingVisualizerSync` queue by selecting rows that *already* have a non-empty `visualizer_id`. The reconciliation needs network/credentials and runs later in boot, so within any single boot migration 16's queue-build can never observe rows that reconciliation later links — and reworking the already-shipped schema migration is out of scope. Making Component B self-correcting (push corrected local rating to the cloud at relink time) removes the ordering dependency entirely: the orphaned-and-stranded cohort is repaired by reconciliation alone, whether or not migration 16's back-sync ever fires for them. The two mechanisms are complementary and safe to run in any order; reconciliation is the authoritative repair for shots the writeback bug orphaned.

## Capabilities

### New Capabilities
- `visualizer-upload-persistence`: defines (1) the contract that a successful Visualizer upload SHALL persist its returned shot id/url to the originating local shot row via a non-UI path, and (2) a one-time bounded reconciliation that relinks already-uploaded-but-unrecorded shots.

### Modified Capabilities

_None — no existing spec governs Visualizer upload persistence; this is net-new behaviour. (The `remove-inferred-shot-ratings` migration-16 back-sync is referenced for motivation only and is not modified here.)_

## Impact

- **Code:**
  - `src/network/visualizeruploader.{h,cpp}` — carry the originating DB shot id per upload (`uploadShot`, `uploadShotFromHistory`, `uploadShotFromHistoryWithOverrides`); emit it on success (new signal `uploadSucceededForShot(qint64 dbShotId, QString visualizerId, QString url)` or an added parameter — design decides). New method/path for the reconciliation list call (`GET /api/shots` with paging; auth already implemented for `testConnection`).
  - `src/controllers/maincontroller.cpp` — connect the uploader success signal to `m_shotHistory->requestUpdateVisualizerInfo(...)`; trigger the one-time reconciliation during boot once `ShotHistoryStorage` is ready and credentials exist (no timer guard — hook the existing readiness path).
  - `src/history/shothistorystorage.{h,cpp}` — a static/background helper to apply a batch of (dbShotId → visualizerId/url) links, reusing `withTempDb` and the existing `requestUpdateVisualizerInfo` SQL.
  - `qml/pages/PostShotReviewPage.qml:332-338`, `qml/pages/ShotDetailPage.qml:137` — remove the persistence call from `onUploadSuccess`; keep any pure UI-refresh behaviour.
- **Settings:** one new QSettings run-once key (e.g. `visualizerBackfill/doneV1`); not a user-facing setting.
- **Docs:** `docs/CLAUDE_MD/VISUALIZER.md` — document the authoritative writeback path and the one-time reconciliation.
- **Tests:** regression test that a successful upload persists `visualizer_id` to the correct row with no UI page involved; reconciliation test (timestamp match links the right row, idempotent re-run is a no-op, already-linked Visualizer shots not reused, empty-credentials path is a safe skip).
- **Risk / not in scope:** matching is heuristic (timestamp ± tolerance) since Decenza and Visualizer share no stable key; the bounded window and "only empty `visualizer_id`, never reuse a linked cloud shot" rules contain mis-link blast radius. Re-deriving or changing the migration-16 inferred-rating back-sync is explicitly out of scope; this change only makes `visualizer_id` reliably populated so that (and future features) can function.
