# mcp-tool-surface-budget Specification

## Purpose
Bound what `tools/list` costs. The listing goes to every client on every connection and real clients truncate it silently, so tool count, description length, payload size, listing order and where long-form documentation lives are all governed here.

## Requirements
### Requirement: Tool listings SHALL NOT carry inline binary payloads

`tools/list` SHALL NOT include an `icons` array on any tool record, and no field of a tool record SHALL contain a `data:` URI. Resource listings are unaffected.

#### Scenario: Tool list carries no icons
- **WHEN** a client negotiates protocol 2025-11-25 and calls `tools/list`
- **THEN** no tool record contains an `icons` key
- **AND** no tool record contains the substring `data:`

#### Scenario: Resource icons are retained
- **WHEN** the same client calls `resources/list`
- **THEN** every resource record still includes an `icons` array with at least one SVG entry

### Requirement: The tool surface SHALL stay inside a declared budget

The registered tool surface SHALL satisfy all of: at most 80 registered tools; every tool description at most 500 characters; every schema property description at most 120 characters; an estimated `tools/list` payload of at most 45 KB. The four limits SHALL be declared in one place so that changing them is a single edit.

#### Scenario: Budget is enforced before merge
- **WHEN** a pull request touching `src/**` is opened
- **THEN** a build-free check parses the tool registrations and fails the job if any of the four limits is exceeded
- **AND** the failure message names the offending tool and which limit it broke

#### Scenario: Adding a tool inside the budget passes
- **WHEN** a pull request adds a tool whose description is within the limit and which keeps the count at or under 80
- **THEN** the check passes

### Requirement: Long-form tool documentation SHALL be retrievable on demand

A tool's `description` SHALL carry only what a client needs to select the tool and fill its arguments. Output-field meanings, worked call sequences, and interaction rules SHALL be served on request instead: `get_agent_file` SHALL accept an optional `topic` argument returning that topic's markdown, and the same content SHALL be exposed as a `decenza://tools/<topic>` resource.

#### Scenario: Topic requested by name
- **WHEN** a client calls `get_agent_file` with `topic` naming a documented tool or family
- **THEN** the response contains that topic's markdown and the app version string

#### Scenario: Unknown topic
- **WHEN** a client calls `get_agent_file` with a `topic` that does not exist
- **THEN** the response is an error naming the available topics

#### Scenario: No topic keeps existing behaviour
- **WHEN** a client calls `get_agent_file` with no arguments
- **THEN** it receives the dialing-assistant system prompt and version exactly as before

### Requirement: Tool listing order SHALL be deterministic and importance-ordered

Every tool SHALL declare a tier: `0` core, `1` standard, `2` niche. `tools/list` SHALL emit tools sorted by `(tier, name)`, so that repeated listings from the same build are byte-identical in order and a client that truncates the list loses the niche tail first.

#### Scenario: Stable order across restarts
- **WHEN** `tools/list` is called on two separate runs of the same build
- **THEN** the tool names appear in the same order in both responses

#### Scenario: Niche tools sort last
- **WHEN** a client calls `tools/list`
- **THEN** every tier-0 tool precedes every tier-1 tool, and every tier-1 tool precedes every tier-2 tool
