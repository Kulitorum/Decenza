## MODIFIED Requirements

### Requirement: Details step prefills from history, then bag data, then profile defaults
The details step SHALL seed its fields in priority order: (1) the most recent shot with the chosen bean and profile pair (dose, yield, temperature, grind); (2) for tea, the bag's structured brewing data; (3) the profile's recommended dose, target weight, and temperature. Prefilled values SHALL never overwrite a value the user has edited in this wizard session.

#### Scenario: History beats profile defaults
- **WHEN** the user picks a bean+profile pair they have brewed before
- **THEN** dose/yield/temp/grind show the values from the most recent such shot, not the profile's recommendations

#### Scenario: Type-matched tea profile keeps its temperature
- **WHEN** a black tea bag stating 100°C is paired with the stock black-tea profile
- **THEN** no temperature override is seeded

#### Scenario: Generic tea profile gets corrected
- **WHEN** a sencha bag stating 70°C is paired with a generic tea profile at 94°C
- **THEN** the temperature field seeds 70°C as a recipe override

#### Scenario: Grind hint translates direction across profiles
- **WHEN** the bean's last grind on the selected equipment was 15 dialed for D-Flow and the user picked Rao Allongé
- **THEN** the grind section shows the 15 (naming D-Flow) and that Allongé typically grinds coarser — no computed number for Allongé

#### Scenario: No shot history falls back to the bag's current dial
- **WHEN** the user creates a recipe for a bean+profile pair with no prior shot history, and the linked bag's current grind is "18"
- **THEN** the grind field prefills "18" as a one-time default, not a live-following value

#### Scenario: Hot-water tea uses bag brewing numbers
- **WHEN** the user creates a hot-water tea recipe from a bag stating brewTempC and leafGramsPer100Ml
- **THEN** the fields use the bag's brewing numbers verbatim, with the leaf dose computed from leafGramsPer100Ml and the target volume

#### Scenario: Edited field is not overwritten
- **WHEN** the user has edited the dose and a prefill tier would otherwise seed it
- **THEN** the user's dose is kept

### Requirement: Grind hint names the last grind for the bean
For coffee drinks the grind section SHALL show a hint: the latest grind dialed for this bean on the selected equipment package regardless of profile, falling back to same-roast-level beans, naming its profile. Without a full-package match it SHALL retry on packages with the same grinder (brand, model, burrs) and basket (brand, model). The hint SHALL NEVER present a computed number for a different profile, only the relative direction when both profiles have UGS positions.

#### Scenario: Grind hint from a same-roast bean
- **WHEN** no shot with this bean has a grind but a same-roast bean does
- **THEN** the hint names that bean's last grind and its profile

#### Scenario: Full package hint wins
- **WHEN** a bean was last dialed at 17.5 on a package with different puck preparation and at 9.0 on the selected complete package
- **THEN** the grind section recommends 9.0 and never offers 17.5

#### Scenario: Puck-prep-only difference is an eligible fallback
- **WHEN** the selected package has no qualifying shot but a package with the same grinder and basket, differing only in puck preparation, was last dialed at 17.5
- **THEN** the grind section recommends 17.5

#### Scenario: Different grinder or basket leaves the hint absent
- **WHEN** neither the selected package nor any package with the same grinder and basket has a qualifying bean or same-roast shot, but a package with a different grinder or basket does
- **THEN** no grind-history hint is shown

#### Scenario: Equipment change drops a stale hint
- **WHEN** the user changes the equipment selection after a grind-history lookup begins
- **THEN** a reply for the earlier package is ignored and only a hint for the newly selected package may appear

#### Scenario: No equipment retains an equipment-agnostic hint
- **WHEN** the user deliberately chooses no equipment and bean history contains a qualifying grind
- **THEN** the grind section may show the latest bean or same-roast hint without package filtering
