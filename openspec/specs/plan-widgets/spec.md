# plan-widgets Specification

## Purpose
Defines the text-rendering rules for the `shotPlan` widget's shot-plan and steam-plan sentences: how the configured display-item list maps to sentence and fragment formats, override indicators for temperature and yield, the cleaning-profile warning, page-aware switching between shot and steam plans, and the wrap-before-elide overflow behavior.
## Requirements
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

Profile item absent (or no profile name available) → the sentence has no profile anchor, so it falls back to the profile-less "recipe" sentence: the scaffold instead consumes the Dose & yield, Temperature, Roaster, and Coffee items (wherever they sit in the list) together with the beverage word, and only the Grind and Roast date items SHALL trail after it as a separator-joined tail in item-list order. The beverage word SHALL always anchor this sentence, so Sentence style SHALL NOT degrade to fragment format while it is ON — regardless of which other items are present:

- yield + temperature + dose + roaster + coffee → "Brew {yield} of {beverage} at {temperature} from {dose} of {roaster} {coffee}"
- yield only, no temperature/dose/roaster/coffee → "Brew {yield} of {beverage}"
- no yield, dose present, no roaster/coffee → "Brew {beverage} from {dose}"
- no yield, no dose, roaster + coffee only → "Brew {beverage} from {roaster} {coffee}"

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

#### Scenario: Profile chip removed renders the profile-less recipe sentence

- **WHEN** Sentence style is ON and the item list is `["temperature", "coffee"]` (no `profile`, no `doseYield`)
- **THEN** the plan renders "Brew {beverage} at {temperature} from {coffee}" — a sentence anchored on the beverage word, not a fragment list

#### Scenario: No profile loaded renders the profile-less recipe sentence even with the Profile chip shown

- **WHEN** Sentence style is ON, the item list includes `profile`, but no profile is currently loaded (no profile name available)
- **THEN** the plan renders the profile-less recipe sentence, exactly as if the Profile chip had been removed

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

### Requirement: Override indicators

The Shot Plan text SHALL highlight only the individual overridden item(s), not the whole sentence: when a deliberate temperature override is active (the override differs from the surface's temperature baseline by more than 0.1°C) the temperature item SHALL render in the override-highlight color; when a deliberate yield override is active (the target differs from the surface's yield baseline by more than 0.1 g) the yield item SHALL render as "{baselineYield} → {target}g" in the override-highlight color. Every other part of the sentence, and the plan icon, SHALL remain the default text color. The override-highlight color SHALL be `Theme.highlightColor` — the same color the Brew Settings values use for an override — so the two surfaces read consistently. Natural drift between measured dose and profile dose SHALL NOT trigger either indicator.

Because the highlight lives in the shared `ShotPlanText` component, it SHALL apply on **every** surface that renders the Shot Plan — the idle home widget, recipe cards, the shot-detail and post-shot-review snapshot lines, and the screensaver preview — not only the idle page. Each surface's highlight SHALL be driven by **that surface's own** override inputs: the live dial for the home widget, and the shot's frozen snapshot values for the shot-detail and post-shot-review lines. A frozen-shot surface SHALL NOT reflect the live dial's override state; if it does not render the temperature or yield item, that item simply shows no highlight (it does not borrow the live override).

**Recipe cards are the one deliberate exception on the temperature item** (recipe-relative-temp-offset, see `recipe-quick-switch`): a card renders its own recipe's profile frame temperatures in the default text color followed by the recipe's stored `tempOffsetC` as a signed tag, and the highlight wraps **only the tag** — the card shows source + delta rather than an overridden result, so it does not feed the whole-item temperature override inputs at all. The card's yield item keeps the standard highlighted "{profileYield} → {recipeYield}g" arrow, driven by the recipe's stored yield against its own profile's target.

#### Scenario: Highlight appears on non-idle surfaces
- **WHEN** a shot snapshot line renders a plan whose shot carries a yield or temperature override, or a recipe card renders a recipe whose stored yield differs from its profile's target
- **THEN** that overridden item (shot line) or the yield arrow (recipe card) is highlighted using the same per-item scheme as the idle widget, driven by the shot's/recipe's own values

#### Scenario: Recipe card temperature highlights only the offset tag
- **WHEN** a recipe card renders a recipe with `tempOffsetC` = −3 on a profile whose frames are 84 · 94°C
- **THEN** the card reads "84 · 94°C −3°" with the base temps in the default text color and only the "−3°" tag in `Theme.highlightColor`

#### Scenario: Frozen surfaces do not borrow the live override
- **WHEN** a live temperature override is active and the user opens a historical shot whose snapshot had no override
- **THEN** the shot-detail plan line shows no highlight (it reflects the shot's frozen state, not the live dial)

#### Scenario: Temperature override highlights only the temperature item
- **WHEN** a deliberate temperature override is active on the live dial or a shot snapshot
- **THEN** that surface's temperature item renders in `Theme.highlightColor`
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

### Requirement: Steam plan sentence

The Steam Plan text (the steam-context rendering of the `shotPlan` widget) SHALL summarise the next steam as "Steam {milk} of milk, using the {pitcher} pitcher for {duration}", degrading to a separator-joined list when a piece is missing, and SHALL render nothing when the selected pitcher preset is the disabled ("Off") preset. When the selected preset's name already contains the word "pitcher" (case-insensitive), the sentence SHALL NOT append another "pitcher", so a preset named "Large Pitcher" never renders "Large Pitcher pitcher". The displayed duration SHALL be the selected preset's effective steam time (scaled when weight-timed steaming has a measured/reference milk, else the preset's base duration), resolved by the single shared `SettingsBrew` helper — never the residue another code path wrote to `steamTimeout`.

#### Scenario: Pitcher name containing "Pitcher"

- **WHEN** the selected preset is named "Large Pitcher"
- **THEN** the sentence reads "…using the Large Pitcher for 30s" (no duplicated word)

#### Scenario: Duration reflects the selected preset

- **WHEN** the user selects a pitcher preset without tapping it to start
- **THEN** the displayed duration matches that preset's effective steam time, not a stale value from a previous session

### Requirement: Page-aware steam mode of the Shot Plan widget

The `shotPlan` widget SHALL display the Shot Plan text except in steam context — steam selected on the idle screen, the steam page active, or the machine actively steaming — where it SHALL display the Steam Plan text, unless the instance's Steam plan option (`shotPlanShowSteamPlan`, default ON) is off. Page/mode state SHALL be read from the app's single existing page-state source (the `Theme` singleton), not from a duplicate copy. Outside steam mode the widget SHALL be an activatable control that opens Brew Settings; in steam mode it SHALL be a read-only summary, and its accessibility role and name SHALL match the mode.

#### Scenario: Switches to steam plan

- **WHEN** the user selects steam on the idle screen and the instance's Steam plan option is on
- **THEN** the widget shows the steam plan and is announced as a read-only "Steam plan"

#### Scenario: Steam plan option off

- **WHEN** the instance's Steam plan option is off and the machine is steaming
- **THEN** the widget keeps showing the shot plan

#### Scenario: Shot side opens Brew Settings

- **WHEN** the widget is showing the shot plan and is tapped (or activated via a screen reader)
- **THEN** Brew Settings opens

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

