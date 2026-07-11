## MODIFIED Requirements

### Requirement: Override indicators

The Shot Plan text SHALL highlight only the individual overridden item(s), not the whole sentence: when a deliberate temperature override is active (the override differs from the profile temperature by more than 0.1°C) the temperature item SHALL render in the override-highlight color; when a deliberate yield override is active (the target differs from the profile yield by more than 0.1 g) the yield item SHALL render as "{profileYield} → {target}g" in the override-highlight color. Every other part of the sentence, and the plan icon, SHALL remain the default text color. The override-highlight color SHALL be `Theme.highlightColor` — the same color the Brew Settings values use for an override — so the two surfaces read consistently. Natural drift between measured dose and profile dose SHALL NOT trigger either indicator.

Because the highlight lives in the shared `ShotPlanText` component, it SHALL apply on **every** surface that renders the Shot Plan — the idle home widget, recipe cards, the shot-detail and post-shot-review snapshot lines, and the screensaver preview — not only the idle page. Each surface's highlight SHALL be driven by **that surface's own** override inputs (`tempOverridden`/`overrideTemp`/`profileTemp` and `yieldOverridden`/`targetWeight`/`profileYield`): the live dial for the home widget, the recipe's stored values for recipe cards, and the shot's frozen snapshot values for the shot-detail and post-shot-review lines. A frozen-shot surface SHALL NOT reflect the live dial's override state; if it does not render the temperature or yield item, that item simply shows no highlight (it does not borrow the live override).

#### Scenario: Highlight appears on non-idle surfaces
- **WHEN** a recipe card (or a shot snapshot line) renders a plan whose recipe/shot carries a yield or temperature override
- **THEN** that overridden item is highlighted using the same per-item scheme as the idle widget, driven by the recipe's/shot's own values

#### Scenario: Frozen surfaces do not borrow the live override
- **WHEN** a live temperature override is active and the user opens a historical shot whose snapshot had no override
- **THEN** the shot-detail plan line shows no highlight (it reflects the shot's frozen state, not the live dial)

#### Scenario: Temperature override highlights only the temperature item
- **WHEN** a deliberate temperature override is active
- **THEN** the temperature item renders in `Theme.highlightColor`
- **AND** the rest of the sentence and the icon remain the default text color

#### Scenario: Yield override shows the arrow, highlighted
- **WHEN** the user dials a yield override of 40.0 g on a profile whose default yield is 36.0 g
- **THEN** the plan shows "36.0 → 40.0g" and that yield fragment renders in `Theme.highlightColor`
- **AND** the rest of the sentence remains the default text color

#### Scenario: No override, no arrow, no highlight
- **WHEN** no yield or temperature override is set
- **THEN** the plan shows the single target weight with no arrow, and the whole sentence renders in the default text color

#### Scenario: Natural dose drift does not highlight
- **WHEN** the measured dose differs from the profile dose but no deliberate override flag is set
- **THEN** no item is highlighted
