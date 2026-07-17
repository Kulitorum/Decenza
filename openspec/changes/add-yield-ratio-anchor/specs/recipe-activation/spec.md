# recipe-activation Delta

## ADDED Requirements

### Requirement: Activation applies the recipe's yield anchor after the dose lands

Activation SHALL apply the recipe's yield spec (`yield-anchor`) to the session anchor verbatim — value **and** mode — so an activated recipe opens with its saved yield, and a ratio-moded recipe stays ratio-moded.

When the recipe's mode is `ratio`, the gram target SHALL be derived **after** the recipe's dose has landed, never against the dose in effect before activation. Today `applyActivatedRecipe` writes the yield synchronously while the dose is deferred to a queued write (so that it beats the profile's own deferred `recommendedDose`); an inline `dose × ratio` at the synchronous point would multiply a stale, pre-activation dose. The derivation SHALL therefore happen in the same queued step as the dose, or resolve against the recipe's own `doseG` directly rather than reading it back from settings.

Applying a `ratio` anchor SHALL NOT be suppressed when the derived target happens to equal the active profile's `target_weight`. The "a value matching the profile default is not an override" rule applies to `absolute` yields only — for a ratio it would silently discard the anchor exactly when it coincides with the profile, taking the dose-tracking behaviour with it.

When the recipe's mode is `none`, activation SHALL leave the ladder to fall through to the bag, then the profile — arming no yield anchor of its own.

#### Scenario: A ratio recipe activates ratio-anchored
- **WHEN** a recipe holding `{2.0, ratio}` with a `doseG` of 18 is activated
- **THEN** the session anchor is `{2.0, ratio}`, the dose is 18 g, and the target is 36 g
- **AND** a subsequent dose capture of 17.5 g re-derives the target to 35 g

#### Scenario: The ratio resolves against the recipe's dose, not the previous one
- **WHEN** the live dose is 20 g and a recipe holding `{2.0, ratio}` with a `doseG` of 18 is activated
- **THEN** the resulting target is 36 g (2.0 × the recipe's 18 g), never 40 g (2.0 × the stale 20 g)

#### Scenario: A derived target equal to the profile default stays anchored
- **WHEN** a recipe holding `{2.0, ratio}` with a `doseG` of 18 is activated on a profile whose `target_weight` is 36 g
- **THEN** the session anchor is `{2.0, ratio}` and `hasBrewYieldOverride` is true
- **AND** a subsequent dose change still re-derives the target

#### Scenario: An absolute recipe activates absolute-anchored
- **WHEN** a recipe holding `{36.0, absolute}` is activated
- **THEN** the session anchor is `{36.0, absolute}`
- **AND** a subsequent dose capture of 17.5 g leaves the target at 36 g

#### Scenario: A recipe with no yield falls through the ladder
- **WHEN** a recipe whose yield mode is `none` is activated while the active bag holds `{3.0, ratio}`
- **THEN** the session anchor is the bag's `{3.0, ratio}`

#### Scenario: A recipe with no yield and no bag anchor uses the profile
- **WHEN** a recipe whose yield mode is `none` is activated and the active bag's mode is also `none`
- **THEN** the effective yield is the profile's `target_weight`
