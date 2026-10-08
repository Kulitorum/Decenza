# Spec Delta

## Purpose

Defines how Decenza compares espresso shots: one shot is the base, and every other shot is
presented as what the user changed relative to it and what the shot did differently, on the
app page, the web page and the MCP tool alike.

## ADDED Requirements

### Requirement: Comparison has a base shot, the oldest by default

Every comparison SHALL have exactly one base shot. When the comparison opens, the base SHALL be
the oldest selected shot and SHALL occupy the left column. The other shots SHALL follow it in
chronological order.

#### Scenario: Opening a comparison selects the oldest shot as base

- **WHEN** the user compares shots from Oct 6 10:35, Oct 6 11:16 and Oct 5 09:02
- **THEN** the Oct 5 09:02 shot is the base and is shown in the left column
- **AND** the remaining shots follow it oldest first

### Requirement: Tapping a shot makes it the base and moves it left

Tapping a non-base shot's header SHALL make that shot the base and move it to the left column.
The previous base SHALL return to its chronological place among the other shots. Every
comparison value and Δ SHALL be recomputed against the new base. The choice SHALL last for the
page visit and SHALL NOT be saved as a setting.

#### Scenario: Re-basing on a later shot

- **GIVEN** a comparison of shots A (oldest), B and C with A as base
- **WHEN** the user taps C's header
- **THEN** C moves to the left column as the base, followed by A then B
- **AND** every Δ is now relative to C

#### Scenario: Re-opening resets the base

- **GIVEN** the user re-based a comparison on a later shot and left the page
- **WHEN** the user opens a comparison again
- **THEN** the oldest shot is the base

### Requirement: All selected shots remain comparable against the base

Choosing a base SHALL NOT reduce the number of shots compared. When more shots are selected than
fit on screen, the base SHALL stay pinned in the left column while the previous/next control
moves through the other shots.

#### Scenario: Paging keeps the base in view

- **GIVEN** five selected shots with the oldest as base and room for three columns
- **WHEN** the user steps to the next shots
- **THEN** the base remains in the left column
- **AND** the two columns beside it show the next non-base shots

### Requirement: "What you changed" lists only inputs that differ from the base

The comparison SHALL show a "What you changed" section whose rows are the inputs that differ
between the base and at least one visible shot: profile, brew temperature, dose, target yield,
grind setting, RPM, every equipment and bean field the advisor treats as shot identity (grinder,
burrs, basket, puck prep, bean, roast and storage dates) and barista. Differing cells SHALL be
marked; numeric inputs SHALL show their Δ.

#### Scenario: Only the grind moved

- **GIVEN** two shots identical except grind setting 10 on the base and 9 on the other
- **WHEN** the comparison is shown
- **THEN** "What you changed" has one row, Grind, showing 10 and 9 with a Δ of −1
- **AND** no other input appears as a row

### Requirement: An unchanged setup is stated, not left blank

When no input differs between the base and a shot, the comparison SHALL say so for that shot,
so "your change did nothing" can be told apart from "you changed nothing".

#### Scenario: Two shots pulled the same way

- **WHEN** every input of a shot matches the base within tolerance
- **THEN** "What you changed" states that nothing changed for that shot
- **AND** "What happened" still shows its metrics and Δs

### Requirement: Grind Δ only between shots on the same grinder

A grind setting SHALL be shown with a Δ only when both shots used the same grinder and burrs.
Across different grinders or burrs both settings SHALL be shown without a Δ, because the numbers
are not on the same scale.

#### Scenario: Grinder swapped

- **WHEN** the base used a Niche Zero at 10 and the other shot a DF64 at 12
- **THEN** both grinder and grind are listed as changed
- **AND** the grind row shows 10 and 12 with no Δ

### Requirement: Inputs identical across all visible shots collapse into one line

Inputs whose values are the same on every visible shot SHALL be shown once, on a single
"Unchanged" line, and SHALL NOT be repeated per shot.

#### Scenario: Shared setup is shown once

- **GIVEN** two shots with the same dose, bean, roast, grinder and RPM
- **WHEN** the comparison is shown
- **THEN** those values appear once on the "Unchanged" line
- **AND** they do not appear as rows in either section

### Requirement: Input equality ignores noise and notation

Two measured or rounded inputs SHALL count as the same within the advisor's tolerance: dose
within 0.3 g, target yield within 0.5 g, RPM within 25. A grind setting is set on purpose, so
any recorded difference SHALL be a change; settings that differ only in spacing or compound
notation SHALL be equal.

#### Scenario: Scale noise is not a change

- **WHEN** the base was dosed 18.0 g and the other shot 18.2 g
- **THEN** dose is not listed as changed

#### Scenario: A fine regrind is a change

- **WHEN** the base grind is 4.0 and the other shot's is 4.2
- **THEN** grind is listed as changed from 4.0 to 4.2

#### Scenario: Notation is not a change

- **WHEN** the base grind is recorded as "1 + 4" and the other as "1+4"
- **THEN** grind is not listed as changed

### Requirement: An input recorded on only one shot is shown, not hidden

When an input is recorded on one shot and missing on the other, the comparison SHALL list it in
"What you changed" with "—" for the missing value and SHALL NOT show a Δ for it.

#### Scenario: Grind recorded only on the newer shot

- **WHEN** the base has no grind setting and the other shot has 10
- **THEN** the Grind row shows "—" and 10, with no Δ

### Requirement: Profile changes are reported as settings, not as file differences

When two shots used the same profile, the comparison SHALL say whether the saved profiles are
the same version. If they differ, it SHALL list the settings that changed, old to new, judged by
what the machine would do: numbers compare numerically, and omitted zeroes, no-op limiters and
inactive-axis values are not changes. Different profiles SHALL show both profile names and no
setting list.

#### Scenario: Same version

- **WHEN** both shots used D-Flow/Q with identical saved profiles
- **THEN** the profile is reported as the same version on both shots

#### Scenario: A re-tuned profile

- **WHEN** both shots used D-Flow/Q and the newer shot's saved profile has a pour pressure of 8 bar instead of 9
- **THEN** the profile row reports that setting changing from 9 to 8 bar

#### Scenario: Encoding differences are not reported

- **WHEN** the two saved profiles differ only in an omitted zero weight or "8.00" versus "8.0"
- **THEN** the profile is reported as the same version

### Requirement: "What happened" shows key metrics first, the rest on request

"What happened" SHALL show by default: duration, yield (with its target when set), ratio, time
to first drop, peak pressure and mean flow, each non-base value carrying its Δ against the base. A
"Show more" control SHALL reveal peak flow, average g/s, mean group temperature, temperature
sag, resistance, preinfusion and pour times, and TDS/EY when recorded, with Δs.

#### Scenario: Two dial-in shots

- **GIVEN** a base shot of 37.2 s and 53.3 g and another shot of 11.0 s and 13.3 g
- **WHEN** the comparison is shown
- **THEN** the other shot's duration shows a Δ of −26.2 s and its yield a Δ of −40.0 g

#### Scenario: Details are collapsed until asked for

- **WHEN** the comparison opens
- **THEN** resistance and temperature sag are not shown
- **AND** tapping "Show more" reveals them with their Δs

#### Scenario: A metric missing on one shot

- **WHEN** a shot has no weight data, so yield cannot be measured
- **THEN** its yield shows "—" with no Δ

### Requirement: A one-line summary leads the comparison

For each non-base shot, the comparison SHALL open with one plain sentence built by fixed rules
from the comparison itself: the inputs that changed, the metric differences furthest past
their noise floor (at most three, none below it), how it stopped when that differs, and any quality badge that appeared or went
away. It SHALL NOT be generated by an AI provider.

#### Scenario: A grind change summarised

- **WHEN** the only changed input is grind 10 → 9.5 and the shot ran 6.1 s longer with channeling gone
- **THEN** the summary names the grind change, the longer shot time and that channeling went away

#### Scenario: Nothing changed

- **WHEN** no input differs from the base
- **THEN** the summary says nothing was changed before naming the largest differences

### Requirement: The comparison shows why each shot stopped

"What happened" SHALL show how each shot ended — at the weight target, at the volume target, by
the profile finishing, or stopped by hand — whenever it differs between visible shots.

#### Scenario: A short shot stopped by hand

- **GIVEN** a base that stopped at its 54 g target and a shot that ended at 13.3 g
- **WHEN** the second shot was stopped by hand
- **THEN** "What happened" shows the base stopped at the weight target and the second stopped by hand

### Requirement: The comparison shows quality badges that differ

"What happened" SHALL show each shot-quality badge (pour truncated, channeling, grind issue,
skip-first-frame) that is raised on some visible shots but not others,
and SHALL NOT list badges raised on none or on all of them.

#### Scenario: Channeling went away

- **WHEN** the base was flagged for channeling and the newer shot was not
- **THEN** a channeling row shows the flag on the base and its absence on the newer shot

### Requirement: Rating, taste and notes are shown compactly

Rating and taste taps (balance, body) SHALL share one row when any visible shot records them.
Notes SHALL be shown as a quote per shot below the table, not as table cells.

#### Scenario: Rated and tasted

- **WHEN** the newer shot is rated 82% and tapped sweet and thin
- **THEN** its rating cell reads "82% · sweet, thin"

### Requirement: The graph can align shots at the start of the pour

The comparison graph's options SHALL offer lining the shots up at the start of the pour instead
of at the start of the shot, so a preinfusion length difference does not offset every later
curve. Shots without a recorded pour start SHALL stay aligned at shot start.

#### Scenario: Different preinfusion lengths

- **GIVEN** a base whose pour began at 5.0 s and a shot whose pour began at 6.4 s
- **WHEN** the user turns on alignment at pour start
- **THEN** both shots' pour phases begin at the same point on the time axis

### Requirement: The crosshair readout sits under the plot

Tapping or dragging on the graph SHALL show the time and, for each visible shot, its value for
each curve that is on, in a strip directly under the plot inside the graph card. It SHALL NOT
cover the plot, SHALL keep one line per visible shot so inspecting never moves the layout, and
a shot with no value at that time SHALL show a muted placeholder rather than losing its line.

#### Scenario: Inspecting without scrolling

- **WHEN** the user taps the graph at 12.7 s with pressure, flow and weight shown
- **THEN** the strip under the plot shows 12.7 s and those three values for each shot
- **AND** the comparison sections stay where they were on screen

#### Scenario: A shot that has already ended

- **GIVEN** a base shot that ended at 1.6 s
- **WHEN** the user inspects 32.7 s
- **THEN** the base keeps its line in the strip, with a placeholder for each value

### Requirement: Curve, phase and shot visibility controls are compact

Curve and phase visibility SHALL be one row of toggle chips directly under the graph. Each shot's
visibility on the graph SHALL be a toggle on its header card, alongside making it the base.

#### Scenario: Hiding a shot

- **WHEN** the user taps the hide control on a shot's header card
- **THEN** that shot's curves are hidden on the graph
- **AND** its comparison column remains

### Requirement: The graph stays in view while the comparison scrolls

The graph, its readout and its controls SHALL stay on screen above the comparison, which
scrolls on its own. The graph SHALL take at most about half the page height, so the
comparison always has room.

#### Scenario: A tall saved graph

- **GIVEN** the user previously dragged the graph taller than half the window
- **WHEN** the comparison opens
- **THEN** the graph is limited to about half the height and the comparison is visible below it

### Requirement: A shot can be compared with the previous shot in one tap

Shot Detail and Post-Shot Review SHALL offer "Compare with previous shot", opening the comparison
with the preceding shot on the same profile and equipment as the base. The option SHALL be hidden
when no such shot exists.

#### Scenario: Comparing the shot just pulled

- **WHEN** the user taps "Compare with previous shot" on Post-Shot Review
- **THEN** the comparison opens with the previous shot on the same profile and equipment as base
- **AND** the shot just pulled beside it

#### Scenario: First shot on this setup

- **WHEN** no earlier shot exists on the same profile and equipment
- **THEN** "Compare with previous shot" is not offered

### Requirement: Δ colour shows direction only

A Δ SHALL be shown signed, at the precision of its value. Its colour SHALL distinguish only
increase from decrease; it SHALL NOT imply that either direction is better. A Δ of zero at the
shown precision SHALL be neutral.

#### Scenario: Lower peak pressure is not coloured as good or bad

- **WHEN** a shot's peak pressure is 1.25 bar below the base
- **THEN** its Δ reads −1.25 in the decrease colour, the same colour any decrease uses

### Requirement: An unrated shot is not shown as rated zero

A shot with no rating SHALL show "—" for rating, never "0%". The rating row SHALL be hidden when
no visible shot is rated, and a Δ SHALL be shown only between two rated shots.

#### Scenario: Neither shot rated

- **WHEN** neither visible shot has a rating
- **THEN** the rating row is not shown

#### Scenario: Only the base rated

- **WHEN** the base is rated 70% and the other shot is unrated
- **THEN** the other shot shows "—" for rating with no Δ

### Requirement: The graph distinguishes the base shot

The comparison graph SHALL draw the base shot's curves heavier than the other shots' curves and
SHALL keep each shot's existing line pattern. Changing the base SHALL update which curves are
drawn heavier.

#### Scenario: Re-basing updates the heavy curves

- **WHEN** the user makes the newest shot the base
- **THEN** the newest shot's curves are drawn heavier and the oldest shot's are not

### Requirement: The web compare page presents the same comparison

The web `/compare/` page SHALL present the same base selection, summary line, "What you
changed", "Unchanged" line and "What happened" sections with "Show more", the same in-graph
readout of only the curves that are on, the same chip row of curve and phase toggles, eye
toggles on shot headers, and the same values, Δs and equality rules as the app.

#### Scenario: Web readout matches the app

- **WHEN** the user hovers or taps the web graph with pressure and flow turned on
- **THEN** a panel inside the graph shows only pressure and flow per visible shot
- **AND** no separate readout table appears below the graph

#### Scenario: App and web agree

- **WHEN** the same shots are compared in the app and on the web page with the same base
- **THEN** both show the same changed inputs, metrics and Δs

### Requirement: The web compare page puts graph and comparison side by side when wide

On a browser window wide enough for both, the web compare page SHALL show the graph and the
comparison sections side by side, graph on the left, with the graph staying in view while the
comparison scrolls. On a narrower window they SHALL stack, graph first, with no horizontal page
scroll.

#### Scenario: Desktop browser

- **WHEN** the compare page is opened in a wide desktop browser window
- **THEN** the graph is on the left and "What you changed"/"What happened" are on the right

#### Scenario: Phone browser

- **WHEN** the compare page is opened on a phone
- **THEN** the graph is shown above the comparison sections and the page does not scroll sideways

### Requirement: The MCP comparison reports changes and metrics against the base

The `shots_compare` tool SHALL report, relative to the oldest requested shot, which inputs each
other shot changed (old and new values), its comparison metrics and their Δs, how it stopped,
differing quality badges and the profile settings that changed, by the app's rules and with
unit-bearing field names. This replaces the shot-to-previous-shot changes list.

#### Scenario: A client sees the grind change and its effect

- **WHEN** a client compares two shots differing only in grind 10 → 9
- **THEN** the response names the grind change from 10 to 9 for the newer shot
- **AND** reports that shot's duration and yield Δs in seconds and grams
