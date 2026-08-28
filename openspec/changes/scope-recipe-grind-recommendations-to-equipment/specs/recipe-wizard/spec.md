## MODIFIED Requirements

### Requirement: Details step prefills from history, then bag data, then profile defaults
The details step SHALL seed its fields in priority order: (1) the most recent shot with the chosen bean+profile pair (dose, yield, temperature, grind); (2) for tea, the bag's structured brewing data — temperature from `brewTempC`, dose computed from `leafGramsPer100Ml` and the target volume; (3) the profile's recommended dose, target weight, and temperature. For coffee drinks the grind section SHALL additionally show a grind hint from the latest grind dialed for this bean on the selected complete equipment package, regardless of profile (falling back to same-roast-level beans on that package), naming the profile it was dialed for. If no such full-package hint exists, it SHALL retry the same bean/same-roast lookup against packages with the same grinder and basket, ignoring puck-preparation differences. A same grinder SHALL include the same brand, model, and burrs; a same basket SHALL include the same brand and model. A shot from a different grinder or basket SHALL NOT supply a hint. When equipment is selected and neither lookup qualifies, the hint SHALL be absent. When the recipe deliberately has no selected equipment, the hint MAY use the existing equipment-agnostic bean/same-roast lookup. If the equipment selection changes while the wizard is open, a result requested for the previous selection SHALL NOT be shown. When the hint's profile differs from the picked one and both have known UGS positions, it SHALL include the relative direction ("finer"/"coarser"). The hint SHALL never present a computed grinder number for a different profile (the KB's own cross-profile rule: only direction translates). When no matching shot history exists for the chosen bean+profile pair, the grind/rpm fields SHALL fall back to the linked bag's current `grinderSetting`/`rpm` as a one-time editable default (recipe-model's "New-recipe grind defaults from the bag, once") — offered, not silently applied; the user may accept or change it before saving. With no linked bag and no history, the fields start empty. For portafilter tea, the bag's `brewTempC` SHALL seed a temperature override only when the chosen profile is not type-matched to the bag's tea type; hot-water tea SHALL use the bag's brewing numbers verbatim. Prefilled values SHALL never overwrite a value the user has already edited in this wizard session.

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

#### Scenario: No shot history falls back to the bag's current dial
- **WHEN** the user creates a recipe for a bean+profile pair with no prior shot history, and the linked bag's current grind is "18"
- **THEN** the grind field prefills "18" as a one-time default, not a live-following value
