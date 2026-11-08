# Spec Delta

## REMOVED Requirements

### Requirement: Details step prefills from history, then bag data, then profile defaults
**Reason**: Superseded by the updated requirement with clear scenario names.
**Migration**: Follow Recipe details prefill from history and brewing defaults.

### Requirement: No-history grind default comes from the bag once
**Reason**: Bags no longer own grind.
**Migration**: Default to matching bean shot history when known.

## ADDED Requirements

### Requirement: Recipe details prefill from history and brewing defaults
The details step SHALL seed its fields in priority order: (1) the most recent shot with the chosen bean and profile pair (dose, yield, temperature); (2) for tea, the bag's structured brewing data; (3) the profile's recommended dose, target weight, and temperature. Prefilled values SHALL never overwrite a value the user has edited in this wizard session.

#### Scenario: History beats profile defaults
- **WHEN** the user picks a bean+profile pair they have brewed before
- **THEN** dose/yield/temp show the values from the most recent such shot, not the profile's recommendations

#### Scenario: Type-matched tea profile keeps its temperature
- **WHEN** a black tea bag stating 100°C is paired with the stock black-tea profile
- **THEN** no temperature override is seeded

#### Scenario: Generic tea profile gets corrected
- **WHEN** a sencha bag stating 70°C is paired with a generic tea profile at 94°C
- **THEN** the temperature field seeds 70°C as a recipe override

#### Scenario: Grind hint translates direction across profiles
- **WHEN** the bean's last grind on the selected equipment was 15 dialed for D-Flow and the user picked Rao Allongé
- **THEN** the grind section shows the 15 (naming D-Flow) and that Allongé typically grinds coarser — no computed number for Allongé

#### Scenario: No matching shot leaves the grind empty
- **WHEN** there is no matching shot with a recorded grind
- **THEN** the grind remains empty rather than adopting a bag dial

#### Scenario: Hot-water tea uses bag brewing numbers
- **WHEN** the user creates a hot-water tea recipe from a bag stating brewTempC and leafGramsPer100Ml
- **THEN** the fields use the bag's brewing numbers verbatim, with the leaf dose computed from leafGramsPer100Ml and the target volume

#### Scenario: Edited field is not overwritten
- **WHEN** the user has edited the dose and a prefill tier would otherwise seed it
- **THEN** the user's dose is kept

### Requirement: Last matching bean grind seeds the recipe
Interactive creation SHALL default to the most recent non-empty shot-history grind for the same bean across profiles and equipment, with its RPM and known profile/equipment context. It SHALL remain editable and SHALL NOT be replaced by a late response or a bag value.

#### Scenario: Most recent grind across profiles
- **WHEN** the newest matching bean grind was recorded on another profile or grinder
- **THEN** its grind and same-shot RPM seed the recipe with available context

#### Scenario: No qualifying shot
- **WHEN** no same-bean shot records a grind
- **THEN** grind remains empty

#### Scenario: Conflicting canonical identities
- **WHEN** bean and shot have different non-empty canonical IDs
- **THEN** the shot does not match even if names match

#### Scenario: Legacy shot without canonical ID
- **WHEN** a shot lacks an ID but has matching non-empty roaster and coffee names without identity ambiguity
- **THEN** it can match the bean

#### Scenario: Timestamp tie
- **WHEN** qualifying shots share the latest timestamp
- **THEN** the shot with the greater ID is selected

#### Scenario: Competing asynchronous results
- **WHEN** bean-profile and latest-bean queries finish in either order
- **THEN** only the latest-bean query can seed grind and RPM, and neither overwrites a user edit

#### Scenario: Stale selection or explicit clear
- **WHEN** the bean changes or the user clears grind before a lookup returns
- **THEN** the stale or unwanted result does not overwrite the current field
