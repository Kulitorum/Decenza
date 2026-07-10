## ADDED Requirements

### Requirement: Shot detail and shot review headers show grind setting and RPM

The Shot Detail page header title and the Shot Review page header title SHALL each append the shot's grind setting, and its RPM when known, after the existing `<Profile Name> (<Temp>)` text, using the same `<grind> · <rpm> rpm` format as the metrics-row Grind cell. The appended `(<Temp>) · <grind> · <rpm> rpm` portion SHALL render at the same font size as the header's adjacent date text so it fits without overlapping adjacent header elements (date, quality badges).

#### Scenario: Shot with a recorded grind setting and RPM
- **WHEN** the user opens a shot with profile "Default", temperature override 90°C, grind setting "5.5", and RPM 340
- **THEN** the header title reads "Default (90°C) · 5.5 · 340 rpm" with the profile name at the normal title size and "(90°C) · 5.5 · 340 rpm" rendered at the same size as the date text

#### Scenario: Shot with a grind setting but no RPM
- **WHEN** the user opens a shot with a recorded grind setting and an RPM of zero or unset
- **THEN** the header title appends only the grind setting (e.g. "Default (90°C) · 5.5") with no RPM suffix

#### Scenario: Shot without a recorded grind setting
- **WHEN** the user opens a shot whose record has no grinder setting
- **THEN** the header title is unchanged from today's format: "<Profile Name> (<Temp>)" with no grind/RPM suffix

#### Scenario: Shot Review reflects an in-progress grind/RPM edit
- **WHEN** the user is editing a shot on the Shot Review page and changes the grind setting or RPM field before saving
- **THEN** the header title updates immediately to reflect the edited value, not the last-saved value

#### Scenario: Header remains a single eliding line
- **WHEN** the combined profile name, temperature, grind, and RPM text would overflow the available header width
- **THEN** the header text elides on the right, matching the existing header's overflow behavior

#### Scenario: Screen reader reads the appended grind/RPM
- **WHEN** a screen reader focuses the Shot Detail or Shot Review header of a shot with a recorded grind setting and RPM
- **THEN** it announces the grind and RPM as part of the header text, consistent with the visible title
