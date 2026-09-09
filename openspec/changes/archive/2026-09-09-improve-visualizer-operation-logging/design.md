## Context

`source-audit.md` records the complete DE1 week, its 847 normalized message patterns, useful failure contexts and the review of PR #1930. The original Visualizer lifecycle implementation exceeded the user's intent and is removed. The change keeps its directory name so the existing PR and review history remain traceable.

## Decisions

### Remove noise at its source

Delete automatic FD inventory calls and their obsolete helper; do not replace them with a summary. `FdDiagnostics::snapshot()` and MCP `debug_get_fds` remain independent and unchanged. Delete color-tracking and raw-weight traces, duplicate Steam UI phase and auto-load invocation receipts, routine settling progress and successful AI/Visualizer diagnostic-file receipts. Preserve artifacts and file-write failures.

A held scale weight is not a new liveness event. Use one constant text within the existing shot/tare `LogCollapse` episode; retain stall/resume and final feed statistics. Battery polls use mode, requested charger, discharge cycle, OS status and power source as their state signature. Five percentage points from the last emitted sample constitute progress; any state change emits immediately. Similar suppressed samples are labeled similar, not identical.

### Only sustained memory growth warrants a log

Keep the existing 60-second sampling, current/peak RSS and 24-hour history. Compare three consecutive block medians using 5-, 30- and 120-sample blocks (15, 90 and 360 samples). Require at least 5 MB total growth, with each interval contributing at least one quarter, so a single step followed by jitter does not qualify. Missing/zero readings or gaps over 90 seconds break the candidate window. The wider windows retain slower growth.

A qualifying record carries all three medians, span, current RSS and QObject count. `LogCollapse` suppresses continuing growth within five MB of the last printed median. Quiet periods discard the expired growth tally and produce no healthy/recovery summary. This is a documented diagnostic heuristic, not proof of a leak; on-demand sample history remains the evidence for deeper analysis. Ordinary QObject churn produces no periodic log.

### Keep the useful outcome, not an automatic lifecycle

PR #1930's AI context continues to capture identity, provider/model at dispatch, stage, endpoint/status and interpretation. Its only automatic emission is the terminal result. Explicit retry/error diagnostics retain context. Removing start/dispatch/response/detail writes must not alter signal delivery, request counts, cancellation or finish-once behavior.

Visualizer changes edit existing messages directly: stable emitter names, numeric HTTP/network/parse errors and bounded IDs/URLs in place of payloads or remote error prose. The main log must not repeat full bodies. Existing UI errors and dedicated files retain their roles. No new Visualizer operation class, callback plumbing or systematic summaries are introduced.

Warning capture retains category and portable file/line. Function remains a fallback when file/line is unavailable. Multiline records retain attribution on every physical line. Old unformatted logs remain readable; no inferred global owner or numeric-normalization suppression is introduced.

Connection failures use a bounded per-owner/source/message LogCollapse cache. A novel failure remains immediate even if the cache is full; recovery or a fresh user attempt flushes the prior episode's repeat count once. Non-Android WiFi hostname-resolution failures route through the existing shared sink. Forecast first/changed availability, failures and recovery share a changes-only result gate; ordinary temperature changes do not affect it. Repeating DNS discovery retains one result gate keyed by backend, outcome counts and error state; elapsed time and routine worker start/end receipts do not create novelty. Found-device notifications and real errors remain intact. Bounded retry-ramp steps retain receipts; an unchanging endless tail stays quiet.

## Validation

Use deterministic trend fixtures for plateau, jitter, isolated jump/spike, sustained fast/slow growth and missing samples. Use existing production AI tests to verify no premature operation records, one terminal result, unchanged outputs and privacy. Retain Visualizer parsing and logger regressions, including function fallback. Run builds and the full suite through Qt Creator MCP on Mac, plus source-marker and OpenSpec validation. Ask the user to launch/restart the exact checkout, then confirm only one app process before a background live inspection. Weekly replay estimates are not a substitute for a newly built device capture.

## Holds and merge

No beta builds, paid AI calls or machine commands are needed. The inherited outstanding tasks remain in `validation-holds.md`. When the user requests merge, check readiness and holds, use the OpenSpec CLI to archive, commit the archive last where possible, and read the checks on the exact PR head before merging.
