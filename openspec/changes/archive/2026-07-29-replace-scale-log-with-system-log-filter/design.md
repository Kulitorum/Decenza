## Context

The connections page has two log windows, each fed by a private channel rather than the log we collect from users.

**Scale** — `BLEManager::appendScaleLog()` appends to `m_scaleLogMessages` (1000-entry ring), emits `scaleLogMessage`, writes `scale_debug_log.txt`, and optionally mirrors to stderr. About 60 call sites in `blemanager.cpp`, plus forwarders from the drivers, transports, refractometers and USB. After #1700 and #1703 those forwarders are record-only and every source carries a `[Scale]` marker and writes its own stderr line — so the ring is now a pure duplicate of what the system log already holds.

**DE1** — `de1LogMessage` is a bare signal. No ring, no file, and its 17 emitters in `blemanager.cpp` write nowhere else, so the permission/scan/direct-wake narrative has never been in a submitted log. DE1 stderr logging separately spans `[BLE DE1]` (10), `[USB]` (62), `[DE1]` (1) and ~33 unprefixed lines, with helpers only in `BleTransport`.

Available already: `WebDebugLogger` (in-memory ring + persisted `debug.log`, session-boundary parsing, chunked reads) and `McpLogFilter` (substring/regex, `minLevel`, dedupe, pagination). `WebDebugLogger` has **no signals and no QML exposure**, which is the only reason a private channel was needed to drive a window.

Verified prerequisites: `qInfo` is not filtered anywhere (no `setFilterRules`, no `QT_LOGGING_RULES`, no `QT_NO_INFO_OUTPUT`), `AsyncLogger` maps `QtInfoMsg`, and `WebDebugLogger` already writes an `INFO` level tag.

## Goals / Non-Goals

**Goals:**
- One store. Every device event recorded once, to the system log.
- Both windows preserved as filtered views, each showing its subsystem's user-facing narrative for the current session.
- DE1 logging centralized behind helpers with a `[DE1]` marker, matching what the scale side now does.
- The DE1 narrative becomes recoverable from a submitted log for the first time.
- Severity carries audience, so one convention drives both the views and MCP `minLevel`.
- A reusable convention, not two ad-hoc markers: the next subsystem registers a token and inherits the view and query behaviour instead of inventing a fifth prefix family.

**Non-Goals:**
- No new MCP parameter. `filter` + `minLevel` already express these queries; only the description changes.
- Not a log-transport rewrite. `WebDebugLogger`/`AsyncLogger` internals, the 2 MB rotation and the persisted format stay as they are.
- Not retrofitting non-device subsystems (`[Profile]`, shot logging, ShotServer) onto the convention. This change *defines* the convention and documents it as the blueprint, but only converts the two subsystems a view depends on — a sweep of everything else would bury the part that has to be right.
- No new UI. Same two windows, same page, same share button — different source.
- Not adding a UI severity filter. The views show INFO+; a developer wanting DEBUG uses the log or MCP.

## Decisions

### Severity is the audience axis, not a second prefix token

Views show marker + `minLevel` INFO. The alternative — an explicit audience token like `[Scale][user]` — was rejected: it adds a prefix family, which is what #1703 spent a PR removing, and it leaves severity orthogonal so a user-facing warning needs both dimensions set correctly. Severity already exists on every line, already drives `minLevel`, and makes the view and the MCP query the same query by construction.

Consequence: helpers need a third tier. `SCALE_INFO_TAGGED` / `SCALE_INFO` alongside the existing `*_LOG` (qDebug) and `*_WARN` (qWarning), and the same for DE1.

Tier assignment is the substance of this change, not a mechanical pass. The rule is **audience, not authorship**: a driver logs INFO when its event is part of the user-facing narrative, and `BLEManager` logs DEBUG when the detail only helps a developer.

### The two windows converge on the DE1 window's character

Today's scale window carries driver and transport debug lines; the DE1 window is a curated 17. Both become curated. This is a deliberate, user-visible reduction in the scale window — driver detail stays in the log and is one `minLevel: DEBUG` request away, but leaves the panel.

### Views read the current session from the in-memory ring, not the persisted file

`WebDebugLogger` already holds the session's lines in memory. Reading the ring avoids parsing `debug.log` and its session markers on every page build, and the ring's cap is not a practical limit for a session's INFO-tier device lines.

The ring is capped, so a very long session with heavy DEBUG traffic could evict early INFO lines. Accepted: the old windows had a hard 1000-entry cap and no persistence at all, so this is not a regression. If it bites, the fallback is a session-scoped read of the persisted file, which `getPersistedLogChunk` + `SessionBoundary` already support.

### `WebDebugLogger` gains a signal and compile-time QML exposure

Add a `lineAppended(QtMsgType, QString)` signal and a session-scoped filtered accessor. Expose to QML via the macro-in-header route — never `setContextProperty` or a runtime `qmlRegisterType`, per `QML_GOTCHAS.md`, both of which are invisible to qmllint and indistinguishable from a typo.

Emission happens on the logging path, which is called from any thread. `WebDebugLogger::handleMessage` already takes a mutex; the signal must be emitted so delivery to the QML view is queued, and must not be emitted while holding the lock (a slot that logs would deadlock). Snapshot under the lock, emit after release.

**A view slot must not log.** Any `qDebug` reachable from the append handler re-enters the logger — the cycle is the hazard here, not the cost.

### Filtering lives in one place, shared by the view and MCP

The view's filter is expressed with `McpLogFilter` (marker substring + `minLevel`), not a second hand-rolled predicate in QML or a view model. Two copies of "what counts as a scale line" would drift, which is the failure mode this whole change is reacting to. The name `McpLogFilter` becomes inaccurate once a non-MCP caller uses it; renaming is noted as a follow-up rather than done here, to keep the diff readable.

### One registry; the description, docs and check all derive from it

The marker set lives in a single header — token plus a one-line description of what each subsystem covers. The MCP `debug_get_log` description is **built** from it at tool-registration time, the enforcement check parses it, and `docs/CLAUDE_MD/LOGGING.md` points at it rather than restating the list.

The alternative — writing the markers into the tool description, the doc and the check independently — is three copies of a list, which is precisely the failure this change is reacting to and which the CLAUDE.md centralization rule now forbids. A marker added without the description following it is invisible to the AI that would use it; a marker renamed without the check following it silently stops being enforced.

The registry is code, not a data file, so the helpers can reference the same constants they stamp onto lines and a typo is a compile error rather than a silent mismatch.

### Enforcement is a build-free source check, not review

Two failure modes are invisible at runtime and to the compiler: a helper that omits its marker, and a call site that hand-rolls a prefix instead of calling a helper. Both produce lines that are simply absent from a subsystem query, and review has already missed this class more than once.

The check therefore belongs in the existing build-free PR gate (`text-invariants.yml` — pure Python over the source, no Qt, no compile, which is why it can afford to run per-PR when nothing else does), as `scripts/check_log_markers.py`. It fails rather than warns.

Scope it to the subsystems the registry covers. A repo-wide ban on bracketed prefixes would fire on unrelated logging and get suppressed, which is worse than not having the check.

### Clear becomes view-local; share retargets to the system log

Clear sets a view-local floor (hide everything currently shown, keep following). It must not touch the shared log — a device panel deleting the diagnostic store is not behaviour worth carrying. Share reuses the existing platform share plumbing from `shareScaleLog()`, retargeted at `WebDebugLogger::logFilePath()`.

### Removal is a hard cut, not a deprecation

`appendScaleLog` and `de1LogMessage` are internal; there is no external consumer to migrate and no persistence format to keep compatible. Leaving them as thin wrappers would preserve exactly the ambiguity ("which one should I call?") the change exists to remove. The compiler finds every call site.

## Risks / Trade-offs

- **Tier assignment is silent in both directions** → A user-facing line left at DEBUG vanishes from its window; driver chatter promoted to INFO restores the noise. Mitigation: assign tiers per file with the audience rule stated above, and review the resulting INFO set as a whole — read the INFO-only view for a full connect/shot/disconnect cycle and judge it as a narrative, rather than checking sites in isolation.
- **`QTest::failOnWarning()`** → Several suites enable it, so any site that newly logs WARN will fail tests that traverse it. Mitigation: the pass must not *promote* into WARN; INFO is unaffected. Existing WARN sites must not be demoted either — they are findable today via `minLevel: WARN`.
- **The 17 DE1 lines are new to the log** → They add volume where there was none. Mitigation: they are INFO, they are per-event not per-poll, and their absence is the defect being fixed.
- **Signal-on-log-path re-entrancy** → A slot that logs, or emission under the mutex, deadlocks or recurses. Mitigation: emit after releasing the lock, deliver queued, and keep the view slot free of logging. This is the one genuinely dangerous mechanism in the change.
- **QML views are manual-test-only** → No automated coverage for the windows themselves. Mitigation: put the accessor/filter behaviour in `tst_webdebuglogger`, and guard the marker contract with a source-level check (a `scripts/check_*.py` in the build-free `text-invariants.yml` gate is the right shape) rather than trusting review to catch a new helper that forgets the marker.
- **`scale_debug_log.txt` disappears** → Anyone still asking users for it gets nothing. Mitigation: audit the wiki manual and support wording as a task, per the CLAUDE.md rule that user-visible changes update the manual in the same change.
- **The convention could become a dead letter** → Documented conventions that nothing enforces decay; this codebase has examples. Mitigation: the source check is the load-bearing part, not the doc. If the check is dropped, treat the convention as aspirational.
- **Diff size** → ~100 call sites across 7 files. Mitigation: sequence as marker/helper introduction, then per-file tier assignment, then store removal, then the views — each independently buildable, so a bisect lands on one file.

## Migration Plan

1. Land after PR #1703 (the `[Scale]` marker), which this depends on.
2. Add the marker registry and the INFO tier to the scale helpers; assign scale tiers per file. Behaviour-neutral for the existing window (it still reads the old channel).
3. Introduce DE1 helpers and the `[DE1]` marker; assign DE1 tiers; convert the 17 signal-only sites to real log calls, still emitting the signal.
4. Add the `WebDebugLogger` signal, accessor and QML exposure.
5. Re-source both views; then delete the two private channels and `scale_debug_log.txt` in the same step as the last reader goes away.
6. Derive the `debug_get_log` description from the registry, write `docs/CLAUDE_MD/LOGGING.md` and list it in the CLAUDE.md table, add `scripts/check_log_markers.py` to the PR gate, and update the wiki manual.

Rollback: steps 2-3 are independently valuable and safe to keep even if 5 is reverted, because they only change where lines go and at what severity. Step 5 is the irreversible one and is a single commit.

## Open Questions

- **Does the scale view need a "show detail" affordance?** The convergence hides driver lines that a power user may have relied on. Assumption: no — the log and MCP cover it. Worth revisiting if it turns out users were reading frame-level lines off that panel to diagnose their own scales.
- **Should the DE1 view show DE1-over-USB lines?** `[DE1][USB]` is DE1 discovery, so filtering on `[DE1]` includes it. Assumed correct — the window is about reaching the machine, whatever the transport.
- **Is `[Scale]` right for refractometers?** They are grouped under it today. Left alone here; a `[Refractometer]` marker would be a separate decision with its own view question.
