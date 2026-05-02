# Spec Delta: shot-metadata-capture

## ADDED Requirements

### Requirement: Conversational bean-metadata correction parser

The advisor SHALL parse user replies for explicit bean-identity corrections and return a sparse correction struct so the app can persist the correction back to the anchored shot.

#### Scenario: Roast level extraction
- **WHEN** the user reply contains "actually it's really dark" (or "the coffee is medium-dark", "this is a light roast")
- **THEN** the parser returns a correction with `roastLevel` set to the canonical value ("Dark", "Medium-Dark", "Light")

#### Scenario: Compound phrases are not roast-level corrections
- **WHEN** the user reply contains "dark chocolate notes" or "light citrus" or "medium body" without a context word linking the adjective to roast level
- **THEN** the parser does not extract a `roastLevel` correction

#### Scenario: Bean brand extraction
- **WHEN** the user reply contains "actually it's from Sey" or "the roaster is Onyx Coffee Lab"
- **THEN** the parser returns a correction with `beanBrand` set to the captured name (trimmed, ≤60 chars)

#### Scenario: Roast date extraction
- **WHEN** the user reply contains "roasted 2026-04-15" or "roasted on April 15, 2026" or "roasted March 5"
- **THEN** the parser returns a correction with `roastDate` as an ISO date string (year defaulting to the current year when omitted)

#### Scenario: Multiple fields in one reply
- **WHEN** the user reply contains both a roast-level correction and a brand correction
- **THEN** the parser returns a correction with all detected fields set

#### Scenario: Unrelated reply
- **WHEN** the user reply contains no recognised bean-correction patterns (e.g. "really good shot, balanced")
- **THEN** the parser returns `std::nullopt`

### Requirement: Conversational bean-metadata write-back

When a bean correction is parsed AND the conversation turn is anchored to a known shot, the system SHALL persist the correction to the shot's metadata via the existing background write path.

#### Scenario: Correction persists on anchored turn
- **WHEN** the user reply parses to a non-empty `BeanCorrection` AND the active turn has a non-zero `shotId`
- **THEN** the system calls `ShotHistoryStorage::requestUpdateShotMetadata(shotId, fields)` with the corrected fields

#### Scenario: No write when shotId is absent
- **WHEN** the user reply parses to a `BeanCorrection` BUT the active turn has no anchored shotId
- **THEN** the system does not write back

#### Scenario: Gate on bean context
- **WHEN** the user reply does not start with a corrective phrasing AND the prior assistant message did not ask about beans
- **THEN** the system does not write back, even if the parser would have matched

### Requirement: Acknowledgement teaching in system prompt

The advisor system prompt SHALL teach the model to acknowledge bean-metadata writes in the next assistant reply and to trust `currentBean.*` on subsequent turns over the user's last-turn phrasing.

#### Scenario: Acknowledgement guidance is in the prompt
- **WHEN** the system prompt is built for shot analysis
- **THEN** it contains a "Conversational metadata corrections" subsection instructing the model to confirm bean-field updates with a one-line acknowledgement and to rely on the next-turn envelope's `currentBean.*` for subsequent reasoning

### Requirement: Profile family classification in catalog

Each profile entry in `resources/ai/profile_knowledge.md` SHALL declare a `Family:` line classifying its underlying mechanic. The rendered profile catalog SHALL surface this family alongside the existing category and roast guidance so the model can identify mechanically-equivalent profiles at a glance.

#### Scenario: Family field is present on every catalog profile
- **WHEN** profile knowledge is loaded
- **THEN** every parsed profile entry has a non-empty `family` value drawn from a small fixed set (`lever-decline`, `pressure-ramp-flow`, `flow-adaptive`, `blooming`, `flat-pressure`, `turbo`, `filter`, `allonge`, `manual`, `volume-based`, `gentle-long-preinfusion`, `tea`, `maintenance`)

#### Scenario: Catalog rendering surfaces family
- **WHEN** `buildProfileCatalog` produces the one-liner per profile
- **THEN** each line carries a `[family: <name>]` tag (or equivalent surface) so the model sees family alongside category and roast guidance

#### Scenario: Within-family rule is taught
- **WHEN** the system prompt is built for shot analysis with the catalog appended
- **THEN** it contains a "Profile families" subsection instructing the model not to recommend within-family profile switches as a meaningful change unless the alternative encodes a constraint that cannot be replicated by adjusting parameters on the current profile

### Requirement: Other-profile parameter discipline

The advisor SHALL be taught not to quote numeric setpoints (temperatures, pressures, flow rates, durations) of profiles it does not have a full recipe for.

#### Scenario: Anti-hallucination rule is in the prompt
- **WHEN** the system prompt is built for shot analysis
- **THEN** it contains an "Other-profile parameter discipline" subsection instructing the model that it has full recipe data only for the current shot's profile, and that for other profiles it MUST recommend in qualitative terms (e.g., "lower temperature", "higher peak pressure") rather than invent specific numeric values
