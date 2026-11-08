# Spec Delta

## MODIFIED Requirements

### Requirement: Explicit acceptance applies only the listed changes
The confirmation SHALL offer keeping the next shot unchanged or applying the displayed proposal. Accepting SHALL apply only the listed corrections and explicitly disclosed persistent effects; declining SHALL change no next-shot setting. A bag selection SHALL neither load its other settings nor write preserved settings to that bag as a consequence of selection.

#### Scenario: Corrected bag with unrelated saved dial-in values
- **GIVEN** the next shot uses 19 g, grind 10 and a 59 g absolute target
- **AND** the corrected bag stores a different dose and yield
- **WHEN** the user accepts a bean-only correction
- **THEN** the next shot uses the corrected bag and retains 19 g, grind 10, its equipment and 59 g absolute target
- **AND** the corrected bag's other saved settings are not rewritten

#### Scenario: Corrected bag on an active recipe
- **WHEN** the user accepts a bean correction that explicitly lists an active recipe's bag-link update
- **THEN** the recipe remains active and is linked to the corrected bag
- **AND** the bag change does not alter the recipe's other fields or the live brew settings

#### Scenario: Accepted dial correction survives equipment selection
- **WHEN** the user accepts a grind or RPM correction with equipment selected
- **THEN** the confirmation discloses the corrected equipment package's saved dial values
- **AND** those explicitly corrected fields are saved to the equipment and any applicable recipe, never to the bag
- **AND** reselecting that equipment preserves the accepted dial
- **AND** a bean-only correction does not write equipment dial values
