## ADDED Requirements

### Requirement: A single log backs every device diagnostic

Device subsystem events SHALL be recorded to the system log and nowhere else. No subsystem SHALL maintain a private in-memory buffer or a separate exported log file for events it also writes to the system log.

A subsystem event SHALL be written exactly once per output. A call site SHALL NOT write the same event to stderr and separately emit it for recording; the subsystem's logging helper performs both from one call, so the two cannot drift apart in wording or severity.

#### Scenario: A subsystem event is recorded once

- **WHEN** any DE1 or scale subsystem source logs an event
- **THEN** it appears exactly once in the system log, and no second store holds an independent copy

#### Scenario: No separate scale log file is produced

- **WHEN** the app runs a session that connects, uses and disconnects a scale
- **THEN** no `scale_debug_log.txt` is written, and the scale narrative is present in the system log

#### Scenario: Diagnostics survive the panel being closed

- **WHEN** a DE1 permission prompt, scan and connection happen while the Connections page has never been opened, and the user then opens it
- **THEN** those events are present in the DE1 view, because the log recorded them independently of any window being built

### Requirement: Every device log line carries its subsystem marker

Each device subsystem SHALL prefix every line it logs with a stable marker naming that subsystem: `[Scale]` for the scale subsystem (BLE, WiFi and USB scales, their transports and the refractometers) and `[DE1]` for the DE1 subsystem (the machine, its BLE and serial transports, and USB DE1 discovery). The marker SHALL be applied inside the subsystem's logging helper, never written at a call site.

A line MAY carry a further source tag after the marker (`[Scale][BLE AcaiaScale]`, `[DE1][USB]`) to name the specific source. One marker match SHALL be sufficient to retrieve the whole subsystem's narrative, so no subsystem line is reachable only through a source-specific pattern.

#### Scenario: One pattern retrieves a whole subsystem

- **WHEN** the system log is searched for `[Scale]`
- **THEN** the result includes every scale line — manager, driver, transport, refractometer and USB — with none reachable only under a different prefix

#### Scenario: DE1 and scale lines are distinguishable

- **WHEN** the system log is searched for `[DE1]`
- **THEN** scale lines are not returned, and DE1 lines from the machine, its transports and USB discovery all are

#### Scenario: A source with no signal to emit still carries the marker

- **WHEN** a subsystem source cannot emit for recording — a free function, static helper or JNI shim — and logs to stderr only
- **THEN** its line still carries the subsystem marker

### Requirement: Severity distinguishes the user-facing narrative from developer detail

Device logging helpers SHALL offer three tiers, and each call site SHALL be assigned one deliberately:

- **DEBUG** — developer detail. Frame-level protocol traffic, per-poll state, parsing internals. Not shown in the on-screen views.
- **INFO** — the connection narrative a user may need to understand what the app is doing: permission outcomes, scan lifecycle, devices found, connect and disconnect, transport selection and fallback, reconnect scheduling.
- **WARN** or above — problems: failures, timeouts, unreachable devices, rejected data.

A line's tier SHALL be chosen for its audience, not its authorship: a driver may log at INFO when the event is part of the user-facing narrative, and the subsystem manager may log at DEBUG when the detail is only useful to a developer.

#### Scenario: Frame-level traffic stays out of the narrative

- **WHEN** a scale driver logs a decoded weight frame or a transport logs a characteristic write
- **THEN** it is recorded at DEBUG and does not appear in the on-screen scale view

#### Scenario: A connection event is part of the narrative

- **WHEN** the app finds a saved scale in a scan and connects to it
- **THEN** those events are recorded at INFO and appear in the on-screen scale view

#### Scenario: A problem is findable by severity alone

- **WHEN** a WiFi scale host cannot be resolved and the app falls back to Bluetooth
- **THEN** the failure is recorded at WARN or above, so a severity-only request that names no subsystem still surfaces it

### Requirement: The connections page shows each subsystem as a filtered view of the log

The Connections page SHALL present a DE1 view and a scale view. Each SHALL show the lines of the current session that carry its subsystem marker at INFO or above, in the order the log recorded them.

Each view SHALL populate with the session's qualifying lines already recorded when the view is built, and SHALL then append qualifying lines as they are recorded, without polling.

The views SHALL be limited to the current session. Lines from earlier sessions SHALL NOT appear, so a long-lived log does not bury what is happening now.

#### Scenario: The view populates from before it existed

- **WHEN** the user opens the Connections page after a scan and connection have already completed this session
- **THEN** the view shows those lines, not an empty box

#### Scenario: The view follows new lines

- **WHEN** a qualifying event is recorded while the view is on screen
- **THEN** the line is appended to the view without the user acting and without a periodic refresh

#### Scenario: The view survives leaving and returning

- **WHEN** the user navigates away from the Connections page and returns within the same session
- **THEN** the view again shows the session's qualifying lines, including those recorded while the page was closed

#### Scenario: A previous session's lines are excluded

- **WHEN** the persisted log contains earlier sessions
- **THEN** neither view shows their lines

#### Scenario: Developer detail is absent from the view

- **WHEN** the subsystem is logging DEBUG-tier detail during a connection
- **THEN** the view shows only the INFO-and-above narrative, while the DEBUG lines remain in the system log

### Requirement: Clearing a view does not destroy the log

A view's clear action SHALL affect only what that view displays. It SHALL NOT delete, truncate or rewrite the system log, and SHALL NOT affect the other view.

After clearing, the view SHALL continue to append newly recorded qualifying lines.

#### Scenario: Clear hides only the view's contents

- **WHEN** the user clears the scale view
- **THEN** the scale view is empty, the DE1 view is unchanged, and the system log still contains every line

#### Scenario: The view keeps following after a clear

- **WHEN** the user clears a view and a qualifying event is then recorded
- **THEN** the new line appears in that view

### Requirement: Sharing from the connections page shares the system log

The Connections page share action SHALL export the system log — the artifact support asks users for — rather than any subsystem-specific log.

#### Scenario: Share hands over the collected artifact

- **WHEN** the user activates the share action on the Connections page
- **THEN** the system log is offered for sharing

#### Scenario: The removed artifact is not offered

- **WHEN** the user looks for a way to share a scale-only log
- **THEN** no such action exists, because no such file is produced
