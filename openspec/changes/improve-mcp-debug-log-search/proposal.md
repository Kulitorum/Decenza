## Why

`debug_get_log` and `shots_get_debug_log` are pure offset/limit pagination over raw log text — there is no way to search, filter by severity, or jump to the end of a session. Finding one warning or one BLE event among thousands of lines requires paging through in chunks of up to 2000 lines and reading each one by eye. This is the exact friction hit repeatedly during live debugging sessions ("check the app log for anything to review", hunting for one dropped frame), and it costs both wall-clock time and MCP round-trips/tokens for every investigation.

## What Changes

- Add a `filter` parameter (substring, with an optional `regex` flag) to both `debug_get_log` and `shots_get_debug_log`, applied before offset/limit pagination, so a caller can jump straight to matching lines instead of paging through everything.
- Add a `minLevel` parameter to `debug_get_log` (`DEBUG` < `INFO` < `WARN` < `ERROR` < `FATAL`) that returns only lines at or above the given severity — the app log already tags every line with a level; the shot debug log carries no comparable level tagging today and is out of scope for this parameter.
- Add a `tail` parameter to both tools: when set (a line count), returns the last N lines/matches of the addressed range (whole log, one session, or one shot log) without a separate call to learn the total line count first.
- Include the absolute line number of every returned/matching line in both tools' responses, so a filtered hit can be followed up with an offset-based request for surrounding context.
- Cache the persisted app-log's session-boundary index (keyed on file size + mtime) inside `WebDebugLogger` instead of rescanning up to 100,000 lines on every `sessions=true`/`session=N` call.

## Capabilities

### Modified Capabilities
- `mcp-server`: `debug_get_log` and `shots_get_debug_log` gain filter/minLevel/tail parameters and return absolute line numbers; the underlying session-index lookup is cached rather than rescanned per call.

## Impact

- `src/mcp/mcpresources.cpp` (`debug_get_log` tool + its `sessions`/`session` handling)
- `src/mcp/mcptools_shots.cpp` (`shots_get_debug_log` tool)
- `src/network/webdebuglogger.h/.cpp` (new cached session-index API backing the tool; existing `getPersistedLogChunk` callers unaffected)
- `docs/CLAUDE_MD/MCP_SERVER.md` (tool descriptions)
- No new dependencies. No breaking changes — all new parameters are optional and additive; omitting them reproduces today's exact behavior and response shape.
