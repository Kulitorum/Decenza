## MODIFIED Requirements

### Requirement: Details step prefills from history, then bag data, then profile defaults
The details step SHALL seed its fields in priority order: (1) the most recent shot with the chosen bean+profile pair (dose, yield, temperature, grind); (2) for tea, the bag's structured brewing data — temperature from `brewTempC`, dose computed from `leafGramsPer100Ml` and the target volume; (3) the profile's recommended dose, target weight, and temperature. For coffee drinks the grind section SHALL additionally show a grind hint: the latest grind dialed for this bean regardless of profile (falling back to same-roast-level beans), naming the profile it was dialed for, plus — when that profile differs from the picked one and both have known UGS positions — the relative direction ("finer"/"coarser") per the knowledge base's UGS ordering. The hint SHALL never present a computed grinder number for a different profile (the KB's own cross-profile rule: only direction translates). When no matching shot history exists for the chosen bean+profile pair, the grind/rpm fields SHALL fall back to the linked bag's current `grinderSetting`/`rpm` as a one-time editable default (recipe-model's "New-recipe grind defaults from the bag, once") — offered, not silently applied; the user may accept or change it before saving. With no linked bag and no history, the fields start empty. For portafilter tea, the bag's `brewTempC` SHALL seed a temperature override only when the chosen profile is not type-matched to the bag's tea type; hot-water tea SHALL use the bag's brewing numbers verbatim. Prefilled values SHALL never overwrite a value the user has already edited in this wizard session.

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
- **WHEN** the bean's last grind was 15 dialed for D-Flow and the user picked Rao Allongé
- **THEN** the grind section shows the 15 (naming D-Flow) and that Allongé typically grinds coarser — no computed number for Allongé

#### Scenario: No shot history falls back to the bag's current dial
- **WHEN** the user creates a recipe for a bean+profile pair with no prior shot history, and the linked bag's current grind is "18"
- **THEN** the grind field prefills "18" as a one-time default, not a live-following value

### Requirement: Drink-type-specific details fields
The details step SHALL show only the fields relevant to the drink type: espresso and filter — dose, yield, temperature, grind (recipe-owned, default-filled per the recipe model — no inherit/override toggle), equipment; americano and long black — the espresso fields plus the water-vessel picker with the order fixed by the type; latte/cappuccino — the espresso fields plus milk weight and pitcher; tea (portafilter) — leaf dose, yield, temperature, equipment, and NO grind fields; tea (hot water) — vessel, volume, temperature, leaf dose only.

#### Scenario: Tea hides grind
- **WHEN** the user reaches the details step of a portafilter tea recipe
- **THEN** no grind or rpm fields are shown and the saved recipe stores no pinned grind

## ADDED Requirements

### Requirement: Equipment precedes grind in the details layout
The details step SHALL present the equipment selection before (above) the grind/rpm fields, so the recipe's grinder rpm-capability is known — and the rpm field's visibility is correct — the first time the user reaches the grind fields, rather than only after separately visiting the equipment section further down the step.

#### Scenario: Rpm field is correct on first view
- **WHEN** the user reaches the details step for a drink type with no per-drink-type equipment default yet, and their only/selected grinder is rpm-capable
- **THEN** the equipment section (already resolved or explicitly chosen) appears before the grind fields, and the rpm field is visible when the user reaches it — not hidden until they scroll past the grind card to equipment

#### Scenario: Changing equipment after grind entry updates rpm visibility immediately
- **WHEN** the user has already entered a grind value and then changes the equipment selection to a non-rpm-capable grinder
- **THEN** the rpm field hides immediately, consistent with the newly selected equipment
