## ADDED Requirements

### Requirement: A narrative line reports the action taken, not the action attempted

A device subsystem line at INFO or above SHALL describe what the subsystem actually did.
Where a step can fall back — a resolution that fails onto a cached value, a transport that
fails onto another transport — the narrative SHALL make the taken branch visible at INFO,
either by deferring the line until the branch is known or by logging the fallback at the
same tier as the attempt.

A line SHALL NOT state an intention in the past tense when the following lines will show
its failure only at DEBUG. The resulting narrative is worse than silence: it reads as a
complete account, and the reader draws the wrong conclusion with no signal that anything
is missing.

#### Scenario: A fallback is visible without dropping to DEBUG

- **WHEN** a subsystem announces an action at INFO, that action fails, and it proceeds via
  a fallback
- **THEN** the fallback and the value it used are recorded at INFO, so a reader at INFO can
  tell which path ran

#### Scenario: A stale cached value is attributable

- **WHEN** a driver dials a remembered address because a fresh lookup produced nothing
- **THEN** the narrative at INFO names the address dialled and says the lookup failed,
  rather than describing only the lookup that was attempted

#### Scenario: The narrative is not contradicted by the DEBUG tier

- **WHEN** a subsystem's INFO lines are read alongside the same session's DEBUG lines
- **THEN** the DEBUG detail elaborates the INFO account rather than reversing it

### Requirement: Repeat suppression covers a whole failing cycle, not one emitter

Where a subsystem suppresses a repeating failure — warning for the first few occurrences
and then dropping to DEBUG — that suppression SHALL apply to every line the failing cycle
emits, across the manager, the driver and the transport alike.

Partial suppression SHALL be treated as a defect rather than a partial improvement. When
the counted lines go quiet and the uncounted ones do not, the reader is left with a
repeating fragment carrying neither the attempt number nor the outcome: noisier than
suppressing nothing and less informative than suppressing everything.

Suppression SHALL remain counted per distinct message, so that a genuinely new failure
arriving mid-run is not silenced by an unrelated one having spent the budget.

#### Scenario: A repeating cycle quiets as a whole

- **WHEN** a reconnect cycle repeats past its warn budget
- **THEN** every line that cycle emits — from the manager, the driver and the transport —
  drops to DEBUG together

#### Scenario: The retained record still proves the ladder is running

- **WHEN** a suppressed cycle repeats
- **THEN** the DEBUG tier still records each attempt with its repeat count, so a reader can
  confirm the ladder did not stop

#### Scenario: A new failure is still loud

- **WHEN** a different failure occurs while an existing one is suppressed
- **THEN** the new failure is warned about on its own budget

### Requirement: A state-change line is emitted only when the state changed

A device subsystem SHALL NOT log a transition, selection or fallback that did not occur.
A line announcing a change SHALL be guarded on the state it claims to be reporting, so a
selection already in force is not announced as though it had just been made.

This applies to fallbacks in particular: announcing a fallback to a component that has been
active since startup reads as the app having just lost the real device.

#### Scenario: An already-active fallback is not announced

- **WHEN** a connection attempt fails while the fallback component has been in use since
  startup
- **THEN** no line claims the app is switching to that fallback

#### Scenario: A genuine change is still announced

- **WHEN** a connection is lost and the app genuinely switches to the fallback component
- **THEN** the change is recorded at INFO or above
