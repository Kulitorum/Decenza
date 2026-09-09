## Context

See proposal.md for motivation. PR #1930 supplied registered owners, shared C++/QML formatting, contextual persistence and an AI diagnostic operation lifecycle. This change extends the outcome discipline to Visualizer, where several separate asynchronous state machines share one owner. The initial source findings and inherited holds are in `source-audit.md` and `validation-holds.md`.

The current network/result paths remain authoritative: uploads have an existing bounded retry budget, history recovery has batch counters, and profile duplicate handling delegates to `ProfileSaveHelper`. Some result signals synchronously invoke consumers. `m_uploading` is UI state rather than a reliable per-request identity.

## Goals / Non-Goals

**Goals:** Attach diagnostic context to the operation that owns a callback, finish at its usable-result boundary, and make its INFO/WARN view coherent. Reuse shared formatting and keep new context independent of mutable UI state.

**Non-Goals:** Change request selection, concurrency rules, retry policy, parsing acceptance, upload persistence, duplicate handling, service capabilities or charging behavior. No global severity promotion based on words such as "error". No new log store or MCP surface. The other app-wide candidates remain explicitly recorded for later passes; MQTT is excluded by the user.

## Decisions

### Keep the published owner and use consistent emitter tags

All Visualizer integration outcomes remain under `[Visualizer]`, with `VisualizerUploader`, `VisualizerImporter` and `MainController` as the appropriate emitter tags. The operation kind carries distinctions such as upload, update, connectionTest, shotList, profileImport, recovery and coffeeSync; a new marker per stage would fragment the existing subsystem query. Shared profile/storage helpers keep their own owner for their distinct work and do not re-emit the Visualizer terminal result.

### Capture operation context at entry and carry it in callbacks

Use a small diagnostic context with an opaque UUID, monotonic elapsed clock, kind, stage, known identifiers, optional parent operation and finish-once state. Capture it in reply callbacks; carry the same context into retries/pages. Snapshot identifiers before emitting result signals so a reentrant request cannot change the event that is being recorded. A rejected entry receives its own context and cannot terminate an active request.

Reuse the existing registry prefix and factor shared bounded-field/URL formatting from the AI diagnostic helper into a neutral utility if needed. Do not copy formatter bodies or make Visualizer depend on AI provider state. A general event bus or a new asynchronous logging queue is unnecessary; the existing message handler already persists and publishes records.

### Finish each action at the boundary its existing consumer uses

The uploader finishes after the existing returned-ID check; updates and connection tests follow their existing result branches. Lists finish after existing validation and pagination. Profile imports finish after save or duplicate resolution, including cancel. Recovery reports its final counters, using partial failure when any item failed. Begin and terminal events are INFO/WARN; attempt/page/parser detail is DEBUG.

Post-upload coffee management has a separate child context so its failure cannot rewrite a successful upload. Existing optional-account restrictions become an explicit INFO limitation; request and interpretation failures are WARN. A queued retry describes pending work and a subsequent pass receives a new attempt identity linked through the same known record IDs. Intentional background non-starts remain DEBUG; an explicit user request refused before dispatch gets a terminal rejection.

Use one helper to format the common terminal fields. Reuse existing counters rather than introduce timers or infer completion from a quiet interval. Per-item failures and a batch summary are different events; give the former item identity and the latter totals so they cannot look like repeated reports of one failure.

### Keep diagnostic data separate from server payloads and UI wording

Use locally controlled reasons such as missingCredentials, network, invalidResponse, pageLimit, saveFailed, capabilityUnavailable and pendingRetry, plus HTTP/network status, item counts and bounded IDs. Preserve user-facing translated errors and dedicated debug-file behavior. Main-log response/request dumps, parse snippets and raw `errorString()` prose are replaced at source; truncating a payload is not sanitization. Logged URLs omit userinfo, query and fragment, including share-code query values.

### Validate result paths rather than only formatter output

Inventory every covered entry, terminal branch and forwarding consumer before editing. Extend test targets that already link the needed production components and use fake `QNetworkAccessManager`/reply delivery or local fixtures. Exercise a real failure followed by success, response interpretation, missing credentials, page-limit failure, duplicate cancellation, partial recovery, post-upload sync failure and reentrant/late callbacks. Assert severity, identity, one terminal event, omitted sensitive content, signal results and request counts. Do not replace this with tests that only construct an expected log string.

Build and run focused tests and the required full suite through Qt Creator MCP on Mac. Runtime checks, if needed, must first establish that no second Decenza instance is running and target this checkout explicitly. Compare an unfiltered persisted log with its Visualizer INFO/WARN selection using a controlled local validation path; source audit covers dormant branches. No live account mutation or paid request is needed.

## Risks / Trade-offs

- Mutable uploader state and synchronous consumers can attach the wrong shot to a result → keep callback-owned context and test reentry with differing IDs; do not alter routing as a logging workaround.
- Import duplicate decisions can outlive a network reply → retain context through the existing save helper decision and close it at save/cancel, not download completion.
- Promoting every retry creates warning noise → keep intermediate attempts DEBUG, terminal failures WARN, and preserve existing repeat collapsing for recurring sources.
- A helper-only test misses silent signal exits → require a branch/consumer inventory and tests driving real result paths.
- Mac results cannot certify native mobile code from #1930 → keep inherited beta and updated-device checks unchecked in `validation-holds.md` until the user lifts the hold and evidence exists.

## Migration Plan

No data migration or rollout flag is needed. Historical logs stay readable and `[Visualizer]` keeps its meaning. Implement on this follow-up branch, record Mac evidence, review the bounded logging-only diff, and open the follow-up PR when ready. Before a requested merge, reconcile tasks, run `openspec archive improve-visualizer-operation-logging --yes`, commit the archive last when possible, and read checks on that head. Held validation remains visibly outstanding. A source revert rolls back diagnostic changes without undoing application data. Publish the short wiki update with the shipped feature; beta builds remain on hold until the user resumes them.
