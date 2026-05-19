## ADDED Requirements

### Requirement: Rendered calibration section SHALL constrain how the model uses UGS

When the enriched user prompt includes a `grinderCalibration` block, the rendered calibration section SHALL carry explicit usage constraints that prevent the model from using UGS in ways it was not intended. The constraints SHALL be stated as directives, not background prose, and SHALL be byte-stable and present on both the in-app advisor and `dialing_get_context` surfaces.

The rendered section SHALL state, at minimum:

- UGS is a **relative ordering** of profiles by grind coarseness, not a grinder click count or an absolute dial position.
- Numeric grinder settings are valid **only within the stated `calibratedUgsRange`**. The model SHALL NOT compute, infer, or quote a grinder number for any profile reported with `source: "directional"`.
- For a `"directional"` profile the model SHALL give only relative direction (finer/coarser) and SHALL recommend pulling a reference shot on the target profile to establish a number.
- The model SHALL NOT multiply a UGS distance by any factor of its own to produce a setting; the only sanctioned arithmetic is the system-provided `conversionKey` applied within the validated range.
- When `confidence` is `"directional"`, the model SHALL NOT present any grinder number for a profile switch and SHALL say a number cannot be given without more dial-in data on the current coffee.
- Directional guidance SHALL be expressed only as a grind-size term (finer/coarser). The model SHALL NOT translate it into a dial-number change ("go up N", "turn coarser by 2") — that needs the grinder's numeric convention and reintroduces the #1223 sign risk; the `direction` field is anchor-free and already correct as finer/coarser.
- When a directional entry has no `direction` field (the current profile is not UGS-placed), the model SHALL state it cannot order the two profiles rather than guess.

The section SHALL repeat the block's `usageConstraint` string verbatim so a single directive governs every provider (Claude, Gemini, GPT, OpenRouter, Ollama) identically.

#### Scenario: Out-of-range profile renders as directional with no number

- **GIVEN** a `grinderCalibration` block whose `calibratedUgsRange` is `[0.0, 1.5]` and a `profiles` entry for "TurboTurbo" with `source: "directional"`, `direction: "coarser"`
- **WHEN** the calibration section is rendered into the user prompt
- **THEN** the section SHALL present "TurboTurbo" as "coarser, pull a reference shot" with no grinder number
- **AND** the section SHALL state that numbers are valid only within UGS 0.0–1.5
- **AND** the `usageConstraint` string SHALL appear verbatim

#### Scenario: Directional confidence suppresses all numeric switch advice

- **GIVEN** a `grinderCalibration` block with `confidence: "directional"` (no `conversionKey`)
- **WHEN** the calibration section is rendered
- **THEN** the section SHALL instruct the model to give only finer/coarser direction for any profile switch
- **AND** the section SHALL state that a specific grinder number cannot be given without more dial-in data on the current coffee
- **AND** the rendered section SHALL contain no numeric grinder settings

#### Scenario: No-anchor directional guidance is correct grind-size language

- **GIVEN** a `grinderCalibration` block with `confidence: "directional"`, no `conversionKey`, no `coffeeAnchor`, current profile "D-Flow / Q", and a "TurboTurbo" entry `direction: "coarser"`
- **WHEN** the calibration section is rendered
- **THEN** the section SHALL tell the model TurboTurbo is coarser than the current profile and to pull a reference shot
- **AND** the section SHALL contain no dial-number delta and no grinder setting
- **AND** the guidance SHALL be correct without reference to the grinder's finer-direction convention

#### Scenario: Constraint wording is byte-stable across surfaces

- **GIVEN** the same `grinderCalibration` block
- **WHEN** rendered via the in-app advisor enrichment path and via `dialing_get_context`
- **THEN** the calibration section text including the usage constraints SHALL be byte-identical between the two surfaces
