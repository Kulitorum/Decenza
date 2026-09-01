## MODIFIED Requirements

### Requirement: A link that has stopped accepting writes is self-explanatory in the log

The log entry recording this condition SHALL state that the link stopped accepting writes
while still reporting itself connected, and SHALL name what happens next. Because the system now
recovers such a link itself, the entry SHALL NOT instruct the user to reconnect the DE1; it SHALL
say what the system is doing, so a reader is not asked to perform an action already under way. It
SHALL be emitted at a level the connection log views display by default. Submitted debug logs are
read by users' AI assistants, so a bare failure count that requires knowledge of this subsystem to
interpret is insufficient.

#### Scenario: Diagnosing from a submitted log

- **WHEN** this condition is recorded in a log later submitted with a bug report
- **THEN** the entry conveys the condition and what the system did about it

#### Scenario: The entry is visible in the connection views

- **WHEN** the condition is recorded
- **THEN** the entry is at a level those views show without the user changing a filter

#### Scenario: The entry does not ask for a manual reconnect

- **WHEN** the condition is recorded and recovery is under way
- **THEN** the entry does not direct the user to reconnect the DE1 from the Connections page or
  over MCP
