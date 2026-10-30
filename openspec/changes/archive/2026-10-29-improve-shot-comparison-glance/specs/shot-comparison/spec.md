# Spec Delta

## ADDED Requirements

### Requirement: Common inputs are visible with the app comparison graph

The shared app comparison page SHALL show grind setting, dose and saved brew-temperature setting in a compact summary adjacent to the graph, identifying each displayed shot and its values. It SHALL show missing values as unavailable, not zero, and SHALL NOT describe the temperature as an override or a measured group temperature.

#### Scenario: Graph and inputs together
- **WHEN** a comparison opens with two shots whose grind, dose and brew temperature are recorded
- **THEN** those values and each shot's identity are visible with the graph without scrolling to the detailed comparison

#### Scenario: A value is missing
- **WHEN** a displayed shot has no recorded grind setting
- **THEN** the summary marks its grind as unavailable

#### Scenario: Paging or rebasing
- **WHEN** the user pages through shots or selects a different base
- **THEN** the summary updates to identify the same displayed shots and base as the graph and detailed comparison

### Requirement: Graph inspection remains usable with the summary

The summary SHALL NOT cover the plot, crosshair readout or graph controls. It SHALL remain readable on narrow screens without horizontal page scrolling and SHALL NOT prevent the graph from being resized or inspected.

#### Scenario: Inspecting on a narrow screen
- **WHEN** the user inspects the graph on a narrow screen
- **THEN** the summary, graph controls and crosshair readout remain available and legible

## MODIFIED Requirements

### Requirement: Inputs identical across all visible shots collapse into one line

Inputs whose values are the same on every visible shot SHALL be shown once, on a single
"Unchanged" line, and SHALL NOT be repeated per shot in the detailed comparison. The app's
graph-adjacent summary may repeat its fixed input values per shot for immediate identification.

#### Scenario: Shared setup is shown once

- **GIVEN** two shots with the same dose, bean, roast, grinder and RPM
- **WHEN** the comparison is shown
- **THEN** those values appear once on the "Unchanged" line
- **AND** they do not appear as rows in either section
