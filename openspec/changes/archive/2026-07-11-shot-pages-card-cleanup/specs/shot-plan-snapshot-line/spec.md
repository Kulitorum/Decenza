## ADDED Requirements

### Requirement: Shot pages show a Shot Plan snapshot line
The Shot Detail and Shot Review pages SHALL display, directly beneath the title line (`<profile-or-recipe name> · <date>`), a single Shot Plan snapshot line rendering this shot's frozen dial-in as a sentence (e.g. `18.0g in · 42.0g · 88°C · Yemen West Haraz · grind 25 · 1400 rpm`), so the user can read a shot's key data at a glance without scrolling — including while swiping between shots to compare them.

#### Scenario: Shot with a full snapshot
- **WHEN** the user opens a shot whose record has dose, yield, temperature, bean, grind, and RPM
- **THEN** the snapshot line renders those values as a Shot Plan sentence beneath the title

#### Scenario: Comparing shots by swipe
- **WHEN** the user swipes from one shot to the next on the Shot Detail page
- **THEN** each shot's snapshot line updates to that shot's own values with the graph, requiring no scroll to read the key data

### Requirement: The snapshot line mirrors the user's Shot Plan field configuration
The snapshot line SHALL take its field selection and order from the user's own Shot Plan widget (the first `shotPlan` widget in the idle layout), so it shows the fields they configured — falling back to the canonical default set when no Shot Plan widget exists. It SHALL always omit `profile` and `temperature`, which the title line above already shows, and it SHALL render as a plain fragment list regardless of the widget's sentence/stacked toggles.

#### Scenario: User configured which fields show
- **WHEN** the user has added or removed fields on their Shot Plan widget (e.g. dropped the bean, added roast date)
- **THEN** the shot-page snapshot line shows the same field selection and order (minus profile and temperature)

#### Scenario: No Shot Plan widget in the layout
- **WHEN** the user's idle layout contains no Shot Plan widget
- **THEN** the snapshot line falls back to the canonical default fields (dose/yield, bean, grind), still omitting profile and temperature

### Requirement: RPM shows only for grinders that report it
The snapshot line (and the recipe/bean grind lines) SHALL show RPM only when the shot's grinder is RPM-capable (`grinderRpmCapable`) and a positive RPM was recorded, so a spurious recorded RPM for a non-RPM grinder never surfaces. This SHALL be consistent across the Shot Detail and Shot Review pages.

#### Scenario: Non-RPM grinder with a recorded RPM
- **WHEN** a shot's grinder is not RPM-capable (e.g. a Niche Zero) yet the record carries a non-zero RPM
- **THEN** neither snapshot line nor the owning grind card shows any RPM

### Requirement: The snapshot renders the shot's frozen values, not the live dial
The snapshot line SHALL be driven by the opened shot's snapshot fields (profile name, dose, yield/target, temperature, roaster, coffee, grind setting, RPM, roast date), never by the currently active bag, profile, or grinder. Changing the live dial after a shot is saved SHALL NOT alter that shot's snapshot line.

#### Scenario: Live dial differs from the shot
- **WHEN** the active bag and profile now differ from those used for the opened shot
- **THEN** the snapshot line shows the shot's recorded values, unaffected by the current live dial

#### Scenario: Field absent from the shot record
- **WHEN** the opened shot has no recorded value for a snapshot field (e.g. no grind, or no RPM)
- **THEN** that field is omitted from the sentence and the remaining fields render without a dangling separator

### Requirement: The snapshot line is accessible
The snapshot line SHALL expose its content to assistive technology as a single readable sentence, with inner decorative items ignored, and all its user-visible text internationalized.

#### Scenario: Screen reader reads the snapshot
- **WHEN** a screen reader focuses the snapshot line
- **THEN** it announces one coherent sentence of the shot's key values rather than separate fragments
