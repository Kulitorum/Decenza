# Follow-up logging audit

Baseline: merged commit `5c2f1d585bde0fd5675eae91988fa89de7208b29` (PR #1930), inspected on 2026-09-09. These are source findings, not newly observed device failures. The initial scan is not a complete branch/consumer census; task 1.1 completes that before implementation.

## First pass: Visualizer

| Location at baseline | Existing gap | Required follow-up |
| --- | --- | --- |
| `visualizeruploader.cpp`, `onUploadFinished`, lines 573–624 | Success and terminal failure use DEBUG; the missing-ID warning says the upload succeeded and prints a response snippet; retries warn while final failure can disappear from WARN. | Correlate attempts; report usable-result success/failure at INFO/WARN and keep intermediate retry detail DEBUG. |
| `visualizeruploader.cpp`, `updateShotMetadata` / `onUpdateFinished`, lines 453–505 | PATCH body is dumped; successful update is DEBUG and failure includes raw response/error text. | Keep remote/local IDs and safe status; remove main-log payloads and log the terminal boundary once. |
| `visualizeruploader.cpp`, `testConnection` / `onTestFinished` | Missing credentials and success/failure only emit result signals in these functions. | Add one backend-owned connection-test outcome; inspect listeners for duplicate diagnostics. |
| `visualizeruploader.cpp`, `fetchShotListSince` / `fetchShotListPage`, lines 654–758 | Credentials, network, parse, missing-paging and page-limit exits emit signals with no operation-owned terminal log at the source. | Make both valid empty and failed list outcomes visible, retaining page and accumulated-count context. |
| `visualizerimporter.cpp`, entry guards and `onFetchFinished` | Several guards/save failures are signal-only; success remains DEBUG. Share-code URL and up to 2,000 response bytes are logged; parse failure adds a payload snippet. | Retain identity through fetch/interpret/save, distinguish duplicate pending/cancel, and remove payload/query text. |
| `visualizerimporter.cpp`, batch import and `finishRecovery` | Batch completion/recovery counts reach result signals; recovery preflight/list failures lack an operation terminal diagnostic at their source. | Include complete/partial/empty counts and preserve item identity through all stages. |
| `visualizeruploader.cpp`, coffee management, approximately lines 2052–2635 | Read-back, linking, enrichment and queued bag-update failures frequently use DEBUG; tags alternate between `Visualizer`, `VisualizerUploader` and `visualizeruploader`. | Consistent source tags, distinct child-operation outcomes, INFO capability limitations and WARN unexpected failure/pending retry. |
| `maincontroller.cpp`, Visualizer signal consumers | Controller decisions and persistence results may already describe backend events. | Trace all consumers; preserve distinct persistence failures and avoid duplicate terminal receipts. |

## Next app-wide candidates

These are candidates for subsequent changes, not evidence that every DEBUG line
below is incorrect. Each needs the same complete result/consumer review as above.

| Source | Candidate diagnostic question |
| --- | --- |
| `locationprovider.cpp` | Which explicit location lookup failed, versus a permission the user intentionally denied or an optional background lookup? Network/geocoding errors currently use DEBUG. |
| `shotreporter.cpp` | Did a report reach the API, and was a failed location lookup optional? API/final errors currently use DEBUG. |
| `translationmanager.cpp` | Which language-update check failed or later recovered? The background update failure is DEBUG; batch drain bookkeeping may correctly remain DEBUG. |
| `shotserver_auth.cpp` | Was an explicit TOTP verification refused? Assess useful non-secret context and appropriate severity without logging codes. |
| `librarysharing.cpp` / `datamigrationclient.cpp` | Distinguish optional download-count failures, expected missing extra settings and intermediate manifest retries from terminal user-operation failures. |
| `docs/CLAUDE_MD/LOGGING.md` | The specialized Network-helper table still says app servers use hand-rolled prefixes, and the statement that all logging families stop at WARN omits the newer `DIAG_ERROR/FATAL` helpers. Correct those stale statements in this change. |

MQTT is intentionally disabled and excluded. Source-only findings do not establish
new service failures. No fresh device log, app launch, live account mutation, paid
provider call or beta build was needed for this initial audit.
