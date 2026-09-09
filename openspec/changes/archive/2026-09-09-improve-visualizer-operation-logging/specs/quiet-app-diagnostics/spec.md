## ADDED Requirements

### Requirement: Routine diagnostics do not obscure faults

Existing periodic diagnostics SHALL use source-level suppression based on meaningful state. Numeric sampling jitter SHALL NOT defeat suppression. Logging changes SHALL NOT change control, request, storage or sampling behavior. Actual faults, recovery and final shot results SHALL remain available with registered owners.

#### Scenario: A battery moves within its progress band
- **WHEN** charge percentage moves less than five points from the last emitted poll and state remains unchanged
- **THEN** the existing LogCollapse suppresses the poll, while a mode, requested charger, cycle, OS status or power-source change emits immediately

#### Scenario: A scale holds different weights during one healthy episode
- **WHEN** constant-weight samples continue during the same shot or tare episode
- **THEN** changing the held weight does not generate a new healthy-liveness record and existing fault and end-of-episode evidence remains intact

#### Scenario: A shot progresses normally
- **WHEN** extraction colors or ordinary settling samples change
- **THEN** the main log omits those routine traces and preserves actual stop, interrupted stability, final weight and timeout decisions

#### Scenario: A connection failure repeats
- **WHEN** the same failure recurs in one connection attempt episode
- **THEN** it uses LogCollapse rather than emitting DEBUG on every retry, distinct failures remain immediate, and a fresh user attempt or recovery re-arms it

#### Scenario: Forecast retrieval remains healthy
- **WHEN** the same provider returns the same forecast coverage with ordinary temperature changes
- **THEN** repeated success receipts are suppressed while failures and subsequent availability remain visible

#### Scenario: DNS discovery returns the same result repeatedly
- **WHEN** repeated browse cycles have the same backend, counts and error state but different elapsed times
- **THEN** only the first result is emitted, worker start/end receipts are omitted, and a changed result or newly found device remains visible

### Requirement: Memory logs report sustained growth

Memory logging SHALL require sustained growth across sampled history. Plateaus, ordinary QObject churn, isolated steps and transient spikes SHALL remain quiet. Current memory, exact peaks, sample history and object snapshots SHALL remain available on demand. Growth diagnostics SHALL describe measured trends without asserting that a leak is established.

#### Scenario: Memory jumps and then plateaus
- **WHEN** one allocation step is followed by a stable level
- **THEN** no sustained-growth record is emitted

#### Scenario: Growth continues over several windows
- **WHEN** three contiguous block medians show material growth across both intervals
- **THEN** a compact trend identifies medians and span, and additional similar records are suppressed until material further growth

#### Scenario: Sampling was interrupted
- **WHEN** readings are missing, zero or separated by a large gap
- **THEN** a candidate growth window crossing that interruption is rejected

### Requirement: FD inspection is on demand

The application SHALL NOT automatically dump full FD inventories during APK installation. The independent MCP descriptor/socket census SHALL remain available on request.

#### Scenario: An APK installation is dispatched
- **WHEN** the existing installation teardown runs
- **THEN** no automatic FD dump or substitute inventory summary is added to the log and teardown behavior is unchanged

#### Scenario: An investigator requests descriptors
- **WHEN** MCP debug_get_fds is invoked
- **THEN** it still returns the live in-process descriptor and socket information supported by the platform

### Requirement: Existing result messages carry bounded useful context

Visualizer diagnostics SHALL retain registered owner/source tags and replace covered request/response dumps with relevant identifiers and numeric status/error details within existing messages. Successful diagnostic-file writes SHALL remain silent. Warning capture SHALL retain portable source locations and use function context when file/line is unavailable.

#### Scenario: Upload failure includes a response body
- **WHEN** an upload fails with a server response
- **THEN** its existing failure message names the local shot and HTTP/network status without persisting the response body

#### Scenario: A response cannot be parsed
- **WHEN** Visualizer profile JSON parsing fails
- **THEN** the existing warning records parse error and offset without a payload snippet

#### Scenario: Qt supplies only function context
- **WHEN** a warning lacks usable file/line but supplies a function
- **THEN** the function remains available without inventing a source location
