# plan-widgets — Delta

## MODIFIED Requirements

### Requirement: Shot plan sentence content follows the display toggles

The Shot Plan text (used by the `shotPlan` widget) SHALL render the display items named by the instance's ordered item list (`shotPlanItems`), in list order, in one of two formats selected by the instance's Sentence style option (`shotPlanSentence`, default ON).

The seven display items are: Profile (`profile`), Temperature (`temperature`), Roaster (`roaster`), Coffee (`coffee`), Grind (`grind`), Roast date (`roastDate`), and Dose & yield (`doseYield`). Each item SHALL contribute exactly its named content, and an item absent from the list SHALL contribute nothing:

- **Profile** (`profile`): the profile name.
- **Temperature** (`temperature`): the brew temperature (following the C/F display unit and the temperature-override rendering).
- **Roaster** (`roaster`): the roaster brand only.
- **Coffee** (`coffee`): the coffee (bean) name only.
- **Grind** (`grind`): the grinder setting, plus the grinder RPM when one is recorded (`Settings.dye.dyeGrinderRpm` > 0).
- **Roast date** (`roastDate`): the roast date.
- **Dose & yield** (`doseYield`): the dose ("{dose} in") and the target output weight.

**Fragment format** (Sentence style OFF): every present item's segment SHALL be joined with the standard separator in item-list order.

**Sentence format** (Sentence style ON): when the Profile item is present with a profile name available, the sentence scaffold SHALL consume the Dose & yield, Profile, and Temperature items (wherever they sit in the list) together with the beverage word, and all other present items SHALL trail after the sentence as a separator-joined tail in item-list order. With the Stacked details option (`shotPlanStacked`, default OFF) enabled, the tail SHALL render on its own line(s) below the sentence instead of trailing after a separator — on the display path only: the accessibility string SHALL remain one separator-joined sentence, and compact (bar) placements SHALL ignore the option. The scaffold SHALL degrade by what is present/available:

- yield + profile + temperature → "Brew {yield} of {beverage}, using {profile} at {temperature}"
- profile + temperature, no yield → "Brew {beverage}, using {profile} at {temperature}"
- yield + profile, Temperature item absent → "Brew {yield} of {beverage}, using {profile}"
- profile only → "Brew {beverage}, using {profile}"
- Profile item absent (or no profile name available) → the sentence has no anchor; the text SHALL render in fragment format regardless of the Sentence style option.

The beverage word SHALL follow the current profile's `beverage_type`: "Espresso" for espresso (and unset), "tea" for tea types, and "coffee" for any other coffee beverage (filter, pourover, …). A cleaning or descale profile SHALL replace the plan entirely — in both formats — with a cleaning notice carrying a do-not-load-coffee warning, rendered bold in the error (red) color with no item segments.

The plain (accessibility) text and the rich (displayed) text SHALL be produced by one shared builder so their content cannot drift.

#### Scenario: Fragment format follows chip order

- **WHEN** Sentence style is OFF and the item list is `["grind", "coffee", "doseYield"]`
- **THEN** the plan renders "grind {setting} · {coffee} · {dose}g in · {yield}g" — the grind segment first, in exactly the configured order

#### Scenario: Stacked details move the tail to its own line

- **WHEN** Sentence style and Stacked details are both ON and the item list includes trailing items
- **THEN** the widget displays the sentence on the first line and the separator-joined tail on the line(s) below it, while a screen reader still hears one joined sentence

#### Scenario: Sentence consumes its core items and the tail keeps user order

- **WHEN** Sentence style is ON and the item list is `["roastDate", "doseYield", "profile", "temperature", "grind", "coffee"]`
- **THEN** the plan renders "Brew {yield} of Espresso, using {profile} at {temperature}" followed by the tail "roasted {date} · grind {setting} · {coffee}" — the tail in list order, the scaffold in template order

#### Scenario: Temperature chip removed drops the at-clause

- **WHEN** Sentence style is ON and the item list contains `profile` and `doseYield` but not `temperature`
- **THEN** the sentence reads "Brew {yield} of {beverage}, using {profile}" with no temperature anywhere

#### Scenario: Profile chip removed falls back to fragments

- **WHEN** Sentence style is ON and the item list is `["doseYield", "temperature", "coffee"]` (no `profile`)
- **THEN** the plan renders as separator-joined fragments in list order, with no sentence scaffolding

#### Scenario: Filter profile says coffee, not espresso

- **WHEN** the current profile's beverage_type is "filter" (or "pourover")
- **THEN** the sentence reads "Brew {yield} of coffee, using {profile} at {temperature}"

#### Scenario: Profile without a target weight keeps the beverage word

- **WHEN** the current profile has no target weight (e.g. a filter or tea profile with no stop-at-weight) but the Profile and Temperature items are present with values
- **THEN** the plan reads "Brew {beverage}, using {profile} at {temperature}", not a fragment list without the beverage word

#### Scenario: Cleaning profile warns instead of planning

- **WHEN** the current profile's beverage_type is "cleaning" or "descale"
- **THEN** the widget shows a cleaning notice including a warning not to put coffee in the portafilter, with no item segments, regardless of the Sentence style option

#### Scenario: Grind item shows grind and RPM

- **WHEN** the `grind` item is present, the grinder setting is "2.5", and the recorded RPM is 90
- **THEN** the plan's grind segment shows the grind setting and "90 rpm"

#### Scenario: Grind item without RPM

- **WHEN** the `grind` item is present and no RPM is recorded (dyeGrinderRpm = 0)
- **THEN** the grind segment shows only the grinder setting, with no RPM placeholder

## ADDED Requirements

### Requirement: Shot plan overflow wraps before eliding

When the Shot Plan text is wider than the width available to the widget, it SHALL wrap onto a second line, and only content that does not fit within two lines SHALL be elided. The text SHALL NEVER be clipped mid-word at the widget or screen edge. Wrapping and eliding SHALL apply to both the sentence and fragment formats and preserve the rich-text styling (bolded live values).

#### Scenario: Long plan wraps to a second line

- **WHEN** the rendered plan's natural single-line width exceeds the width granted to the widget
- **THEN** the text wraps onto a second line and all content remains readable

#### Scenario: Extreme overflow elides, never clips

- **WHEN** the rendered plan does not fit even within two lines at the granted width
- **THEN** the text ends with an ellipsis at the end of the second line, with no characters cut off at the widget edge

#### Scenario: Short plan stays on one line

- **WHEN** the rendered plan fits the granted width on one line
- **THEN** it renders on a single line, centered as today
