## Context

Decenza already supports one-way auto-upload of fresh shots to visualizer.coffee via `VisualizerUploader` and the `visualizerAutoUpload` setting. A PATCH path (`updateShotOnVisualizerWithOverrides`) exists and is already wired in `PostShotReviewPage` for the manual "re-upload" button. The gap is that this manual step is required every time a user edits metadata — notes, rating, TDS, beans — either through the review page or via the MCP API.

The feature adds a `visualizerAutoUpdate` setting and two automatic triggers: page-close on `PostShotReviewPage` and MCP shot-edit tools.

## Goals / Non-Goals

**Goals:**

- Add `visualizerAutoUpdate` boolean setting (default `true`) to `SettingsVisualizer`.
- Show an **Auto-Update Shots** toggle in `SettingsVisualizerTab`, subordinate to Auto-Upload.
- On `PostShotReviewPage` close, if changes were made and the shot has a `visualizer_id`, fire `updateShotOnVisualizerWithOverrides()` (or `uploadShotFromHistoryWithOverrides()` if no id yet and auto-upload is also on).
- On MCP shot metadata write (any field), if the shot has a `visualizer_id` and `visualizerAutoUpdate` is on, fire a PATCH.

**Non-Goals:**

- First-upload of un-uploaded shots from MCP edits (MCP never triggers a first upload; it only PATCHes existing cloud shots).
- Batch re-sync of all shots with local edits not yet on visualizer.
- Changes to the reconciliation path defined in `visualizer-upload-persistence`.
- Any change to the auto-upload-on-shot-end path.

## Decisions

### Decision: Trigger on page Component.onDestruction, not a dedicated "save" signal

**Rationale:** `PostShotReviewPage` already autosaves every field edit to the local DB incrementally. A page-close trigger (via `Component.onDestruction` or the StackView `onStatusChanged: Page.Inactive` exit) is the natural point to fire a single consolidated PATCH rather than patching per-keystroke. This matches how the existing manual re-upload button works (it calls `autosave()` then fires the request).

**Alternative considered:** Patch on every field edit (debounced). Rejected — too chatty, unnecessary API calls, and the existing autosave already deduplicates local writes.

**Chosen approach:** Track a `pendingVisualizerUpdate` boolean on `PostShotReviewPage` that is set when any field edit is saved. On page deactivation/destruction, if `pendingVisualizerUpdate` is true and the shot has a `visualizer_id`, call `updateShotOnVisualizerWithOverrides()` with the current override map; if no `visualizer_id` and auto-upload is also on, call `uploadShotFromHistoryWithOverrides()`.

### Decision: MCP trigger lives in the MCP tool handler, not MainController

**Rationale:** The MCP tool for shot-update already has access to the shot id, the fields being written, and can read the stored `visualizer_id` from the DB result. Adding a post-write call to `VisualizerUploader` directly from the tool handler keeps the trigger co-located with the mutation and avoids a new signal/slot chain.

**Alternative considered:** Emit a signal from `ShotHistoryStorage` on any shot update and connect MainController to it. Rejected — too broad (would fire on every internal shot write, not just user-facing edits) and harder to gate on `visualizerAutoUpdate`.

### Decision: Auto-Update is subordinate to Auto-Upload in the UI

The toggle for Auto-Update is disabled (grayed out) when Auto-Upload is off. This communicates the dependency clearly: if you're not uploading, updating makes no sense. The underlying setting remains independently settable via MCP for programmatic control.

### Decision: No separate "auto-update on first edit" first-upload from MCP

If the MCP edits a shot that has never been uploaded (`visualizer_id` is empty), the auto-update path skips it silently. A first upload from MCP would require credentials and a full shot payload fetch; that scope belongs to a future change. The QML page path (PostShotReviewPage close) does trigger a first upload if `visualizerAutoUpload` is also on.

## Risks / Trade-offs

- **Double-PATCH on page close + manual button**: If the user manually taps the upload button and then immediately closes the page, two PATCHes fire in close succession. Visualizer.coffee is idempotent on PATCH so this is safe, just slightly wasteful. Mitigation: clear `pendingVisualizerUpdate` when the manual upload succeeds.
- **MCP trigger fires without user awareness**: An MCP agent editing a shot will silently PATCH visualizer. This is the intended behavior, but users who share their API credentials broadly should understand this. The toggle lets them opt out.
- **No retry on failure**: Auto-triggered updates do not queue for retry. A transient network error silently drops the update. The user can still manually re-upload from `PostShotReviewPage`. Full retry/queuing is out of scope.

## Open Questions

- Should the Auto-Update toggle also trigger a PATCH when the MCP `shots_update` tool is called and there is no `visualizer_id` yet but a prior upload exists that wasn't linked? — Deferred; the reconciliation pass in `visualizer-upload-persistence` handles this.
