# plan-widgets Specification

## Purpose
Defines the text-rendering rules for the `shotPlan` widget's shot-plan and steam-plan sentences: how the configured display-item list maps to sentence and fragment formats, override indicators for temperature and yield, the cleaning-profile warning, page-aware switching between shot and steam plans, and the wrap-before-elide overflow behavior.
## Requirements
### Requirement: Shot plan sentence content follows the display toggles
The Shot Plan text SHALL render the display items named by the instance's ordered item list (`shotPlanItems`), in list order, in one of two formats selected by Sentence style (`shotPlanSentence`, default ON). Each item SHALL contribute exactly its named content, and an item absent from the list SHALL contribute nothing. Fragment format (Sentence style OFF) SHALL join present segments with the standard separator in list order.

#### Scenario: Fragment format follows chip order

- **WHEN** Sentence style is OFF and the item list is `["grind", "coffee", "doseYield"]`
- **THEN** the plan renders "grind {setting} · {coffee} · {dose}g in · {yield}g" — the grind segment first, in exactly the configured order

#### Scenario: Recipe item renders the active recipe name in fragment order

- **WHEN** Sentence style is OFF, a recipe named "Morning Latte" is active, and the item list is `["recipe", "doseYield"]`
- **THEN** the plan renders "Morning Latte · {dose}g in · {yield}g" — the recipe name first, in the configured order

#### Scenario: Recipe replaces Profile as the sentence anchor

- **WHEN** Sentence style is ON, a recipe named "Morning Latte" is active, the item list is `["doseYield", "temperature", "recipe", "grind"]` (no `profile`)
- **THEN** the plan renders "Brew {yield} of Espresso, using Morning Latte at 92°C" followed by the tail "grind {setting}" — the recipe name filling the "using" slot exactly where a profile name would

#### Scenario: Profile takes the anchor when both Profile and Recipe are shown

- **WHEN** Sentence style is ON, a recipe named "Morning Latte" is active, a profile is loaded, and the item list is `["doseYield", "profile", "temperature", "recipe"]`
- **THEN** the plan reads "Brew {yield} of {beverage}, using {profile} at {temperature}" with "Morning Latte" trailing as a tail item — the profile keeps the anchor

#### Scenario: Recipe item contributes nothing when no recipe is active

- **WHEN** the `recipe` item is present but no recipe is active
- **THEN** the recipe segment is empty; in fragment mode it renders nothing, and in sentence mode it neither fills the anchor nor trails, so the plan renders exactly as if the `recipe` item had been absent

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

- **WHEN** Sentence style is ON, no recipe is active, and the item list is `["temperature", "coffee"]` (no `profile`, no `doseYield`)
- **THEN** the plan renders "Brew {beverage} at {temperature} from {coffee}" — a sentence anchored on the beverage word, not a fragment list

#### Scenario: No profile loaded renders the profile-less recipe sentence even with the Profile chip shown

- **WHEN** Sentence style is ON, the item list includes `profile`, no profile is currently loaded (no profile name available), and no recipe is active
- **THEN** the plan renders the profile-less recipe sentence, exactly as if the Profile chip had been removed

#### Scenario: Filter profile says coffee, not espresso

- **WHEN** the current profile's beverage_type is "filter" (or "pourover")
- **THEN** the sentence reads "Brew {yield} of coffee, using {anchor} at {temperature}"

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

### Requirement: Display item contents
The eight display items are Profile (`profile`), Temperature (`temperature`), Roaster (`roaster`), Coffee (`coffee`), Grind (`grind`), Roast date (`roastDate`), Recipe (`recipe`) and Dose & yield (`doseYield`). Profile SHALL show the profile name. Roaster SHALL show the roaster brand only, Coffee the bean name only, and Roast date the roast date. Dose & yield SHALL show the dose ("{dose} in") and the target output weight.

#### Scenario: Roaster and coffee show only their own names
- **WHEN** the item list is `["roaster", "coffee"]` with a roaster brand and bean name set
- **THEN** the plan shows the brand alone and then the bean name alone, with no other text

### Requirement: Grind, recipe and temperature item contents
The Grind item SHALL show the grinder setting, plus the RPM only when `Settings.dye.dyeGrinderRpm` is greater than 0. The Recipe item SHALL show the active recipe name, or nothing when no recipe is active. The Temperature item SHALL follow the C/F display unit and the temperature-override rendering.

#### Scenario: Temperature item follows the display unit
- **WHEN** the display unit is Fahrenheit and the `temperature` item is present
- **THEN** the brew temperature is shown in Fahrenheit

### Requirement: Sentence format anchors on the profile or the active recipe
In Sentence format the scaffold SHALL have a single "using {anchor}" slot. The anchor SHALL be the Profile item's name when that item is present and a name is available. Otherwise, with the Recipe item present and a recipe active, the anchor SHALL be the recipe name. The Dose & yield and Temperature items SHALL be consumed by the scaffold, wherever they sit. All other present items SHALL trail as a separator-joined tail in list order.

#### Scenario: Recipe stands in for profile in the anchor slot
- **WHEN** Sentence style is ON, a recipe named "Morning Latte" is active, the item list has no `profile`, and `recipe` is present
- **THEN** the sentence reads "using Morning Latte", in the slot a profile name would fill

### Requirement: The sentence scaffold degrades by what is present
With an anchor, the scaffold SHALL degrade by what is present and available, and SHALL always include the beverage word.

#### Scenario: The scaffold degrades by what is present
- **WHEN** Sentence style is ON with an anchor
- **THEN** yield, anchor and temperature give "Brew {yield} of {beverage}, using {anchor} at {temperature}"; with no yield, "Brew {beverage}, using {anchor} at {temperature}"; with no temperature, "Brew {yield} of {beverage}, using {anchor}"; and with only the anchor, "Brew {beverage}, using {anchor}"

### Requirement: Recipe never displaces a present profile
When both the Profile and Recipe items are present with values, the Profile name SHALL fill the anchor and the Recipe name SHALL trail as an ordinary tail item. Recipe SHALL NOT displace a present, available Profile.

#### Scenario: Recipe never displaces a present profile
- **WHEN** a recipe is active, a profile is loaded, and both `profile` and `recipe` are shown
- **THEN** the profile fills the anchor and the recipe name trails as a tail item

### Requirement: Stacked details option
With Stacked details (`shotPlanStacked`, default OFF) on, the tail SHALL render on its own line below the sentence instead of trailing after a separator. This applies on the display path only. The accessibility string SHALL remain one separator-joined sentence, and compact (bar) placements SHALL ignore the option.

#### Scenario: Stacked details keep one spoken sentence
- **WHEN** Stacked details is on and the widget is read by a screen reader
- **THEN** the sentence and tail are heard as one separator-joined sentence

### Requirement: Profile-less recipe sentence
With no anchor available, the plan SHALL fall back to the profile-less recipe sentence. The scaffold SHALL consume Dose & yield, Temperature, Roaster and Coffee together with the beverage word, and only Grind and Roast date SHALL trail as a separator-joined tail in list order. Sentence style SHALL NOT degrade to fragment format while it is ON.

#### Scenario: Profile-less scaffold forms
- **WHEN** Sentence style is ON and no anchor is available
- **THEN** the plan reads "Brew {yield} of {beverage} at {temperature} from {dose} of {roaster} {coffee}" when all four are present, "Brew {yield} of {beverage}" when only yield is present, and "Brew {beverage} from {dose}" when only dose is present

### Requirement: Beverage word follows the profile type
The beverage word SHALL follow the profile's `beverage_type`: "Espresso" for espresso and unset, "tea" for tea types, and "coffee" for any other coffee beverage. A cleaning or descale profile SHALL replace the plan entirely, in both formats, with a bold error-colour notice warning not to load coffee, and no item segments.

#### Scenario: A cleaning profile replaces the plan
- **WHEN** the current profile's beverage_type is "cleaning" or "descale"
- **THEN** the widget shows the cleaning notice with no item segments, regardless of the Sentence style option

### Requirement: Plain and rich text share one builder
The plain (accessibility) text and the rich (displayed) text SHALL be produced by one shared builder, so their content cannot drift.

#### Scenario: Accessibility and display text match
- **WHEN** the same plan is rendered for display and read for accessibility
- **THEN** both carry the same items in the same order

### Requirement: Override indicators
The Shot Plan text SHALL highlight only the overridden item, not the whole sentence. A deliberate temperature override, beyond 0.1°C of the surface's temperature baseline, SHALL render the temperature item in `Theme.highlightColor`. A deliberate yield override, beyond 0.1 g of the yield baseline, SHALL render as "{baselineYield} → {target}g" in that colour. The rest of the sentence and the icon SHALL keep the default text colour.

#### Scenario: Highlight appears on non-idle surfaces
- **WHEN** a shot snapshot line renders a plan whose shot carries a yield or temperature override
- **THEN** that overridden item is highlighted using the same per-item scheme as the idle widget, driven by the shot's own values

#### Scenario: Recipe cards never highlight
- **WHEN** a recipe card renders a recipe with `tempOffsetC` = −3 on a profile whose frames are 84 · 94°C, or a recipe whose stored yield differs from its profile's target
- **THEN** the card's temperature and yield segments both render in the default text color — no tag, no arrow, no highlight

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

### Requirement: Override indicators on every Shot Plan surface
The highlight SHALL apply on every surface that renders the Shot Plan: the idle widget, recipe cards, the shot-detail and post-shot-review lines, and the screensaver preview. Each surface SHALL be driven by its own override inputs, the live dial for the home widget and the frozen shot snapshot for shot surfaces. A frozen-shot surface SHALL NOT reflect the live dial's override state.

#### Scenario: Frozen surfaces do not borrow the live override
- **WHEN** a live temperature override is active and the user opens a historical shot whose snapshot had no override
- **THEN** the shot-detail plan line shows no highlight

### Requirement: Recipe cards never highlight
Recipe cards have no override inputs and SHALL render neither temperature nor yield in the override-highlight colour, with no tag and no arrow. The temperature SHALL resolve from the recipe's own profile frames, shifted by its `tempOffsetC`, and show the resulting value only (see `recipe-quick-switch`). Natural dose drift SHALL NOT trigger either indicator.

#### Scenario: Recipe cards never highlight
- **WHEN** a recipe card renders a recipe with `tempOffsetC` of −3 on a profile whose frames are 84 · 94°C
- **THEN** the card's temperature and yield segments render in the default text color

### Requirement: Steam plan sentence
The Steam Plan text SHALL summarise the next steam as "Steam {milk} of milk, using the {pitcher} pitcher for {duration}", degrading to a separator-joined list when a piece is missing. It SHALL render nothing when the selected pitcher preset is the disabled "Off" preset. The sentence SHALL NOT append "pitcher" when the preset name already contains it, case-insensitively.

#### Scenario: Pitcher name containing "Pitcher"

- **WHEN** the selected preset is named "Large Pitcher"
- **THEN** the sentence reads "…using the Large Pitcher for 30s" (no duplicated word)

#### Scenario: Duration reflects the selected preset

- **WHEN** the user selects a pitcher preset without tapping it to start
- **THEN** the displayed duration matches that preset's effective steam time, not a stale value from a previous session

### Requirement: Steam duration comes from the shared helper
The displayed duration SHALL be the selected preset's effective steam time, resolved by the single shared `SettingsBrew` helper. It SHALL NOT read the residue another code path wrote to `steamTimeout`. The effective time SHALL be scaled when weight-timed steaming has a measured or reference milk weight, else it SHALL be the preset's base duration.

#### Scenario: Steam duration uses the scaled time when weight-timed
- **WHEN** weight-timed steaming has a measured milk weight
- **THEN** the displayed duration is the preset's time scaled to that weight

### Requirement: Page-aware steam mode of the Shot Plan widget
The `shotPlan` widget SHALL display the Shot Plan text except in steam context (steam selected on the idle screen, the steam page active, or the machine actively steaming). In steam context it SHALL display the Steam Plan text, unless the Steam plan option (`shotPlanShowSteamPlan`, default ON) is off. Page state SHALL be read from the app's single existing source, the `Theme` singleton, not a duplicate copy.

#### Scenario: Switches to steam plan

- **WHEN** the user selects steam on the idle screen and the instance's Steam plan option is on
- **THEN** the widget shows the steam plan and is announced as a read-only "Steam plan"

#### Scenario: Steam plan option off

- **WHEN** the instance's Steam plan option is off and the machine is steaming
- **THEN** the widget keeps showing the shot plan

#### Scenario: Shot side opens Brew Settings

- **WHEN** the widget is showing the shot plan and is tapped (or activated via a screen reader)
- **THEN** Brew Settings opens

### Requirement: Activation and accessibility follow the steam context
Outside steam mode the widget SHALL be an activatable control that opens Brew Settings. In steam mode it SHALL be a read-only summary, and its accessibility role and name SHALL match the mode.

#### Scenario: Steam mode is a read-only summary
- **WHEN** the machine is actively steaming
- **THEN** the widget is announced as a read-only "Steam plan" and does not open Brew Settings when tapped

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

### Requirement: Recipe item defaults off and is offered by both layout editors
The Recipe (`recipe`) display item SHALL default OFF. It SHALL NOT appear in the canonical default item order, and no widget configuration saved before this change SHALL gain it. It SHALL appear only when a user adds it to an instance's `shotPlanItems` list. The shared item catalog (`allKeys`) SHALL include `recipe`.

#### Scenario: Recipe is off in the default layout

- **WHEN** a Shot Plan widget uses the canonical default item order (no `shotPlanItems` saved, or the pre-change defaults)
- **THEN** the recipe name is not shown, and the widget renders exactly as before this change

#### Scenario: Recipe appears in both editors' available items

- **WHEN** a user opens the in-app Shot Plan Settings, or the ShotServer web layout editor, for a Shot Plan widget that does not yet show Recipe
- **THEN** "Recipe" is offered in the Available items and can be added to the Shown list, and once shown can be reordered and removed like any other item

### Requirement: Both layout editors offer the Recipe item
Both layout editors SHALL offer `recipe` as an addable and removable item with a translatable "Recipe" label: the in-app Shot Plan Settings chip editor and the ShotServer web layout editor. The two editors SHALL stay in sync on the item set and its label.

#### Scenario: Web editor offers Recipe
- **WHEN** a user opens the ShotServer web layout editor for a Shot Plan widget
- **THEN** "Recipe" is offered in the same item set and label as the in-app editor

### Requirement: Recipe name is the active recipe live and the frozen recipe on shot surfaces
On the live idle widget the Recipe item SHALL render the currently active recipe's name, gated on there being an active recipe. On a frozen-shot surface (the shot-detail and post-shot-review snapshot lines) it SHALL render that shot's own recorded recipe name, from the surface's snapshot input. A surface supplying no recipe name SHALL render the item empty and SHALL NOT let an empty recipe fill the sentence anchor.

#### Scenario: Idle widget shows the live active recipe

- **WHEN** the Recipe item is shown on the idle Shot Plan widget and the active recipe changes
- **THEN** the widget updates to the newly active recipe's name

#### Scenario: Shot snapshot shows the shot's own recipe, not the live one

- **WHEN** the Recipe item is shown on the shot-detail or post-shot-review snapshot line for a past shot, and a different recipe is now active live
- **THEN** the line shows the recipe recorded for that shot, not the currently active recipe

