## MODIFIED Requirements

### Requirement: Debug log tools support substring/regex filtering
`debug_get_log` and `shots_get_debug_log` SHALL accept an optional `filter` string parameter and an optional `regex` boolean parameter. When `filter` is provided, only lines matching it are eligible for pagination/tail; matching is case-insensitive substring containment by default, or a case-insensitive regular expression match when `regex` is `true`. Filtering SHALL be applied before offset/limit or tail is applied.

`debug_get_log`'s tool description SHALL name the subsystem markers a caller can filter on (`[Scale]`, `[DE1]`) and the severity convention that separates each subsystem's user-facing narrative from its developer detail, so retrieving what a device panel shows requires no knowledge of the source. The description SHALL also warn that a bracketed marker is a substring, not a regular expression — passed with `regex: true` it is a character class that matches almost every line, which fails by returning too much rather than erroring.

No dedicated preset parameter is provided: `filter` plus `minLevel` already express these queries in a single call, and a second way to say the same thing would need precedence rules against them.

#### Scenario: Substring filter narrows an app-log request
- **WHEN** an MCP client calls `debug_get_log` with `filter: "R2 error"`
- **THEN** the response's `log`/`lines` contain only lines whose text contains "R2 error" (case-insensitive), and `returnedLines`/pagination fields are computed against the filtered set, not the full log

#### Scenario: Regex filter on a shot debug log
- **WHEN** an MCP client calls `shots_get_debug_log` with `shotId`, `filter: "SAW.*trigger"`, `regex: true`
- **THEN** the response contains only lines matching that pattern

#### Scenario: No filter reproduces existing behavior
- **WHEN** an MCP client calls either tool without a `filter` parameter
- **THEN** the response is identical in shape and content to the tool's behavior before this change

#### Scenario: A caller can reproduce a device panel from the tool description alone
- **WHEN** an MCP client has only `debug_get_log`'s description and wants the lines the connections page's scale view shows
- **THEN** the description tells it to pass the `[Scale]` marker as `filter` with `minLevel: "INFO"` and `session: -1`, and that call returns that set

#### Scenario: The marker-as-regex trap is documented
- **WHEN** an MCP client reads the description before filtering on a marker
- **THEN** it is told to pass the marker as a substring, because `[Scale]` under `regex: true` is a character class that matches nearly every line
