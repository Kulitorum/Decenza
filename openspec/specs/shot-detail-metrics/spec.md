# shot-detail-metrics Specification

## Purpose
TBD - created by archiving change improve-grind-visibility-shot-review. Update Purpose after archive.
## Requirements
### Requirement: Shot detail metrics row shows grind setting
The shot detail page SHALL display the shot's grind setting as a top-level cell in the metrics row above the graph, positioned between Dose and Output, using the same caption-label-over-value style as the existing metric cells.

#### Scenario: Shot with a recorded grind setting
- **WHEN** the user opens a shot whose record has a non-empty grinder setting
- **THEN** the metrics row shows a "Grind" cell displaying that setting between the Dose and Output cells

#### Scenario: Shot without a recorded grind setting
- **WHEN** the user opens a shot whose record has no grinder setting
- **THEN** the Grind cell is not shown and the metrics row reflows to the remaining metrics

#### Scenario: Long free-text grind setting
- **WHEN** the shot's grind setting is a long free-text value that would exceed the cell's maximum width
- **THEN** the displayed value is elided rather than pushing the other metric cells out of view, and the full value remains readable in the Equipment card

### Requirement: Grind cell shows RPM as a caption suffix
When the shot has a recorded grinder RPM greater than zero, the Grind cell SHALL append the RPM as a caption-styled, baseline-aligned suffix after the grind setting (e.g. `5.5 · 340 rpm`), matching the Shot Plan's grind format.

#### Scenario: Shot with grind setting and RPM
- **WHEN** the user opens a shot with grinder setting "5.5" and RPM 340
- **THEN** the Grind cell shows "5.5" in the standard metric value style followed by "· 340 rpm" in caption style

#### Scenario: Shot with grind setting but no RPM
- **WHEN** the user opens a shot with a grinder setting and an RPM of zero or unset
- **THEN** the Grind cell shows only the grind setting with no RPM suffix

### Requirement: Grind cell is accessible
The Grind cell SHALL follow the metrics row's accessibility pattern: the cell exposes a single combined static-text accessible name including the label, the setting, and the RPM when present, with its inner visual items ignored by assistive technology.

#### Scenario: Screen reader reads the grind metric
- **WHEN** a screen reader focuses the Grind cell of a shot with setting "5.5" and RPM 340
- **THEN** it announces a single item conveying "Grind: 5.5, 340 rpm" rather than separate fragments

### Requirement: Equipment card keeps its grind dial-in line
The shot detail page's Equipment card SHALL continue to display the grind setting and RPM line alongside grinder identity, independent of the metrics-row cell.

#### Scenario: Grind shown in both locations
- **WHEN** the user opens a shot with a recorded grind setting and views the Equipment card
- **THEN** the card still shows the grind dial-in line (un-elided), in addition to the metrics-row Grind cell

