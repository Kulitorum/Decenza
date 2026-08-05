## ADDED Requirements

### Requirement: Merged tools SHALL dispatch on a required `action` argument

A tool that replaces a family of per-verb tools SHALL take a required `action` argument whose enum values are the verb suffixes of the tools it replaces. `action` SHALL have no default. A call with a missing or unrecognised `action` SHALL fail with an error that enumerates the valid values.

#### Scenario: Action selects the behaviour
- **WHEN** a client calls `steam_pitcher` with `action: "select"` and a valid id
- **THEN** the tool performs what `steam_pitcher_select` performed and returns the same payload

#### Scenario: Missing action
- **WHEN** a client calls a merged tool with no `action`
- **THEN** the response is an error listing the tool's valid actions
- **AND** no state changes

#### Scenario: Unknown action
- **WHEN** a client calls a merged tool with `action: "frobnicate"`
- **THEN** the response is an error listing the tool's valid actions
- **AND** no state changes

### Requirement: Access level, rate limiting, and confirmation SHALL resolve per action

Each action of a merged tool SHALL declare its own category, which determines access level and whether the call counts against the control/settings rate limit, and SHALL declare whether it requires confirmation. The server SHALL resolve all three from the tool name and the call's arguments. Handlers SHALL NOT inspect the `confirmed` argument; the server strips it before dispatch.

#### Scenario: Read action is not rate-limited
- **WHEN** a client calls `steam_pitcher` with `action: "list"`
- **THEN** the call does not increment the control-call counter and is not refused by the rate limiter

#### Scenario: Destructive action still confirms
- **WHEN** a client calls `steam_pitcher` with `action: "delete"` and no `confirmed`
- **THEN** the server returns a `needs_confirmation` payload before the handler runs

#### Scenario: Confirmation names the action
- **WHEN** a confirmation is raised for a merged tool
- **THEN** the payload's `action` field is `<tool>.<action>` and its description names the specific verb

#### Scenario: Read action does not confirm
- **WHEN** a client calls `flow_calibration` with `action: "get"`
- **THEN** no confirmation is raised

### Requirement: Gating SHALL fail closed on an unresolvable action

When `action` is absent or is not a declared value, the server SHALL resolve category and confirmation to the most restrictive action the tool declares, apply that gate, and only then let the handler return its validation error.

#### Scenario: Omitted action cannot bypass confirmation
- **WHEN** a client calls a merged tool that has at least one confirmable action, with no `action` and no `confirmed`
- **THEN** the server returns `needs_confirmation` rather than dispatching to the handler

#### Scenario: Omitted action cannot bypass access level
- **WHEN** a client at Monitor access calls a merged tool that has a settings-level action, with no `action`
- **THEN** the call is refused for access level, not executed
