# Spec Delta

## MODIFIED Requirements

### Requirement: Automatic milk capture before steaming

The system SHALL capture milk only for a sample the user requests, from the review-page milk button or the idle-screen steam pitchers (Steam row or compact Steam popup). The pitcher SHALL be weight-timed with a saved empty weight. Each request SHALL confirm a settled sample and lock the scaled time, and SHALL replace it when the reading moves by more than 2 g and settles again while the pitcher stays on the scale. It SHALL end when the pitcher is lifted after a capture (a tare or a scale disconnect is not a lift), when its surface closes, or when any operation other than a wand purge starts. Steam mode, the steam page and a finished steam SHALL NOT start one.

#### Scenario: Milk settles and the time locks
- **WHEN** the user opens the idle pitchers and the pitcher with milk holds steady within tolerance for the dwell
- **THEN** the steam timeout is set to the scaled value, a ding + banner are shown, and the milk is recorded for this session

#### Scenario: No double capture across pages
- **WHEN** the steam page is opened over the home screen while steam mode is active
- **THEN** the idle page's request ends and nothing is captured on the steam page

#### Scenario: Cup after a steam is not taken for milk
- **WHEN** a steam has just ended, no sample is requested, and the user sets an espresso cup on the scale
- **THEN** nothing is captured and the steam time is unchanged

#### Scenario: One sample per request
- **WHEN** milk was captured from the idle pitchers, the pitcher is lifted, and the user then sets a different load on the scale with the pitchers still showing
- **THEN** nothing further is captured until the pitchers are opened again

#### Scenario: Milk added after a capture replaces it
- **WHEN** milk was captured, and the user pours more into the pitcher on the scale and the reading settles
- **THEN** the new weight replaces the capture and the steam time is scaled to it

#### Scenario: Top-up after a manual adjustment
- **WHEN** the user tapped ±5 after a capture and then pours more into the pitcher on the scale
- **THEN** the new weight is recorded and the adjusted time is kept until the pitcher is lifted

#### Scenario: Steam start ends the request
- **WHEN** a sample is requested and steam starts from the group head before anything is captured
- **THEN** the request ends as the steam starts, and the steam uses the earlier capture or the preset time

#### Scenario: Pitcher without an empty weight
- **WHEN** the user opens the idle pitchers with a pitcher selected that has no saved empty weight
- **THEN** nothing is captured

#### Scenario: The bar Steam popup behaves like the Home row
- **WHEN** the Steam widget sits in a bar, the user opens its pitcher popup and taps a different pitcher
- **THEN** the popup stays open with that pitcher selected and its sample is captured; only tapping the selected pitcher (start steam) or Purge closes it

#### Scenario: Closing the pitchers ends the request
- **WHEN** the user opens the idle pitchers and closes them before a sample is captured
- **THEN** the request ends and the session's milk weight is what it was before
