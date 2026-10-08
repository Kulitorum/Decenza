# shot-page Specification

## Purpose
Defines the single page for one espresso shot: how it is reached, what it tells the user about
how the shot went and what changed since the last one, how it is laid out, and how the web shot
page mirrors it.

## Requirements

### Requirement: One page shows and edits a shot

The app SHALL have one page for a single shot, and it SHALL always be editable. The post-shot
path, a shot opened from Shot History, the last-shot widget and the shot-review layout action
SHALL all open this page. There SHALL be no separate read-only shot page.

#### Scenario: Opening a shot from history

- **WHEN** the user taps a shot in Shot History
- **THEN** the shot page opens with that shot's rating, notes and measurements editable in place
- **AND** no Edit button is needed to change them

#### Scenario: After pulling a shot

- **WHEN** a shot finishes and post-shot review is enabled
- **THEN** the same shot page opens for the new shot, with its auto-close behaviour unchanged

### Requirement: The shot page steps between shots from a list

When opened from a list of shots, the shot page SHALL let the user step to the newer and older
shot in that list by swiping the graph and by Newer/Older buttons, and SHALL show the position
in the list. Pending edits SHALL be saved before the step. The undo history SHALL belong to the
shot it was made on.

#### Scenario: Swiping to the older shot after an edit

- **GIVEN** the user opened a shot from Shot History and changed its rating
- **WHEN** the user swipes to the older shot
- **THEN** the rating change is saved to the first shot
- **AND** the page shows the older shot with nothing to undo

#### Scenario: Opened without a list

- **WHEN** the page is opened for one shot only, such as after pulling it
- **THEN** no Newer/Older control is shown

### Requirement: The shot page keeps the read-only page's actions

The shot page SHALL offer the following actions:
- Delete Shot, after a confirmation
- View Debug Log, in advanced mode
- Create a recipe from this shot
- the shot's Visualizer and Decent upload state

When the shown shot is deleted, the page SHALL go back.

#### Scenario: Deleting a shot

- **WHEN** the user chooses Delete Shot and confirms
- **THEN** the shot is deleted and the page goes back

#### Scenario: Cancelling a delete

- **WHEN** the user chooses Delete Shot and cancels the confirmation
- **THEN** nothing is deleted and the page stays on the shot

### Requirement: "Shot results" shows the shot's measured outcome

The shot page SHALL show a "Shot results" section. It SHALL use the same metrics, labels and
display precision as the shot comparison, so a value on the shot page reads the same as in a
comparison.
- Shown by default: duration, yield with its target when set, ratio, time to first drop, peak
  pressure and mean flow.
- Revealed by "Show more": the comparison's other metrics, including TDS and EY when recorded.
- Always shown: how the shot stopped.

#### Scenario: A shot stopped at its weight target

- **WHEN** a 36.4 s shot that stopped at its 40 g target is opened
- **THEN** "Shot results" shows the duration 36.4 s, the yield against the 40 g target and that it stopped at the weight target

#### Scenario: A metric the shot cannot support

- **WHEN** the shot has no weight data
- **THEN** yield shows "—" rather than a number

#### Scenario: Details on request

- **WHEN** the page opens
- **THEN** resistance and temperature sag are hidden until the user taps "Show more"

### Requirement: "Shot results" compares the shot with the previous one on its profile

When an earlier shot on the same profile exists, "Shot results" SHALL open by naming it, by
profile and time, with a control that opens the comparison of the two with the earlier shot as
base. It SHALL then show the comparison's one-line summary, the inputs that changed (before →
after) and the profile settings a re-tune moved, and each metric SHALL carry its Δ against that
shot. With no earlier shot it SHALL show only the metrics. There SHALL be no second card for
this comparison.

#### Scenario: A grind change

- **GIVEN** the previous shot on this profile was ground at 10 and this one at 9.5
- **WHEN** the shot page opens
- **THEN** "Shot results" names the previous shot, the grind change 10 → 9.5 and what moved
- **AND** duration carries its Δ against the previous shot

#### Scenario: Nothing changed

- **WHEN** no input differs from the previous shot on this profile
- **THEN** the summary says the setup was the same rather than listing no inputs

#### Scenario: Opening the comparison

- **WHEN** the user taps Compare in "Shot results"
- **THEN** the comparison opens with the previous shot as base and this shot beside it

#### Scenario: First shot on a profile

- **WHEN** no earlier shot on the same profile exists
- **THEN** "Shot results" shows the metrics with no Δs and names no earlier shot

#### Scenario: An edit changes the comparison

- **WHEN** the user changes this shot's grind on the page
- **THEN** the summary and Δs reflect the new grind once the edit is saved

### Requirement: The shot graph has curve and phase chips and a readout under the plot

The shot page graph SHALL use the comparison graph's controls:
- **Curve chips** under the plot. Each curve is a toggle chip with an explanatory tip, and
  rarely used curves sit behind a "+N" chip.
- **Phase chips.** Each phase is a chip that shows or hides that phase's marker.
- **Crosshair readout.** Values are shown under the plot in fixed columns, so the readout never
  covers a curve and a tap does not move the layout.

#### Scenario: Turning a curve off

- **WHEN** the user taps the Flow chip
- **THEN** the flow curve is hidden on the shot page and the chip shows as off

#### Scenario: Inspecting a point

- **WHEN** the user taps the graph at 20 s
- **THEN** the values at 20 s appear under the plot and nothing else on the page moves

### Requirement: The shot page is one column with the graph at full width

The app's shot page SHALL be a single column at every window width: the header and plan
line, then the graph at full width with its chips and readout, then rating, taste, notes and
measurements, then "Shot results", then the recipe, bean and equipment cards. The graph SHALL NOT share its width with other content. The graph height
SHALL be one setting, shared by every way of opening the page.

#### Scenario: Tablet in landscape

- **WHEN** the page is shown on a landscape tablet
- **THEN** the graph spans the page width and nothing sits beside it

#### Scenario: Rating right after the pull

- **WHEN** the page opens after a shot
- **THEN** the rating control sits directly under the graph, before "Shot results"

### Requirement: Merging the pages preserves editing behaviour

Editing on the shot page SHALL autosave, coalesce gestures and support undo exactly as the review
page did. Uploads SHALL be held while the shown shot is being edited. Screen-reader reading order
SHALL follow the visual order.

#### Scenario: Undo after editing notes

- **WHEN** the user edits the notes and taps Undo
- **THEN** the notes return to their previous text and the change is saved

#### Scenario: Screen reader order

- **WHEN** a screen reader traverses the page
- **THEN** it reads the header, the graph, the rating and notes, the outcome, then the cards, top to bottom

### Requirement: The web shot page mirrors the shot page

The web shot page SHALL show what the app's shot page shows, in the app's order: "Shot results" with "Show more",
how the shot stopped and the comparison with the previous shot linking to the web comparison;
curve and phase chips and a crosshair readout under the chart; the phase summary; the
Visualizer and Decent upload state; newer and older shot links; the debug log; and Delete
after a confirmation.

#### Scenario: Web shot with a previous shot

- **WHEN** a shot with an earlier shot on the same profile is opened in the browser
- **THEN** the page shows the same summary sentence as the app and links to the comparison of the two shots

### Requirement: The web shot page edits in place as the app does

Rating, taste, notes, dose, yield, grind, RPM, TDS, EY and barista on the web shot page SHALL
be editable in place and saved as each is changed, with Undo. Beans SHALL be chosen from the
bags and equipment from the packages, as the app's dialogs do. The page SHALL offer Upload and
Save as recipe. The refractometer and milk-weigh features, which need the user at the machine,
SHALL NOT be on the web.

#### Scenario: Rating a shot in the browser

- **WHEN** the user taps 75 on the web shot page
- **THEN** the rating is saved without a Save button, Undo appears, and "Shot results" refreshes

#### Scenario: Changing beans in the browser

- **WHEN** the user picks a bag from the web page's bean list
- **THEN** the shot carries that bag's beans, as if changed in the app

### Requirement: The web shot page keeps its graph in view on a wide window

On a browser window at least 1300 px wide, the web shot page SHALL place the graph beside the
details and keep it in view while the details scroll, as the web comparison does. Narrower
windows SHALL use one column with the graph first.

#### Scenario: Desktop browser

- **WHEN** the web shot page is opened in a 1440 px wide window
- **THEN** the graph sits beside the details and stays in view while they scroll

#### Scenario: Phone browser

- **WHEN** the web shot page is opened on a phone
- **THEN** the page is one column, graph first, with no horizontal scroll
