# Spec Delta

## MODIFIED Requirements

### Requirement: Capture attempts do not tare

Starting a capture attempt SHALL NOT tare the scale, whether it starts on the idle screen, in the compact steam widget, or from the review-page button. Every net-milk value read from the scale SHALL be measured above the learned empty reading when there is one.

#### Scenario: Cup or pitcher still on the scale at the tap
- **WHEN** the user taps the button with the cup or pitcher on the scale, then lifts it and sets down the pitcher with milk
- **THEN** no tare is sent and the captured weight is the milk only

#### Scenario: Scale not reading zero when empty
- **WHEN** the empty scale reads a few grams above zero and the user taps the button
- **THEN** the capture still starts and waits for the pitcher

#### Scenario: Milk readouts use the capture's empty reading
- **WHEN** after a shot the empty scale reads −120 g, Steam is selected on idle, and a 25 g pitcher with 150 g of milk is set down
- **THEN** the pitcher pill and a Weight widget in Net milk mode show 150 g

#### Scenario: Pitcher set down before the attempt starts
- **WHEN** the empty scale reads −450 g, the user sets down a 50 g pitcher holding 100 g of milk, and then selects Steam on idle or taps the review-page milk button
- **THEN** 100 g of milk is captured once the reading has settled

#### Scenario: Steam started before the capture settles
- **WHEN** the empty scale reads −450 g and the user starts steam within the settling time of setting down a 50 g pitcher holding 100 g of milk
- **THEN** the steam time is scaled to 100 g of milk

## ADDED Requirements

### Requirement: The empty scale reading is learned while a scale is connected
The empty reading SHALL be learned whenever a weighing scale is connected, including while no capture attempt is open, so a cup or pitcher already on the scale when an attempt starts is measured from it.

#### Scenario: Reading learned with no attempt open
- **WHEN** a weighing scale is connected, no capture attempt is open, and the empty scale settles at −450 g
- **THEN** −450 g becomes the empty reading that a later capture attempt measures from

### Requirement: A tare, scale switch or disconnect discards the empty reading
A tare, a scale switch or a disconnect SHALL discard the learned empty reading. Until a new one is learned, a cup or pitcher on the scale is handled by lifting it, after which the lower settled reading becomes the empty reference.

#### Scenario: Pitcher lifted after a tare
- **WHEN** the scale is tared with a pitcher on it, and the user then lifts the pitcher
- **THEN** the learned reading is discarded, and the lower settled reading with the pitcher lifted becomes the empty reference
