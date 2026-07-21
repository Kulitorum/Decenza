## ADDED Requirements

### Requirement: Debug log tools support substring/regex filtering
`debug_get_log` and `shots_get_debug_log` SHALL accept an optional `filter` string parameter and an optional `regex` boolean parameter. When `filter` is provided, only lines matching it are eligible for pagination/tail; matching is case-insensitive substring containment by default, or a case-insensitive regular expression match when `regex` is `true`. Filtering SHALL be applied before offset/limit or tail is applied.

#### Scenario: Substring filter narrows an app-log request
- **WHEN** an MCP client calls `debug_get_log` with `filter: "R2 error"`
- **THEN** the response's `log`/`lines` contain only lines whose text contains "R2 error" (case-insensitive), and `returnedLines`/pagination fields are computed against the filtered set, not the full log

#### Scenario: Regex filter on a shot debug log
- **WHEN** an MCP client calls `shots_get_debug_log` with `shotId`, `filter: "SAW.*trigger"`, `regex: true`
- **THEN** the response contains only lines matching that pattern

#### Scenario: No filter reproduces existing behavior
- **WHEN** an MCP client calls either tool without a `filter` parameter
- **THEN** the response is identical in shape and content to the tool's behavior before this change

### Requirement: App debug log supports a minimum-severity filter
`debug_get_log` SHALL accept an optional `minLevel` parameter (`"DEBUG" | "INFO" | "WARN" | "ERROR" | "FATAL"`, ordered ascending) that restricts returned lines to that level or higher, based on the level tag already present on every persisted log line. `minLevel` SHALL combine with `filter` (a line must satisfy both to be returned). `shots_get_debug_log` SHALL accept `minLevel` without error but ignore it, since the shot debug log carries no level tagging.

#### Scenario: Only warnings and errors from the current session
- **WHEN** an MCP client calls `debug_get_log` with `session: -1, minLevel: "WARN"`
- **THEN** only lines tagged WARN, ERROR, or FATAL from the most recent session are returned

#### Scenario: minLevel combined with filter
- **WHEN** an MCP client calls `debug_get_log` with `filter: "BLE", minLevel: "ERROR"`
- **THEN** only ERROR or FATAL lines containing "BLE" are returned

#### Scenario: minLevel ignored on shot debug log
- **WHEN** an MCP client calls `shots_get_debug_log` with `shotId` and `minLevel: "WARN"`
- **THEN** the call succeeds and returns lines from the shot's debug log unaffected by `minLevel`

### Requirement: Debug log tools support a tail mode
`debug_get_log` and `shots_get_debug_log` SHALL accept an optional `tail` integer parameter. When present, the response contains the last `tail` qualifying lines (after any `filter`/`minLevel` is applied) of the addressed range — the whole log, the addressed session, or the shot's debug log — without requiring a prior call to determine the total line count. When both `tail` and `offset` are supplied, `tail` SHALL take precedence and `offset` SHALL be ignored.

#### Scenario: Tail of the current session
- **WHEN** an MCP client calls `debug_get_log` with `session: -1, tail: 100`
- **THEN** the response contains the last 100 lines of the most recent session, without a preceding call to learn its line count

#### Scenario: Tail of a filtered shot debug log
- **WHEN** an MCP client calls `shots_get_debug_log` with `shotId`, `filter: "flow calibration"`, `tail: 20`
- **THEN** the response contains the last 20 lines of that shot's debug log matching "flow calibration"

#### Scenario: Tail overrides offset
- **WHEN** an MCP client calls either tool with both `offset` and `tail` set
- **THEN** the response is computed using `tail` and `offset` is ignored

### Requirement: Debug log responses carry absolute line numbers
`debug_get_log` and `shots_get_debug_log` SHALL include, alongside the existing newline-joined `log` string field, a `lines` array of `{"line": <absolute 0-based line number in the addressed range>, "text": <line text>}` objects for every returned line, so a caller can issue a follow-up `offset`-based request to see context around a specific hit.

#### Scenario: Line numbers accompany a filtered result
- **WHEN** an MCP client calls `debug_get_log` with `filter: "disconnected"`
- **THEN** each entry in the response's `lines` array carries the absolute line number of that match within the addressed range, in addition to the existing `log` string

### Requirement: Debug log tools support consecutive-line deduplication
`debug_get_log` and `shots_get_debug_log` SHALL accept an optional `dedupe` boolean parameter. When `true`, consecutive lines within the already-filtered/leveled candidate list that are identical once each line's own leading `[<elapsed>]` timestamp field is stripped SHALL be collapsed into a single entry, applied before `tail`/`offset`/`limit`. Each collapsed entry in the `lines` array SHALL carry `count` (the number of consecutive occurrences collapsed) and `lastLine` (the absolute line number of the last occurrence), in addition to the existing `line`/`text` (describing the first occurrence). Non-consecutive occurrences of the same message elsewhere in the addressed range SHALL NOT be collapsed together. When `dedupe` is omitted or `false`, `count`/`lastLine` SHALL NOT appear and the response is unaffected.

#### Scenario: A repeated burst collapses to one entry
- **WHEN** an MCP client calls `debug_get_log` with `dedupe: true` over a range where the same warning fires 3 times consecutively (identical text apart from each line's own timestamp)
- **THEN** the response's `lines` array contains one entry for that warning with `count: 3` and `lastLine` set to the absolute line number of the third occurrence

#### Scenario: Non-consecutive repeats stay separate
- **WHEN** the same message occurs twice in the addressed range with a different, non-matching line in between
- **THEN** `dedupe: true` SHALL produce two separate entries, not one collapsed entry

#### Scenario: dedupe combines with filter and tail
- **WHEN** an MCP client calls either tool with `filter`, `dedupe: true`, and `tail` together
- **THEN** filtering is applied first, then consecutive collapsing, then `tail` selects the last N resulting (collapsed) entries

#### Scenario: No dedupe reproduces existing behavior
- **WHEN** an MCP client calls either tool without a `dedupe` parameter
- **THEN** the response is identical in shape to the tool's behavior without this parameter — no `count` or `lastLine` fields appear

### Requirement: App debug log session index is cached
The app debug log's session-boundary index (used by `debug_get_log`'s `sessions=true` and `session=N` modes) SHALL be cached keyed on the persisted log file's size and modification time, and rebuilt only when either differs from the cached key, instead of rescanning the full file on every call.

#### Scenario: Repeated session queries reuse the cached index
- **WHEN** an MCP client calls `debug_get_log` with `sessions: true` twice in a row with no log activity in between
- **THEN** the second call reuses the cached session index rather than rescanning the persisted log file

#### Scenario: Cache invalidates after new log activity
- **WHEN** new lines are appended to the persisted log file between two `debug_get_log` calls
- **THEN** the next `sessions: true` or `session: N` call rebuilds the index and reflects the new session boundaries
