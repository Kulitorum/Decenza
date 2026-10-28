# remote-ui-session Specification

## Purpose
Connects independent browser interface sessions to the native Decenza host, which owns machine operations and persisted data, while making synchronization and connection failures visible.

## Requirements

### Requirement: Shared services cover the complete app interface
Native and Wasm QML SHALL use shared domain rules and supported host operations through shared Qt Remote Objects services. Services SHALL provide correlated state, progress, errors and completion for every implemented UI group. The host SHALL own hardware, persistence and integrations; browser navigation and file selection/downloads SHALL stay local where appropriate. Inert stubs and duplicate browser domain implementations SHALL NOT satisfy parity.

#### Scenario: Settings or maintenance is invoked from shared QML
- **WHEN** native or Wasm invokes a shared settings, calibration, firmware or maintenance action
- **THEN** the host applies the same canonical preconditions and operation lifecycle
- **AND** the requesting interface displays correlated progress, refusal and completion without requiring browser access to native hardware

#### Scenario: A browser disconnects during a host operation
- **WHEN** an accepted host operation outlives the requesting browser
- **THEN** its native lifecycle remains authoritative
- **AND** reconnecting observes its current result without replaying the request or navigating another client's draft

### Requirement: Native host owns machine execution
The device connected to the DE1 SHALL remain authoritative for device communication, machine state, stop conditions, operation timing, and persistence. Browsers SHALL request supported operations through the host's existing domain behavior. Browser execution or connectivity SHALL NOT be required for an accepted operation to complete or stop correctly.

#### Scenario: Browser disappears during a shot
- **WHEN** a browser closes, suspends, or loses connectivity during a shot
- **THEN** the host continues processing machine and scale data and applying configured stop conditions
- **AND** it saves the resulting shot through the normal native path

#### Scenario: Browser requests an operation
- **WHEN** a connected browser requests a supported machine operation
- **THEN** the host applies the same validation, access rules, and machine preconditions as the equivalent native operation
- **AND** the browser reports the actual accepted, rejected, pending, or completed result

### Requirement: Independent interface navigation
Each browser SHALL maintain its own navigation and unsaved form state. Ordinary browsing SHALL NOT drive the native screen or another browser's navigation. Host-owned sleep/wake and machine-driven operation screens are synchronized exceptions. Persisted data and machine state SHALL remain shared and authoritative on the host.

#### Scenario: Review history while the tablet displays brewing
- **WHEN** one browser opens shot history while the native screen shows the current operation
- **THEN** each screen retains its own navigation
- **AND** all sessions refer to the same host data

### Requirement: Shared sleep and machine-driven presentation
The host SHALL own sleep/screensaver state. A browser opening or reconnecting SHALL display the host’s current sleep state. Sleep or wake requested from either interface SHALL use the common host control service and update every interface. Touching the browser screensaver SHALL wake the host interface and machine. Ordinary page navigation and unsaved drafts SHALL remain local outside these shared lifecycle transitions.

#### Scenario: Browser joins a sleeping host
- **WHEN** a browser starts or reconnects while the native host is sleeping
- **THEN** it displays the shared screensaver
- **AND** tapping it requests host/machine wake and dismisses sleep presentation across connected clients

#### Scenario: Machine starts an operation
- **WHEN** the machine enters espresso, steam, hot water, flush, descaling or transport, or a browser joins during that operation
- **THEN** native and browser show the existing shared operation page
- **AND** ordinary browsing does not navigate the other interface

### Requirement: Maintenance navigation does not end the host workflow
Native and Wasm Descaling and Transport screens SHALL treat Back as navigation only. Explicit Finish, Cancel, or Stop actions SHALL end or cancel the corresponding host-owned workflow from any client, using the canonical device/controller rules. Leaving, closing, or disconnecting a view SHALL NOT restore heating, cancel preparation, or stop the shared operation. Explicit steam requests SHALL retain the native controller's existing override of a descaling heater hold.

#### Scenario: An observer leaves maintenance
- **WHEN** a native or browser observer presses Back during descaling cooling or transport preparation
- **THEN** only that client's screen changes
- **AND** the host workflow continues until an explicit workflow control or existing host rule ends it

#### Scenario: Explicit maintenance completion
- **WHEN** a user activates Finish, Cancel, or Stop from any connected client
- **THEN** the host applies the corresponding canonical cleanup or stop once
- **AND** every client observes the shared result without replaying the action after reconnect

### Requirement: Browser ignores application quit
The Wasm interface SHALL ignore application Quit actions, including direct and confirmation-based layout actions. A browser Quit action SHALL NOT terminate the Wasm runtime, close its interface, or request native host shutdown. Native Quit behavior SHALL remain unchanged.

#### Scenario: Quit from a shared browser control
- **WHEN** a browser user activates a Quit widget or a custom Quit action
- **THEN** the browser interface remains available without a quit confirmation
- **AND** the native host and its current operation remain running

### Requirement: Shared preparation workflow selection
Native and Wasm SHALL share one host-owned preparation selection for espresso, steam, hot water, and flush. Selection SHALL update preparation controls without changing any client's navigation, dialogs, or drafts, and SHALL NOT start an operation. HTML workflows SHALL remain available under `/legacy/`, preserving route context.

#### Scenario: Select a preparation workflow from another interface
- **WHEN** a user selects Steam in a browser while native Decenza is showing an editor, or selects Espresso on native Decenza while a browser is reviewing history
- **THEN** all interfaces show the shared selected preparation workflow and its matching preparation controls
- **AND** each interface remains on its current route with its open dialog and unsaved draft intact
- **AND** no machine operation starts from the selection alone

### Requirement: Host-owned bean and milk capture
The native host SHALL own the bean/milk stable-weight capture algorithm, timer, measured values, and any resulting settings/device writes. Native and browser presentation SHALL observe the same capture state and result. A browser SHALL NOT run a second capture timer or independently apply a captured value, and a client disconnect SHALL NOT interrupt an already accepted host capture.

#### Scenario: Two interfaces observe one capture
- **WHEN** native and browser interfaces display the same preparation workflow while a capture is active
- **THEN** the host performs the stable-weight capture and any resulting setting/device update once
- **AND** both interfaces display the authoritative capture outcome
- **AND** loss of the browser connection does not start, repeat, or cancel the host's accepted capture

### Requirement: Guarded shared pitcher selection
Browser pitcher selection SHALL identify the ordered preset revision it displayed.
The host SHALL reject an obsolete revision before applying any selection or values,
derive milk fallback from host-owned capture state, and reuse the native selection
operation. Selection alone SHALL NOT start a machine operation. Browser settings
SHALL change only from authoritative host state.

#### Scenario: Presets reordered before a browser selection arrives
- **WHEN** the host's ordered presets change after the browser displayed them
- **AND** the browser submits a selection against the old revision
- **THEN** the host rejects the action without selecting the row now occupying that index
- **AND** the browser reports the rejection without replaying the action

### Requirement: Host-owned steam lifecycle
The native host SHALL apply steam-start milk scaling, preserve manual time overrides, reset session adjustments on steam end, and own the auto-flush countdown and expiry action. Shared pages SHALL observe these values without repeating those side effects when a page opens, reconnects, or receives telemetry. Countdown expiry SHALL be cancelled when the machine leaves the applicable Steam/Puffing state.

#### Scenario: A browser joins an active steam session
- **WHEN** a browser opens or reconnects while the host is steaming
- **THEN** it receives the current measured-milk, scaling and countdown state
- **AND** it does not reapply the scaled duration or start another countdown

#### Scenario: Steam ends before auto-flush expiry
- **WHEN** the host leaves the applicable Steam/Puffing state before the countdown expires
- **THEN** the host cancels that countdown
- **AND** the old expiry cannot stop a subsequent operation

### Requirement: Atomic Steam page actions
Steam page activation, pitcher application and time adjustment SHALL be host-owned
operations using current host settings. Browser commands SHALL carry the machine
control context they observed; the host SHALL reject commands from a previous raw
state, substate or connection context before applying changes. Browser pages SHALL
report failures and update values from host snapshots rather than optimistic writes.

#### Scenario: Two clients press the first Steam stop
- **WHEN** both clients observed the first-tap Stop state and one request was already accepted
- **THEN** the other first-tap request is rejected instead of becoming Purge
- **AND** both clients observe the same host-owned stop state
- **AND** a raw Steam exit or disconnect clears that state for the next operation

#### Scenario: Only the requesting Steam page dismisses
- **WHEN** a client requests Back or a stop that dismisses its page
- **THEN** that client waits for its correlated successful result before dismissing
- **AND** the other interface keeps its independent navigation

#### Scenario: A time adjustment arrives after the machine changes operation
- **WHEN** a browser sends a five-second adjustment and the machine changes control context before it arrives
- **THEN** the host rejects the request without changing the new operation's timeout
- **AND** the browser reports the rejection without automatically retrying it

#### Scenario: Two clients adjust a stable steam operation
- **WHEN** two distinct accepted five-second adjustments target the same current control context
- **THEN** the host applies each delta to its current timeout in order
- **AND** neither adjustment overwrites the other using an older browser value

### Requirement: Initial state and ordered updates
A session SHALL load an authoritative initial state and receive updates for its displayed data. It SHALL reject obsolete updates, detect gaps or a host restart, and obtain a fresh snapshot before relying on state that may be incomplete. Live monitoring SHALL include the current operation's accumulated graph samples when joining mid-operation.

#### Scenario: Join during extraction
- **WHEN** a browser opens the live screen partway through extraction
- **THEN** it receives the current phase, readings, and available samples from the beginning of that operation
- **AND** subsequent samples extend the same graph without duplicates

#### Scenario: Host restarts or updates arrive out of order
- **WHEN** a session observes a new host instance or an update older than its current state
- **THEN** it does not overwrite newer state with the obsolete update
- **AND** it resynchronizes when continuity cannot be established

### Requirement: Disconnection and resume
The interface SHALL distinguish connected, reconnecting, unauthorized, and unavailable states. On loss of a usable session it SHALL mark displayed live data as stale and disable new mutations. On resume it SHALL revalidate access and refresh authoritative state before enabling mutations.

#### Scenario: Resume a suspended mobile tab
- **WHEN** a user returns to a phone browser after the host state changed while the tab was suspended
- **THEN** the interface refreshes state before accepting another edit or command
- **AND** old readings are not presented as current during reconnection

#### Scenario: Host is unavailable
- **WHEN** the host app stops or the network is lost
- **THEN** the interface displays the connection failure and permits a retry
- **AND** it does not operate on an independent local copy of host data

### Requirement: Uncertain commands are not replayed
A browser SHALL NOT automatically replay a mutation after a lost response or reconnect. It SHALL distinguish an unknown outcome from a confirmed failure and refresh authoritative state to resolve what can be resolved. Retrying a command SHALL require a new deliberate action after reconciliation.

#### Scenario: Response is lost after execution
- **WHEN** the host accepts a command but its response does not reach the browser
- **THEN** the browser reports an uncertain outcome and refreshes state
- **AND** reconnecting does not execute the command again

### Requirement: Writes preserve concurrent changes
Edits SHALL use host validation and write-through persistence. An edit SHALL preserve unrelated host changes made while its form was open. Conflicting changes to the same field SHALL be reported for reconciliation rather than silently overwriting another edit. Successful changes SHALL become visible to other sessions viewing the affected data.

#### Scenario: Native state changes while a browser edits
- **WHEN** a shot stamps a bag's opened date while a browser edits its tasting notes
- **THEN** saving the notes preserves the host's opened date
- **AND** both interfaces show the saved result

#### Scenario: Two sessions edit the same field
- **WHEN** two sessions open the same record and submit different values for the same field
- **THEN** the first accepted write persists
- **AND** the later stale write reports a conflict and offers refreshed values

### Requirement: Secrets and integrations stay on the host
Third-party integrations SHALL run through host services using the user's existing configuration. The browser SHALL NOT need stored provider credentials to perform those workflows. Account setup inputs SHALL be sent only through the intended authenticated configuration workflow and SHALL NOT be returned through general state snapshots.

#### Scenario: Request bean extraction
- **WHEN** a browser invokes bean extraction
- **THEN** the host uses the configured provider and model under the existing rules
- **AND** browser state receives the result or error without receiving the stored provider key

### Requirement: Slow clients do not impede host control
Remote sessions SHALL consume bounded host resources. A slow or disconnected client SHALL NOT cause an unbounded update queue or block native device handling. A client that falls behind SHALL receive a fresh state rather than require the host to retain unlimited missed updates.

#### Scenario: Browser stops consuming telemetry
- **WHEN** a browser stops reading updates during an operation
- **THEN** the host bounds or ends that subscription
- **AND** native machine and scale processing continue
