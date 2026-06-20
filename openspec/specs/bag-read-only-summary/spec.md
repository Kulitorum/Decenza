# bag-read-only-summary Specification

## Purpose
TBD - created by archiving change bean-bag-inventory. Update Purpose after archive.
## Requirements
### Requirement: Read-only bean summary replaces editable fields in shot contexts
All editable bean fields SHALL be removed from brew settings, post-shot review, and shot detail pages. Each SHALL display a `BeanSummary` component showing the active (or shot's) bag state.

#### Scenario: Editable fields removed
- **WHEN** any shot context page is displayed
- **THEN** no editable text field for roaster name, coffee name, roast date, or roast level SHALL be present

### Requirement: Bean summary adapts to data confidence
The `BeanSummary` component SHALL display progressively less content as data becomes more complete, following the "show less when more is known" principle.

#### Scenario: Full canonical with freeze tracking
- **WHEN** the bag has `beanBaseId`, canonical attributes (origin, variety, process), and `defrostDate`
- **THEN** the summary SHALL render as a single dense line: "{coffeeName} · {origin} · {process} · Roasted {N}d · Def {M}d" with a canonical verified badge

#### Scenario: Canonical without freeze
- **WHEN** the bag has `beanBaseId` and canonical attributes but no `defrostDate`
- **THEN** the summary SHALL render: "{coffeeName} · {origin} · {process} · Roasted {N}d"

#### Scenario: History only, no canonical
- **WHEN** the bag has no `beanBaseId`
- **THEN** the summary SHALL render: "{roasterName} {coffeeName} · Roasted {N}d"
- **AND** a subtle "Link to Bean Base" nudge SHALL appear below the summary line

#### Scenario: No roast date
- **WHEN** the bag has no `roastDate`
- **THEN** the roast age portion SHALL be omitted — no "Roasted unknown" placeholder

#### Scenario: No bag selected
- **WHEN** `activeBagId` is null or unset
- **THEN** the summary SHALL display "No beans selected"
- **AND** the "Change Beans" button SHALL be labelled "Select Beans"

### Requirement: Change Beans button present in all shot contexts
Every shot context displaying a `BeanSummary` SHALL include a "Change Beans" button adjacent to the summary. On historical shot detail pages the action re-links that shot only; selection semantics per context are defined in the `change-beans-dialog` spec.

#### Scenario: Change Beans opens dialog
- **WHEN** the user taps "Change Beans"
- **THEN** the Change Beans dialog SHALL open
- **AND** on bag selection, the summary SHALL update immediately to reflect the newly selected bag

