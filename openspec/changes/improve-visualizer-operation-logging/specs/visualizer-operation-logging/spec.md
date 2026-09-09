## Purpose

Explain Visualizer uploads, imports and synchronization from the persisted application log, including operations that fail before a request or after receiving an unusable response.

## ADDED Requirements

### Requirement: Visualizer operations retain diagnostic identity

Uploads, metadata updates, connection tests, shot lists, profile imports, history recoveries and coffee-management passes SHALL carry an operation identifier on their start, stage and terminal records. Retries and paginated stages SHALL retain that identifier. Known local shot, remote shot and bag identifiers SHALL identify the action actually dispatched. Overlapping or later operations SHALL NOT inherit another operation's context.

#### Scenario: An upload retries and then completes
- **WHEN** an upload fails transiently and succeeds within the existing retry budget
- **THEN** its attempt records share an operation identifier and exactly one terminal upload success names the originating local shot and returned remote shot

#### Scenario: Independent actions overlap
- **WHEN** a connection test and shot-list request overlap with an upload
- **THEN** each action's records retain a distinct operation identifier and the context captured for that action

#### Scenario: A batch crosses pages and items
- **WHEN** a list or import continues through several pages or records
- **THEN** the batch retains one identity and item records include their own relevant identifiers without being mistaken for the batch's terminal outcome

### Requirement: Visible outcomes describe the usable result

Each initiated action SHALL produce one terminal result under `[Visualizer]`. Success, valid empty results, intentional skips and cancellation SHALL be visible at INFO; terminal failures, partial failures and actionable rejections SHALL be visible at WARN or above. The result SHALL name its kind, stage, elapsed time and bounded reason or counts. Intermediate retries SHALL stay at DEBUG. Diagnostic-file receipts SHALL NOT stand in for operation completion.

#### Scenario: Credentials are missing
- **WHEN** an explicit connection test, upload, list, import or recovery request is refused for missing credentials
- **THEN** the log contains a rejected result naming the action and missing-credentials reason without recording credentials

#### Scenario: An upload exhausts retries
- **WHEN** an upload fails after its existing retry budget is exhausted
- **THEN** a WARN query returns its terminal failure with HTTP status when supplied, network error code and shot context

#### Scenario: A response has no usable shot identifier
- **WHEN** an HTTP response succeeds but does not provide the shot identifier required by the upload
- **THEN** the upload result is failed at the interpretation stage and does not claim a successful upload

#### Scenario: A list stops at its defensive page limit
- **WHEN** a list exceeds its existing page ceiling before reaching the end
- **THEN** its WARN result identifies the incomplete list and page limit rather than reporting an empty or successful list

#### Scenario: Recovery completes with failed items
- **WHEN** history recovery imports some shots and fails others
- **THEN** its terminal WARN result reports partial completion with total, imported, skipped and failed counts

#### Scenario: An import waits for a duplicate decision
- **WHEN** a downloaded profile requires the user to choose overwrite, rename or cancellation
- **THEN** the import remains pending until that choice resolves, and the eventual result describes the save or cancellation rather than merely the download

### Requirement: Upload and subsequent synchronization are distinct outcomes

The log SHALL distinguish successful shot upload from subsequent coffee-management linking, enrichment or repair. Related passes SHALL carry a parent operation identifier where applicable. An expected capability restriction SHALL be described as a limitation or skip at INFO; an unexpected request or interpretation failure SHALL be WARN. A scheduled retry SHALL NOT claim that synchronization succeeded.

#### Scenario: A shot uploads but bag enrichment fails
- **WHEN** an uploaded shot receives a remote identifier and subsequent bag enrichment fails
- **THEN** the log preserves the successful upload and separately reports the related failed synchronization stage with bag and shot context

#### Scenario: The account lacks a premium capability
- **WHEN** the existing service handling recognizes that optional enrichment requires an unavailable account capability
- **THEN** the log describes the limitation without reporting that the already completed upload failed

#### Scenario: A bag update is queued for a future pass
- **WHEN** a failed bag update is retained for the existing later retry path
- **THEN** the log states that it remains pending and a later successful pass has its own truthful result

### Requirement: Visualizer main-log context is bounded and excludes payloads

Visualizer operation diagnostics SHALL use stable reason codes, bounded identifiers, counts and status values. They SHALL NOT persist full request or response bodies, remote error prose, credentials, authorization headers or share-code query values in the main log. Logged URL identity SHALL omit user information, queries and fragments. Existing dedicated diagnostic files retain their separate role.

#### Scenario: A server error echoes private content
- **WHEN** a failed response echoes credentials, notes or request data
- **THEN** the main log records the status and safe failure classification without that content

#### Scenario: A share-code import fails parsing
- **WHEN** an import response cannot be parsed
- **THEN** the log identifies the failed operation and parse stage without a response snippet or share code

### Requirement: Logging preserves application behavior and event ownership

Diagnostic collection SHALL NOT add or reroute requests, change retry budgets or timing, alter result signals, persist different application data or change device control. `[Visualizer]` SHALL remain the registered owner. Source tags SHALL identify emitters consistently. Forwarded signals SHALL NOT duplicate an existing terminal diagnostic, while a downstream persistence failure SHALL remain a distinct event owned by the subsystem that failed.

#### Scenario: A result reaches several consumers
- **WHEN** an upload result is delivered to a controller and UI listeners
- **THEN** the persisted log contains one terminal upload result and the listeners retain their existing behavior

#### Scenario: A late callback arrives after cancellation
- **WHEN** a diagnostic operation has already ended and a late callback is delivered
- **THEN** no second terminal event is recorded and logging does not alter the application's existing callback handling

#### Scenario: Validation uses fake replies
- **WHEN** regression validation drives success, retry, failure and cancellation cases
- **THEN** it can prove diagnostic behavior and unchanged request counts without live Visualizer or paid AI requests
