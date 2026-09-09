## Context

See proposal.md for the problem. The existing text format, subsystem registry, macro helpers, repeat collapsing and persisted log remain the foundation. The logger's handler currently receives `QMessageLogContext` but passes only severity and text into persistence. The marker gate covers selected C++ files and marker literals in QML; headers, many mixed C++ files and platform emitters can escape it.

The evidence census covers all 5,240 timestamped lines fetched through DE1 MCP for 6 September 09:05 through 9 September 09:05, across builds 3585–3587. It classifies formatting rather than certifying the absence of functional bugs. Of 3,039 lines outside the registered-marker grammar, 683 are BatteryManager lines, 316 use the unregistered Memory prefix, 1,046 are bare FD rows, and three are framework warnings. A current-source inventory is required because the capture spans older builds and contains no evidence about dormant branches.

## Goals / Non-Goals

**Goals:** a user or assistant can retrieve a complete subsystem story from the existing log; a failed bag action can be diagnosed without accessing separate AI files; framework warnings retain all context that was supplied; future unformatted first-party calls fail the existing source gate.

**Non-Goals:** redesigning the log transport, adding structured event storage or a second buffer, changing retained history, fixing the underlying bag/UI/network behaviors, or enabling paid provider calls during validation. Logging cannot manufacture a missing Qt stack or prove that an unlogged fault never happened.

## Decisions

### 1. Convert emitters by diagnostic ownership

Build a temporary source inventory of runtime emitters across `src/**/*.{cpp,h,mm,m}`, `qml/**/*.qml` and first-party platform bridges. Include raw Qt calls, category-based calls, QML console calls, direct stderr/native logging, multiline messages and logging hidden in helpers. Check each capture family against the current implementation. Record intentional exceptions with a call-site reason; generated/vendor code, test harness output and crash-signal-safe writes are outside normal runtime-helper enforcement.

Use this routing for the observed families and their source siblings:

| Diagnostic question / emitters | Destination |
|---|---|
| Tablet charge policy and observed power: BatteryManager | New `[Battery]` |
| Memory samples, object deltas and FD census | Register existing spelling `[Memory]`; tags distinguish Memory, Objects, FDs |
| Advisor/provider work and saved AI conversations outside bag operations | New `[AI]` |
| Bag page search, local fetch, archive lookup, provider extraction and parse/apply results | Existing `[BeanBase]`, including AI stages |
| Shot start/end, frame exits, timers, extraction display | New `[Shot]`; retain `[SAW]` for weight-stop decisions and `[Calibration]` for calibration |
| Steam sessions, pitcher capture and steam health | New `[Steam]` |
| History reads/writes, missing records, backup and restore | Existing `[Storage]`, expanding its catalog description to name reads and backup outcomes |
| Profile loading/saving and ProfileStorage | New `[Profiles]` |
| Recipe activation, overrides and start refusals | New `[Recipes]`; scheduled reload remains `[AutoLoad]` |
| Visualizer upload and coffee-management sync | New `[Visualizer]` |
| ShotServer/MQTT transport lifecycle | Existing `[Network]` with emitter tags and updated description; protocol decisions remain `[MCP]` |
| Startup, settings, location, widget library and UI navigation | Existing `[App]` with descriptive tags; a bag UI error still belongs to `[BeanBase]` |
| Auto-wake, firmware, screensaver and other already-owned activity | Existing matching marker |
| Framework or otherwise unattributed diagnostics | New `[Runtime]`, retaining available origin information without guessing ownership |

Register each new marker once with a question-oriented description. Do not turn each class into a subsystem or prefix all remaining messages with App to reach a coverage count.

C++ helpers continue to alias the common formatter. Add a small QML-facing tier API to the existing compile-time logging singleton, sharing registry identifiers and the same formatting implementation; call sites provide an emitter tag, not a bracketed marker string. Preserve a single log event when a current C++ emit already feeds a QML consumer. Native bridges use the same format where safe; crash-handler low-level writes retain their constraints.

**Alternative considered:** heuristic message-prefix rewriting at ingestion. Rejected for first-party conversion: it masks unmigrated code and assigns ownership from text. A runtime fallback is for genuinely unattributed incoming messages, not a substitute for source coverage.

### 2. Preserve context and multiline membership at the capture boundary

Pass the supplied message context into the persisted formatter. Retain category and file/line/function for WARN+ when available, with repository/QML-relative paths where possible. Framework output without a registered marker receives `[Runtime]`; its original text and severity remain intact. A missing context field stays absent or explicitly unknown. Do not attach the active page to a warning as though it proves the page emitted it.

Split multiline messages once at capture. Every physical continuation receives the same elapsed-time/severity and subsystem/source prefix; content stays in order and session-looking text remains ordinary prefixed content. FD dump rows gain an owner, dump identifier and reason so pagination or concurrent activity cannot detach rows from their header. Keep raw FD detail at DEBUG.

Keep one persisted representation for MCP and the web/device log readers. Preserve the existing handler chain, thread-local recursion guard, locking and teardown lifetime. The formatter must not log, perform network/disk lookups, or query UI objects on the emitting thread. This work does not rearrange logging I/O or add per-sample work beyond formatting.

**Alternative considered:** JSON lines and a new log reader. Rejected: the existing text readers already supply the required filtering, and changing the transport would enlarge the migration without resolving missing outcomes.

### 3. Record AI operation outcomes where the outcome is known

Capture a session-unique request identifier, operation kind, relevant bag/shot identifier, initial provider/model selection and start time when a user operation enters. Reuse existing URL/request tokens for matching callbacks but do not print tokens that contain sensitive data. Carry the diagnostic context through existing callbacks; no new queue, timer, retry policy or provider fallback.

A bag operation stays under `[BeanBase]` for every stage. General advisor work stays under `[AI]`. Use one formatter and route by operation kind; do not duplicate a terminal event under both markers. Capture selection at dispatch so changing settings cannot relabel an in-flight request.

Emit one terminal outcome after the operation's actual result is known:
- local page failure: fetched host/path, HTTP status or transport reason, archive lookup/replay stage, and that no provider was invoked;
- provider failure: selected provider/model, bounded error/status and elapsed time;
- provider response that fails parsing: parsing failure, not success because a response arrived;
- completed search/extraction/advisor response: success or empty/no-result, with field/result counts where relevant;
- rejection, cancellation or supersession: explicit result and reason, without a later callback reporting another completion.

A provider retry is an intermediate DEBUG event associated with that request, not a second terminal failure. The local fetch owner reports failures that never enter AIManager; AIManager reports provider/parse outcomes. UI and MCP callers consume those outcomes without logging duplicates. Keep detailed prompt/response files as they are, but demote file-write receipts to developer detail and never use them as the operation verdict.

For bag page failures record the archive response actually observed. An empty availability envelope is an empty lookup result, not proof that no archived copy exists. Status 429 names the responding service. Logs describe current recovery choices; this change does not alter them.

Do not add prompts, page contents, API keys, authorization headers or MCP access tokens to the main log. Format URLs without credentials/query/fragment and bound remote error text. Invalid/non-finite diagnostic values must be labeled unavailable with their reason instead of rendered as valid ranges such as `[inf, -inf]`; numerical behavior is untouched.

**Alternative considered:** exposing the existing AI files through a new MCP tool. Rejected for this change: it would leave the shared main log incomplete and add an API/privacy surface instead of fixing outcome logging.

### 4. Review severity and truth, not just punctuation

While converting an event, identify whether it is an intent, observation, outcome, expected refusal or actual failure. Preserve periodic healthy telemetry at DEBUG, put user-relevant transitions/recoveries at INFO, and expose actionable failures at WARN+. Existing repeat suppression stays in place; do not create new per-frame INFO traffic.

Concrete checks from the capture:
- update-check HTTP 504 must be visible as a failure, and eventual recovery readable without flooding unchanged checks;
- benign R2 code 2 must not look like a fault;
- a normal charge-enable transition must not be presented as proof of a one-minute power outage: distinguish commanded state from the OS observation sampled before/after it;
- recipe-start refusal names the known GHC/not-ready gate rather than the speculative phrase “active GHC?”;
- a missing shot includes the requesting operation when available and distinguishes a missing row from a failed query.

These are wording, severity and context changes. Discovering an underlying behavioral defect creates a separate follow-up rather than expanding this logging change.

### 5. Close source coverage and validate actual retrieval

Extend `scripts/check_log_markers.py` rather than adding a second marker registry/checker. Once emitters are migrated, enforce helper use for all first-party runtime sources, including headers, mixed owners, QML tiers and native bridges; exemptions identify the concrete constraint and location. Lowercase or dynamically assembled leading prefixes cannot bypass the contract. Include all newly covered paths in the existing text-invariants trigger. Read representative native and Qt source paths before making claims about context availability.

Use focused negative fixtures for a bare C++ call in a header/mixed file, a QML console bypass, an unregistered lowercase/dynamic marker, and a native bridge bypass. Valid helpers and a justified crash-safe exception must pass. Extend existing runtime tests for multiline filtering/context retention and the AI success/failure/parse distinction; avoid a new executable per subsystem.

Validate with a current-build, unfiltered MCP session and a source census. Every remaining runtime fallback event needs attribution or an explained limitation. An unscoped census of historical builds is not a pass/fail check of new code.

## Risks / Trade-offs

- Broad mechanical conversion can misassign ownership or duplicate events → implement by family, inspect callers and signal consumers, then compare current-session narratives.
- Framework release builds may provide only category or text → preserve what exists and report limits; do not promise file/line or stack availability on every platform.
- More context increases bytes and may shorten the effective retained time → keep context off hot DEBUG telemetry, preserve collapsing, compare bytes for the same exercised workflow; no retention-size increase as a workaround.
- Prefixing all multiline content changes physical line counts → verify MCP pagination, dedupe and session parsing with old/new logs and prefixed session-like text.
- Widening a textual gate can create false positives → test real call shapes and exemptions, and keep source inventory separate from claims that every logged fault has a correct severity.
- A shared logger regression affects every thread → preserve existing reentrancy/shutdown tests and validate platform builds through the prescribed Qt Creator MCP/CI workflow.

## Migration Plan

1. Capture a reproducible source/family baseline and map owners before conversion.
2. Add shared formatting/context/QML plumbing and registered owners, then migrate families in reviewable groups.
3. Close AI outcome gaps and correct the evidenced severity/wording issues; finish source enforcement.
4. Run the prescribed checks, reproduce representative actions with test providers, and inspect the complete current-build log with MCP.
5. Update logging documentation and the short manual guidance. Historical logs remain readable. Rollback reverts the logging changes without a data migration.
