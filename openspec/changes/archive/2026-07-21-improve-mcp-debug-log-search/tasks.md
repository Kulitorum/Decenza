## 1. WebDebugLogger session-index caching

- [x] 1.1 Add a cached session-boundary index to `WebDebugLogger` (`src/network/webdebuglogger.h/.cpp`): a list of `{startLine, timestamp, lineCount}` plus the `(qint64 size, QDateTime mtime)` the cache was built from.
- [x] 1.2 Add an accessor (e.g. `sessionIndex()`) that rebuilds the cache only when the persisted log file's current size/mtime differ from the cached key, otherwise returns the cached list.
- [x] 1.3 Verify `trimLogFile()`'s truncate-and-rewrite path (which changes both size and content) is correctly picked up as a cache miss by the size/mtime check — no special-casing needed if the check is implemented as "size or mtime changed", but confirm with a test.

## 2. `debug_get_log` tool changes (`src/mcp/mcpresources.cpp`)

- [x] 2.1 Rewrite the `sessions=true`/`session=N` handling to consume `WebDebugLogger::sessionIndex()` instead of the inline full-file rescan loop.
- [x] 2.2 Add `filter` (string) and `regex` (bool) input-schema parameters; apply the filter (case-insensitive substring, or case-insensitive `QRegularExpression` when `regex: true`) to the candidate line list before offset/limit/tail.
- [x] 2.3 Add `minLevel` input-schema parameter (`DEBUG`/`INFO`/`WARN`/`ERROR`/`FATAL`); parse each line's existing level tag and filter to that level or higher, combined (AND) with `filter` when both are present.
- [x] 2.4 Add `tail` input-schema parameter (integer); when present, select the last N qualifying lines of the addressed range (whole log, or the addressed session) instead of applying `offset`. Document and implement that `tail` overrides `offset` when both are supplied.
- [x] 2.5 Add a `lines` array field to the response (`[{"line": <absolute line number>, "text": <line>}]`) alongside the existing `log` string field, for every mode (raw pagination, session-scoped, filtered, tailed).
- [x] 2.6 Update the tool's registered description/input schema text to document the new parameters and their interactions (filter+minLevel combine with AND; tail overrides offset).
- [x] 2.7 Add `dedupe` (bool) input-schema parameter. Apply consecutive-line collapsing (see `mcplogfilter.h` task 7.1) to the qualifying line list after filter/minLevel, before tail/offset/limit.
- [x] 2.8 When `dedupe: true`, add `count`/`lastLine` fields to each `lines[]` entry (via the shared `appendLogFields` helper); omit both fields entirely when `dedupe` is not set.

## 3. `shots_get_debug_log` tool changes (`src/mcp/mcptools_shots.cpp`)

- [x] 3.1 Add `filter` and `regex` input-schema parameters with the same matching semantics as `debug_get_log`, applied to the shot's debug log lines before offset/limit/tail.
- [x] 3.2 Add `tail` input-schema parameter with the same override-offset semantics as `debug_get_log`.
- [x] 3.3 Accept `minLevel` in the input schema without erroring, but document (and verify with a test) that it has no effect, since shot debug log lines are not level-tagged.
- [x] 3.4 Add the same `lines` array field (`{"line", "text"}`) to the response.
- [x] 3.5 Update the tool's registered description to document the new parameters.
- [x] 3.6 Add `dedupe` (bool) input-schema parameter with the same consecutive-collapsing semantics and `count`/`lastLine` fields as `debug_get_log`.

## 4. Shared dedupe helper (`src/mcp/mcplogfilter.h`)

- [x] 4.1 Add a `stripTimestampPrefix(line)` helper that removes a leading `[<elapsed>]` field (matching `WebDebugLogger`'s persisted line format) if present, otherwise returns the line unchanged (shot debug log lines may have no such prefix).
- [x] 4.2 Add a `dedupeConsecutive(matches)` function operating on `QList<LineMatch>`: walks the list in order, collapsing a run of consecutive entries whose `stripTimestampPrefix(text)` is equal into one entry carrying `count` (occurrences collapsed) and `lastLine` (absolute line number of the last occurrence in the run); non-consecutive matches of the same text are NOT collapsed together.
- [x] 4.3 Extend the `LineMatch` struct with `count` (default 1) and `lastLine` (default equal to `line`) fields so `paginate()` and existing call sites need no changes.

## 5. Tests

- [x] 5.1 Unit test `WebDebugLogger`'s session-index cache: same size/mtime returns the cached instance (e.g. assert no rescan via a call-count hook or timing-independent structural check); a size/mtime change triggers a rebuild that reflects new session boundaries. (`tests/tst_webdebuglogger.cpp`)
- [x] 5.2 Test `debug_get_log`: substring filter, regex filter, `minLevel` alone, `minLevel` combined with `filter`, `tail` alone, `tail` overriding `offset`, and that omitting all new parameters reproduces the exact prior response shape (regression guard against the additive-only contract in design.md). (`tests/tst_profilemanager.cpp`, `=== MCP tool: debug_get_log ===` section)
- [x] 5.3 Test `shots_get_debug_log`: substring filter, regex filter, `tail`, `tail` overriding `offset`, `minLevel` accepted-and-ignored, and the no-new-parameters regression case. (`tests/tst_mcptools_shots_debuglog.cpp`)
- [x] 5.4 Test that `lines[].line` values are correct absolute indices within the addressed range (whole log vs. a specific session vs. a shot log) across a representative filtered/tailed case. (covered by `tst_mcplogfilter.cpp`'s `filterLines_absoluteLineNumbersHonorStartLine` plus the session-scoped/shot-scoped tool tests above — this is also what caught a real off-by-`sessStart` bug in the session-mode handler, fixed in `mcpresources.cpp`.)
- [x] 5.5 Unit test `dedupeConsecutive()` and `stripTimestampPrefix()` directly (`tst_mcplogfilter.cpp`): a consecutive run collapses to one entry with the right `count`/`lastLine`; a non-consecutive repeat (matching text separated by a different line) stays as two entries; lines with no `[<elapsed>]` prefix (shot-log style) still dedupe on full-text equality; a genuinely-different-payload case (different shot ids in an otherwise identical template) stays separate.
- [x] 5.6 Test `debug_get_log` and `shots_get_debug_log` with `dedupe: true`: a real repeated-burst case collapses correctly; `dedupe` combined with `filter`/`tail`; and that omitting `dedupe` reproduces the exact prior response shape (no `count`/`lastLine` fields) — regression guard matching the pattern used for the other parameters. (`tst_profilemanager.cpp`'s `debugGetLog_dedupe*` tests, `tst_mcptools_shots_debuglog.cpp`'s `dedupeCollapsesRepeatedBurst`/`noDedupeReproducesPriorShape`.)

## 6. Documentation

- [x] 6.1 Update `docs/CLAUDE_MD/MCP_SERVER.md`'s `debug_get_log` and `shots_get_debug_log` tool descriptions with the new parameters and their interactions.
- [x] 6.2 Update the same tool descriptions with `dedupe`'s semantics (consecutive-only, timestamp-stripped comparison, `count`/`lastLine` fields).

## 7. Finalize

- [x] 7.1 Re-read `specs/mcp-server/spec.md` in this change against what actually shipped (parameter names, defaults, interaction rules, including the new `dedupe` requirement) before archiving, and correct any drift. (No drift found.)
- [x] 7.2 Run the full local test suite (`docs/CLAUDE_MD/TESTING.md`) before opening/updating the PR — no CI gate exists on this repo for pull requests. (89/89 passed.)
- [x] 7.3 Archive via `openspec archive improve-mcp-debug-log-search` as the last commit on the feature branch before merge.
