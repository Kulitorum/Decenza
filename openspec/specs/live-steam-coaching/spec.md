# live-steam-coaching Specification

## Purpose
Local, real-time coaching cues while steaming milk on the DE1 — an on-screen
banner and optional spoken voice (stretch → roll → almost → done). Entirely
on-device: no AI calls, no network, no DB in the hot path. Created by archiving
change refine-live-steam-coaching.
## Requirements
### Requirement: Feature is optional and off by default

The live steam coaching feature SHALL be disabled by default. It SHALL be controlled by two independent user settings — a visual banner toggle and a spoken audio toggle — each of which SHALL default to off. Enabling one SHALL NOT implicitly enable the other. Changes to either setting SHALL take effect on the next steam operation without an application restart. (The settings UI is unreachable while steaming — leaving the steam page stops steam — so no mid-operation toggle behavior is specified.)

#### Scenario: Fresh install shows no coaching

- **WHEN** a user with no prior configuration starts a steam operation
- **THEN** no coaching banner is shown and no coaching cue is spoken

#### Scenario: Visual toggle does not enable audio

- **WHEN** the user enables the visual steam-coaching setting but leaves the audio setting off
- **AND** a steam operation runs
- **THEN** the banner is shown for each milestone cue
- **AND** no coaching cue is spoken

#### Scenario: Audio toggle does not require the visual banner

- **WHEN** the user enables the audio steam-coaching setting but leaves the visual setting off
- **AND** a steam operation runs
- **THEN** coaching cues are spoken at their milestones
- **AND** no coaching banner is shown

#### Scenario: Toggle takes effect without restart

- **WHEN** the user enables either coaching setting and then starts a steam operation
- **THEN** the corresponding coaching output is active for that operation without restarting the app

### Requirement: Spoken cues are a service, independent of the visual banner and the accessibility master switch

Spoken steam-coaching cues SHALL be produced by the coaching service itself whenever the audio steam-coaching setting is enabled — not by the visual banner component — and regardless of the state of the general accessibility / screen-reader master switch. The audio path SHALL NOT be silently suppressed by any unrelated accessibility preference, and SHALL NOT depend on the visual banner being enabled.

#### Scenario: Audio works with the accessibility master switch off

- **WHEN** the audio steam-coaching setting is on
- **AND** the general accessibility master switch is off (its default)
- **AND** a steam operation reaches a spoken milestone
- **THEN** the cue is spoken

#### Scenario: No spoken cue is dropped without a diagnostic

- **WHEN** a cue is marked to be spoken but the audio path cannot produce speech
- **THEN** the reason is recorded to the debug log rather than being silently discarded

### Requirement: Completion cue is driven by the actual steam-end event

The end-of-steam cue SHALL be triggered from the machine event that ends steam flow (the transition that stops the steam timer), not from a predicted elapsed-time window, and SHALL be phrased as a completion notification rather than an instruction. It SHALL be delivered exactly once per steam operation and SHALL NOT be missable due to drift between the local clock and the machine's own countdown. The anticipatory "almost" heads-up MAY be derived from the predicted remaining time.

#### Scenario: Completion fires even when the local clock lags the machine

- **WHEN** steam flow ends at the machine's target but before the local elapsed time reaches the predicted end
- **THEN** the completion cue is still delivered from the steam-end event

#### Scenario: Completion fires exactly once

- **WHEN** a single steam operation completes
- **THEN** the completion cue is delivered exactly once, with no duplicate on the frozen post-stop clock

#### Scenario: Early manual stop is silent

- **WHEN** the user stops steam well before the target duration (outside the final anticipation window)
- **THEN** no completion cue is delivered — a deliberate abort needs no announcement

### Requirement: Milestone cues are never rate-limited away and persist until superseded

Each milestone cue (stretch, roll, almost, completion) SHALL fire at most once per steam operation via one-shot latching, and that latching SHALL be the only rate control — no additional spacing governor SHALL suppress a distinct milestone cue. On short steams every applicable milestone SHALL still be delivered. On the visual banner each cue SHALL remain visible until the next cue replaces it or the steam operation ends — cues SHALL NOT self-dismiss on a timer (easy to miss).

#### Scenario: Short steam still warns before stopping

- **WHEN** a steam operation is short enough that the "almost" cue falls shortly after the opening "stretch" cue
- **THEN** the "almost" cue is still delivered
- **AND** the completion cue is still delivered

### Requirement: Coaching requires a milk-derived duration

Coaching cues SHALL be emitted only when the session's steam duration was derived from the actual milk weight (weight-timed steaming enabled, calibrated, and the milk captured on the scale this session). A fixed preset duration says nothing about the milk in the pitcher, and pacing cues off it would endorse ruining the milk (e.g. 200 mL against a 60 s preset is destroyed long before "almost"). When coaching is enabled but the duration is not milk-derived, the coach SHALL emit a single informational pill explaining why ("no coaching — milk weight not captured"), on the visual banner only — it SHALL NOT be spoken — and SHALL otherwise behave as if the feature were off. Untimed/manual steams are never milk-derived.

#### Scenario: Captured milk enables full coaching

- **WHEN** weight-timed steaming scaled this session's duration from captured milk
- **AND** a coaching setting is on
- **THEN** the milestone cues are delivered normally

#### Scenario: No captured milk yields only the explanatory pill

- **WHEN** coaching is enabled but the session's duration was not derived from a captured milk weight (weight-timed off, calibrated-but-not-captured, manual duration override, or untimed steam)
- **THEN** a single informational pill explains that coaching is off because the milk weight was not captured
- **AND** no milestone or completion cue is emitted
- **AND** nothing is spoken

#### Scenario: Feature off shows no pill

- **WHEN** both coaching settings are off
- **THEN** no pill and no cue of any kind is shown

### Requirement: Milestone pacing and manual-steam behavior

During steaming (with a milk-derived duration) the coach SHALL emit at most one cue at a time in the order stretch → roll → almost → completion, each at most once per operation. When the target duration is too short for a distinct texturing phase, the "roll" cue SHALL be skipped while the stretch, almost, and completion cues are retained. When both coaching settings are off, the coach SHALL perform no per-sample evaluation work.

#### Scenario: Very short steam skips the roll cue

- **WHEN** a steam operation's target duration is below the minimum needed for a texturing phase
- **THEN** the roll cue is not emitted
- **AND** the stretch, almost, and completion cues are still emitted

#### Scenario: State resets between operations

- **WHEN** a second steam operation begins after a first has ended
- **THEN** all milestone latches are cleared so the second operation is coached from its own start
- **AND** any lingering cue from the first operation is cleared

#### Scenario: Disabled coach does no work

- **WHEN** both the visual and audio steam-coaching settings are off
- **AND** a steam operation runs
- **THEN** the coach performs no per-tick milestone evaluation
