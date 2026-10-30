## Purpose

Runs a recipe that adds hot water to espresso as one drink: the user starts the first step and the app requests the second when the first ends.

## ADDED Requirements

### Requirement: Which recipes run as a two-step drink
A recipe SHALL run as a two-step drink exactly when it has a profile, a hot-water block with `hasWater` true, and no milk block. The block's `order` SHALL set the steps: `after` is espresso then hot water, `before` is hot water then espresso. No setting, stored drink type, firmware version or GHC state SHALL affect this.

#### Scenario: An americano recipe qualifies
- **WHEN** the active recipe has a profile, a hot-water block ordered `after` and no milk block
- **THEN** it runs as espresso followed by hot water

#### Scenario: A long black recipe qualifies
- **WHEN** the active recipe has a profile, a hot-water block ordered `before` and no milk block
- **THEN** it runs as hot water followed by espresso

#### Scenario: Milk and water together are not sequenced
- **WHEN** the active recipe has both a milk block and a hot-water block
- **THEN** no second step is requested after either operation

#### Scenario: Hot-water tea is not sequenced
- **WHEN** the active recipe is profile-less with a hot-water block
- **THEN** no espresso is requested after its hot water

### Requirement: Starting the recipe starts its first step
Starting a qualifying recipe from the app (a tap on its selected pill, or the Espresso control while it is selected) SHALL start the recipe's first step: espresso for an americano, hot water for a long black. Controls that name one operation (the Hot Water control, MCP `machine_start`) SHALL start that operation.

#### Scenario: A long black starts with hot water
- **WHEN** the user taps the selected long black recipe's pill
- **THEN** the app requests hot water, not espresso

#### Scenario: An americano starts with espresso
- **WHEN** the user taps the selected americano recipe's pill
- **THEN** the app requests espresso

### Requirement: The second step is requested when the first ends
An americano SHALL be armed when its espresso starts flowing with the recipe active, however it was started. A long black SHALL be armed only on hot water its recipe tap started, so other hot water with that recipe active is not followed by a shot. The app SHALL request the second step once, at the later of the shot's settled end (americano only) and 4 seconds after the machine reports Idle, with no firmware or GHC check and no retry. The second step SHALL NOT arm a further step.

#### Scenario: Americano hot water follows the shot
- **GIVEN** an active americano recipe
- **WHEN** an espresso that started pouring has settled and the machine has reported Idle 4 seconds ago
- **THEN** the app requests hot water once

#### Scenario: Long black espresso follows the hot water
- **GIVEN** an active long black recipe
- **WHEN** hot water that started pouring ends and the machine has reported Idle 4 seconds ago
- **THEN** the app requests espresso once

#### Scenario: The water waits for a shot that settles late
- **GIVEN** an americano whose espresso has not settled 4 seconds after Idle
- **WHEN** the shot settles
- **THEN** the app requests hot water then, not before

#### Scenario: A GHC-started first step still sequences
- **GIVEN** an active americano recipe on a machine whose GHC is active
- **WHEN** the user starts the espresso from the GHC and it ends
- **THEN** the app requests hot water

#### Scenario: Hot water for tea with a long black selected
- **GIVEN** an active long black recipe
- **WHEN** the user starts hot water from the Hot Water control, not the recipe
- **THEN** no espresso is requested when the hot water ends

#### Scenario: Refused request is not retried
- **WHEN** the machine does not enter the second step after the request
- **THEN** the app sends no further request for that drink and the machine stays as the firmware left it

#### Scenario: A shot that never poured is not followed
- **WHEN** an americano's espresso is stopped before it starts pouring
- **THEN** no hot water is requested

### Requirement: The second step can be skipped before it starts
While the first step runs, its operation page SHALL show a Skip button naming the step that follows ("Skip" with the water or espresso icon; accessible name "Skip water" or "Skip espresso"). Skip SHALL drop the sequence for this drink only; the recipe stays active and the next drink sequences normally. The Skip control SHALL meet the project's accessibility rules for interactive elements.

#### Scenario: Skipping hot water on an americano
- **WHEN** the user taps Skip during the americano's espresso
- **THEN** no hot water is requested when the shot ends

#### Scenario: Skipping espresso on a long black
- **WHEN** the user taps Skip during the long black's hot water
- **THEN** no espresso is requested when the hot water ends

#### Scenario: Skip while waiting after the first step
- **WHEN** the user taps Skip after the first step has ended, before the second is requested
- **THEN** that step is not requested
- **AND** the next drink with the same recipe sequences normally

### Requirement: A sequence ends with its drink
An armed sequence SHALL be dropped when the machine goes anywhere other than Idle at the end of the first step (sleep, disconnect, or another operation).

#### Scenario: Disconnect drops the sequence
- **WHEN** the DE1 disconnects during an americano's espresso
- **THEN** no hot water is requested after reconnecting

### Requirement: The drink stays on screen as one operation
While a sequence is active, the first step's page SHALL stay up: it SHALL NOT show its completion message, leave, or open the post-shot review. The review SHALL open after the second step ends. If the second step is skipped, dropped, or has not started 3 seconds after its request (the DE1 sends no refusal), the first step's page SHALL complete as it does for a single operation.

#### Scenario: Americano without cup view
- **WHEN** an americano's espresso ends
- **THEN** the Espresso page stays up until the hot water starts, the Hot Water page replaces it, and the post-shot review opens when the water ends

#### Scenario: Refused second step
- **WHEN** the machine has not started the hot water 3 seconds after the request
- **THEN** the Espresso page leaves or opens the review as it does for a plain espresso

### Requirement: Cup view shows the whole drink
When the Espresso page shows the cup, an americano or long black SHALL be shown on the Espresso page for both steps. Hot water SHALL NOT open the Hot Water page. Water entering the cup SHALL be drawn as a lighter stream than coffee, the cup SHALL be drawn half coffee and half water, each half filling with its step's progress, and the liquid SHALL lighten as water is added. Stop on that page SHALL stop whichever step is running.

#### Scenario: Americano in cup view
- **WHEN** an americano's espresso ends with the cup view selected
- **THEN** the cup keeps the coffee in its lower half, a pale water stream fills the upper half, and no Hot Water page opens

#### Scenario: Long black in cup view
- **WHEN** a long black starts with the cup view selected
- **THEN** the Espresso page opens for the hot water, the cup fills with water, and the espresso pours into the same cup
