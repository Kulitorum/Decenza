## ADDED Requirements

### Requirement: A Tool Reports The Outcome, Not The Attempt

A tool that performs an operation SHALL report success only when the operation
actually took effect. Where the underlying app-layer call can report its own
outcome, the tool SHALL consult it; where the call currently cannot, the call
SHALL be given a way to report it rather than the tool assuming success.

An outcome SHALL be determined from what the operation did, not from a
pre-flight check performed before it — a check taken before the write races the
write and costs a second query.

#### Scenario: A profile the manager refuses

- **WHEN** `profiles_set_active` names a profile the profile manager refuses to load, leaving the previously active profile in place
- **THEN** the tool result reports the failure and names the profile that was refused

#### Scenario: A profile that loads

- **WHEN** `profiles_set_active` names a profile that loads
- **THEN** the tool result reports success, unchanged from prior behaviour

#### Scenario: Updating a shot that does not exist

- **WHEN** `shots_update` names a shot ID matching no stored shot
- **THEN** the tool result reports the failure rather than success

#### Scenario: Deleting a shot that does not exist

- **WHEN** `shots_delete` names a shot ID matching no stored shot
- **THEN** the tool result reports the failure rather than success

#### Scenario: A theme name that matches nothing

- **WHEN** `apply_theme` names a theme that matches no preset
- **THEN** the tool result reports the failure and does not claim the theme was applied

### Requirement: An Asynchronous Tool Answers On Every Terminal Outcome

A tool that responds when a signal fires SHALL be connected to every signal that
can terminate the operation it started, including its failure signals, and SHALL
respond exactly once. A terminal outcome SHALL NOT leave the request
unanswered.

#### Scenario: The deletion fails in storage

- **WHEN** `shots_delete` is called and the storage layer reports a failure instead of completing the deletion
- **THEN** the client receives a failed tool result rather than no response

#### Scenario: The deletion succeeds

- **WHEN** the storage layer completes the deletion
- **THEN** the client receives exactly one response, reporting success

### Requirement: An Unavailable Dependency Is Reported As An Error

When a tool cannot run because a dependency it needs is unavailable, its result
SHALL carry an `error` naming the unavailable dependency. It SHALL NOT return an
empty result, a default-constructed payload, or an empty-string enum value in
place of that error.

A payload field that already distinguishes a meaningful "no data yet" state
SHALL keep that meaning and SHALL NOT be reused to signal unavailability.

#### Scenario: Profile manager unavailable

- **WHEN** a profile read tool runs while the profile manager is unavailable
- **THEN** the result carries an `error` naming the unavailable dependency, rather than an empty object

#### Scenario: Steam tracker unavailable

- **WHEN** `steam_get_health` runs while the steam tracker is unavailable
- **THEN** the result carries an `error`, and does not report a health status of empty string

#### Scenario: Steam tracker available with no sessions

- **WHEN** `steam_get_health` runs with the tracker available but no steam sessions recorded
- **THEN** the result reports that state as data-not-yet-available, distinctly from the tracker being unavailable

### Requirement: Inputs That Did Not Resolve Are Named

A tool that accepts a list of identifiers and silently omits the ones it could
not resolve SHALL instead name the unresolved identifiers in its result, so a
caller does not have to infer the loss by comparing counts. When no identifier
resolves, the result SHALL be an error, because the requested operation did not
happen at all.

#### Scenario: Some shot IDs do not resolve

- **WHEN** `shots_compare` is given several shot IDs and only some resolve
- **THEN** the result compares the resolved shots and separately names each unresolved ID

#### Scenario: No shot ID resolves

- **WHEN** `shots_compare` is given shot IDs and none resolve
- **THEN** the result carries an `error` rather than an empty comparison

### Requirement: A No-Op Is Distinguishable From A Performed Operation

When a tool is asked to perform an operation whose requested end state already
holds, its result SHALL report success — the caller got what it asked for — and
SHALL say that nothing was done in a machine-readable field rather than only in
prose. It SHALL preserve the parameters the caller supplied.

A result carrying neither a success indicator nor an `error` SHALL NOT be used
to signal this state, because it is a third state the caller cannot classify.

#### Scenario: Already connected

- **WHEN** `devices_connect_de1` is called while the requested device is already connected
- **THEN** the result reports success, flags the no-op in a machine-readable field, and still reports which device the call referred to

#### Scenario: Nothing to disconnect

- **WHEN** `mqtt_disconnect` is called while MQTT is not connected
- **THEN** the result reports success and flags the no-op in a machine-readable field, rather than returning a bare message with neither success nor error

### Requirement: A Tool Reports Unsupported Device Capabilities

A tool that drives a device capability SHALL determine whether the connected
device implements that capability and SHALL report an error when it does not. A
default implementation that does nothing SHALL NOT be reported as success.

The capability SHALL default to unsupported, so that a device whose driver has
not declared it reports an error rather than a false success.

#### Scenario: Timer on a scale without timer support

- **WHEN** a scale timer tool is called on a connected scale whose driver does not implement the timer
- **THEN** the result carries an `error` stating the scale does not support the timer

#### Scenario: Timer on a scale with timer support

- **WHEN** the same tool is called on a scale whose driver implements the timer
- **THEN** the result reports success, unchanged from prior behaviour
