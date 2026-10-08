# ai-operation-logging Specification

## Purpose
Make AI-backed user actions diagnosable from the persisted application log, including failures before a provider is called and failures interpreting a provider response.

## Requirements

### Requirement: Each AI-backed operation has a correlated terminal outcome

Each advisor request, product-page search and bag-details extraction SHALL have a session-unique diagnostic identifier that relates its retries and errors to exactly one terminal outcome. A request rejected before execution SHALL have an explicit rejection outcome. Cancellation and supersession SHALL be distinguishable from success and failure.

#### Scenario: A local archive fetch fails

- **WHEN** a bag extraction stops because an archive page returns HTTP 429 before provider invocation
- **THEN** the main log contains one terminal page-fetch failure naming the archive host, HTTP status, bag when known, and that no provider was invoked

#### Scenario: A provider request fails

- **WHEN** the selected provider rejects an advisor request
- **THEN** the operation's terminal event names the provider and model actually requested and the bounded rejection reason

#### Scenario: Settings change during a request

- **WHEN** provider settings change while an operation is in flight
- **THEN** its later log events retain the provider and model used for that operation

#### Scenario: An intermediate retry succeeds

- **WHEN** an operation retries a provider request and then succeeds
- **THEN** retry details share the operation identifier and exactly one terminal success is recorded

#### Scenario: A request is cancelled or superseded

- **WHEN** a started operation ends through cancellation or supersession
- **THEN** it records that outcome once and a late callback does not produce a second completion

### Requirement: The terminal event SHALL name the stage, and the provider only when invoked

The terminal event SHALL identify the operation, the relevant bag or shot when known, elapsed time for a started operation, the stage reached, and a result or bounded failure reason. When a provider was invoked, it SHALL name the provider and model actually used. A local failure SHALL state that no provider was invoked. Retries SHALL stay associated with their original operation.

#### Scenario: Terminal event names the stage reached

- **WHEN** an operation ends at a stage before provider invocation
- **THEN** the terminal event SHALL name that stage and state that no provider was invoked

### Requirement: An outcome describes the usable result

A successful receipt or save of a provider response SHALL NOT by itself be reported as a successful search, extraction or advice. The terminal result SHALL reflect the validation that operation requires. An empty valid result SHALL be distinguishable from invalid output or a failed request. An archive lookup with no returned capture SHALL describe that observed result rather than assert that no capture exists.

#### Scenario: Invalid extraction JSON

- **WHEN** a provider returns text that cannot be interpreted as the required extraction result
- **THEN** the main log reports a parsing failure for that operation rather than successful extraction

#### Scenario: Valid extraction has no fields

- **WHEN** an extraction returns a valid empty result
- **THEN** its terminal event explicitly identifies the empty result without claiming fields were populated

#### Scenario: Saving the response file succeeds after provider failure

- **WHEN** a failed provider response is successfully written to its diagnostic file
- **THEN** the operation still has a failed terminal result visible without opening that file

#### Scenario: Availability lookup returns no capture

- **WHEN** an archive availability request returns no capture
- **THEN** the log records that lookup result without asserting permanent absence of an archived page

### Requirement: Outcome log levels SHALL follow outcome severity

Success or a valid empty outcome SHALL be logged at INFO. Terminal failure and actionable rejection SHALL be logged at WARN or above. User cancellation SHALL NOT be represented as a fault. Successful prompt or response file-write receipts SHALL NOT be emitted automatically, while file-write failures SHALL remain diagnosable.

#### Scenario: User cancellation is not a fault

- **WHEN** the user cancels an operation in flight
- **THEN** the outcome SHALL NOT be logged as a fault

### Requirement: A bag query includes its AI stages

A query for the registered bag subsystem SHALL retrieve the terminal result with known page status, the final stage reached, provider/model when invoked and the failure or interpretation outcome. General advisor operations SHALL be retrievable under their own registered subsystem. Context SHALL be updated without automatically logging start, dispatch, response or ready records. A terminal event SHALL NOT be duplicated under multiple subsystem markers.

#### Scenario: The bag fetch succeeds but provider parsing fails

- **WHEN** an assistant filters the main log for the bag subsystem
- **THEN** one terminal result identifies the provider parsing failure with known page status and the operation identifier, without automatic per-stage chatter

### Requirement: Outcome diagnostics do not expose request content or secrets

Main-log outcome events SHALL use bounded summaries, relevant identifiers and result counts. They SHALL NOT introduce prompts, fetched page bodies, full provider responses, credentials, authorization headers or access tokens into the main log. Logged URL identity SHALL omit credentials, query strings and fragments. Collecting the diagnostics SHALL NOT initiate extra provider requests or change provider/model selection.

#### Scenario: An error includes sensitive request data

- **WHEN** a remote error contains credentials or echoes request content
- **THEN** the main-log outcome uses a safe bounded reason and status without persisting the sensitive payload

#### Scenario: Validation exercises provider errors

- **WHEN** error-outcome logging is validated with a test provider
- **THEN** validation can demonstrate the outcome without consuming a paid provider request
