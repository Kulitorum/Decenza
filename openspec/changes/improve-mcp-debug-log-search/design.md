## Context

Two MCP read tools expose log text to AI clients:

- `debug_get_log` (`src/mcp/mcpresources.cpp:317-467`) — the persisted, whole-app debug log (`WebDebugLogger`), with a `sessions=true`/`session=N` mode layered over raw `offset`/`limit` pagination.
- `shots_get_debug_log` (`src/mcp/mcptools_shots.cpp:564-`) — the per-shot debug log stored in the `shots.debug_log` column (`ShotDebugLogger`), also raw `offset`/`limit` pagination.

Both already materialize the full candidate line list before slicing (`allLines` in the app-log tool, `debugLog.split('\n')` in the shot-log tool), so filtering fits naturally ahead of the existing pagination step — no new data source is needed.

Every app-log line already carries a fixed-format level tag (`webdebuglogger.cpp:73-88`: `[%1] %2 %3` = elapsed-seconds, 5-char padded level, message), so a level filter is a string-prefix check with no schema change. The shot debug log has no equivalent level tagging (it's a raw capture of qDebug output during a shot plus BLE frame dumps) — `minLevel` is intentionally app-log-only.

The app-log session index (`sessions=true`/`session=N`) currently rescans up to 100,000 lines on every call with no cache, because `getPersistedLogChunk` has no memory of previous scans and the tool handler holds no state between calls.

## Goals / Non-Goals

**Goals:**
- Let an MCP client go straight to relevant lines (`filter`, `minLevel`, `tail`) instead of paging through everything by hand.
- Make a filtered/tailed response self-sufficient for a follow-up context request by including absolute line numbers.
- Remove the repeated full-file rescan cost of session-boundary lookups.
- Keep every change additive and backward compatible: omitting the new parameters reproduces exactly today's request/response shape.

**Non-Goals:**
- No change to what gets logged, the log line format, or file rotation/trim behavior.
- No level tagging added to the shot debug log (it isn't leveled today; retrofitting that is a separate, larger change to `ShotDebugLogger`/shot capture).
- No cross-referencing between the app log and a shot's debug log by wall-clock time (app-log lines carry elapsed-seconds-since-launch, not wall clock; bridging that is out of scope here — the existing per-session ISO timestamp plus elapsed offset is judged sufficient).
- No new resource/endpoint; this only extends the two existing tools' input schemas and output shape.

## Decisions

**1. Filter is substring-by-default, regex opt-in, applied line-by-line before pagination.**
`filter: "R2 error"` matches literally; `regex: true` compiles `filter` as a `QRegularExpression` instead. Substring-by-default matches how these tools are actually used in practice (searching for an exact phrase copied from an error message) and avoids every caller having to escape regex metacharacters for a plain phrase. Case-insensitive by default (`QRegularExpression::CaseInsensitiveOption` / `QString::contains(..., Qt::CaseInsensitive)`) since log casing is inconsistent (`"BLE"` vs `"Ble"` vs message-cased text) and an AI searching for a remembered phrase shouldn't have to match case exactly.
Alternative considered: grep-style context lines (`-C N`) around each match. Deferred — the new absolute-line-number field already lets a caller re-request an `offset` window around any hit, and building that into a single call adds response-shape complexity (nested match+context groups) for a case the two-call path already covers cheaply.

**2. `minLevel` is an ordinal threshold, not a set.**
Levels order `DEBUG < INFO < WARN < ERROR < FATAL`; `minLevel: "WARN"` returns WARN and above. A discrete set (`levels: ["WARN","ERROR"]`) was considered but rejected — the universal question is "did anything go wrong" (a threshold), and a set adds a parameter shape (array vs scalar) for a use case that doesn't need it. `minLevel` combines with `filter` (both must pass) and is app-log-only; passing it to `shots_get_debug_log` is accepted but ignored (documented, not an error — the shot log has no levels to filter on, and erroring would make a client's uniform "try both tools with the same params" pattern awkward for no benefit).

**3. `tail` is evaluated after filtering, within whatever range was already addressed (whole log / one session / one shot log).**
`tail: N` returns the last N qualifying lines of that range. It composes with `filter`/`minLevel` (tail of the matches) and with `session` (tail of that session). It is mutually exclusive with `offset` at the handler level: if both are supplied, `tail` wins and `offset` is ignored (documented) rather than erroring, since a client that always sends a default `offset: 0` alongside an ad hoc `tail` shouldn't get a hard failure.

**4. Absolute line numbers ride on the existing per-line array, not as a side-channel.**
Today's `log` field is a single newline-joined string. Adding line numbers means each returned/matching line becomes a small object (`{"line": 4032, "text": "..."}`) in a new `lines` array field. The plain `log` string field is kept as-is (still newline-joined, still the full text of what was returned) so an existing caller reading `log` sees no shape change; `lines` is additive for callers that want to follow up with a precise `offset`.

**5. Session-index caching lives in `WebDebugLogger`, keyed on file size + mtime.**
`WebDebugLogger` gains a small cache (`QList<SessionBoundary>` + the `(qint64 size, QDateTime mtime)` it was built from) and a `sessionIndex()` accessor that rebuilds only when the persisted file's size/mtime differ from the cached key. This is a conservative invalidation (any append changes size, so the cache never serves stale boundaries) that needs no new signal/notification plumbing — the tool handler simply calls the accessor instead of re-scanning. `mcpresources.cpp`'s `debug_get_log` handler is rewritten to consume this instead of its inline scan loop.
Alternative considered: invalidate incrementally as new `SESSION START` lines are written (hook into `writeToFile`). Rejected as unnecessary complexity — the size/mtime check is O(1) per call and correct by construction; incremental tracking would need to handle the existing `trimLogFile()` truncation path as a special case anyway.

## Risks / Trade-offs

- [Regex filter could be pathological (catastrophic backtracking) on attacker-controlled input] → Not attacker-controlled: `filter` comes from the MCP client the user has already granted `read` access to, same trust boundary as every other tool argument. No additional mitigation beyond what already applies to MCP input generally.
- [Case-insensitive substring search is slower than case-sensitive on very large line counts] → Bounded by the same 100,000-line / 2 MB file cap that already exists; not a new performance class.
- [`tail` silently overriding `offset` instead of erroring could surprise a caller who intended both] → Documented in the tool's input-schema description (`tail (if set) is applied after filtering and overrides offset`), consistent with the project's preference for a working default over a rejected call.
- [Session-index cache keyed on mtime could miss a change on a filesystem with coarse mtime resolution (e.g. 1s on some FAT variants)] → Not a realistic target platform for the debug log file (always app-private storage on Windows/macOS/Linux/Android/iOS with sub-second or exact mtime); accepted as a non-issue for this file's actual deployment environments.
