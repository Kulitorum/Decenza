# layout-widget-instance-config — Delta

## MODIFIED Requirements

### Requirement: Shot plan display option set

The `shotPlan` widget type SHALL expose, in both editors (in-app popup and web layout editor):

- An **ordered display-item list** (`shotPlanItems`): a JSON array of item keys drawn from `doseYield`, `profile`, `temperature`, `roaster`, `coffee`, `grind`, `roastDate`. The list defines both which items are shown and their order. Profile and Temperature SHALL be independent items.
- A **Sentence style** boolean (`shotPlanSentence`, default ON) selecting sentence vs fragment rendering.
- A **Stacked details** boolean (`shotPlanStacked`, default OFF) that, in sentence mode, moves the detail tail onto its own line(s); the in-app toggle SHALL be disabled while Sentence style is OFF (the option has no meaning for fragments).
- A **Steam plan** boolean (`shotPlanShowSteamPlan`, default ON) gating the page-aware steam swap, unchanged.

The default item list SHALL be `["doseYield", "profile", "temperature", "roaster", "coffee", "grind"]` (Roast date not shown by default, matching the only legacy toggle that defaulted OFF), which — together with Sentence style ON — SHALL reproduce the widget's previous default rendering.

**In-app editor**: the Shot Plan settings popup SHALL present the item list as a "Shown" row of chips (drag to reorder, with an explicit remove affordance per chip) and an "Available" row of the unused items (activate to add), plus the two toggles. Reordering SHALL have an accessible fallback (per-chip move controls) when a screen reader is active. Edits SHALL apply only on Save; Cancel SHALL discard them. The popup SHALL show a live preview of the plan as configured.

**Web editor**: the web layout editor SHALL present the same item list with add, remove, and reorder controls plus the two toggles, reading and writing the same keys.

**Migration**: derivation from legacy booleans SHALL apply only when the `shotPlanItems` property is absent — a stored empty list is a valid "show nothing" configuration and SHALL be honored, not treated as unset. When an instance has no `shotPlanItems` property, both editors and the widget SHALL derive the list from the legacy booleans in canonical order — `shotPlanShowDoseYield` → `doseYield`; `shotPlanShowProfile` → `profile` **and** `temperature`; `shotPlanShowRoaster` → `roaster`; `shotPlanShowCoffee` → `coffee`; `shotPlanShowGrind` → `grind`; `shotPlanShowRoastDate` (default OFF) → `roastDate` — honoring each legacy default. The legacy display booleans SHALL be read but never written by the new editors. Both editors and the C++ configurable-type gate SHALL accept the same keys so a configuration set in one editor round-trips through the other.

#### Scenario: Defaults reproduce the previous rendering

- **WHEN** a fresh `shotPlan` instance is added and never configured
- **THEN** it renders the sentence with dose & yield, profile, temperature, roaster, coffee, and grind — identical content to the pre-change default widget

#### Scenario: Legacy booleans derive the item list

- **WHEN** a saved layout has `shotPlanShowRoaster: false`, `shotPlanShowGrind: false`, and no `shotPlanItems`
- **THEN** the widget shows dose & yield, profile, temperature, and coffee in canonical order, and the editor opens with exactly those chips in the Shown row

#### Scenario: Legacy profile boolean expands to two chips

- **WHEN** a saved layout has `shotPlanShowProfile: true` and no `shotPlanItems`
- **THEN** the derived Shown list contains both the Profile and Temperature chips

#### Scenario: Reorder persists per instance

- **WHEN** a user drags the Grind chip to the front of the Shown row and saves
- **THEN** that instance's `shotPlanItems` starts with `grind`, the widget renders grind first, and the order survives an app restart
- **AND** other `shotPlan` instances are unaffected

#### Scenario: Remove and re-add via the Available row

- **WHEN** a user removes the Coffee chip from the Shown row
- **THEN** Coffee appears in the Available row and the widget no longer shows the coffee name after Save
- **WHEN** the user later activates Coffee in the Available row
- **THEN** it is appended to the Shown row

#### Scenario: An emptied item list stays empty

- **WHEN** a user removes every chip from the Shown row and saves
- **THEN** the instance stores an empty `shotPlanItems` list, the widget renders nothing, and reopening either editor shows an empty Shown row — the legacy booleans do not resurrect the default items

#### Scenario: Stacked details round-trips and gates on Sentence style

- **WHEN** a user enables Stacked details with Sentence style ON and saves
- **THEN** the instance stores `shotPlanStacked: true`, the widget renders the tail below the sentence, and the other editor shows the option enabled
- **WHEN** Sentence style is toggled OFF in the in-app editor
- **THEN** the Stacked details toggle is disabled

#### Scenario: Cancel discards chip edits

- **WHEN** a user reorders and removes chips, then taps Cancel
- **THEN** the instance's stored configuration and rendering are unchanged

#### Scenario: Options round-trip between editors

- **WHEN** the item order is changed and Sentence style turned off in the web editor
- **THEN** the in-app editor shows the same chip order and toggle state, and the widget renders fragments in that order

#### Scenario: New editors never write legacy display keys

- **WHEN** either editor saves a shot-plan configuration
- **THEN** only `shotPlanItems`, `shotPlanSentence`, and `shotPlanShowSteamPlan` are written; the `shotPlanShow*` display booleans are not modified

#### Scenario: Accessible reorder fallback

- **WHEN** a screen reader is active and the Shot Plan settings popup is open
- **THEN** each Shown chip exposes move controls that reorder it without drag gestures
