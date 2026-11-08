# Spec Delta

## ADDED Requirements

### Requirement: Web recipe creation does not copy bag grind
Web recipe creation SHALL preserve explicitly supplied grind, including an explicit empty value, and SHALL NOT silently adopt grind or RPM from a bag.

#### Scenario: Create without a grind
- **WHEN** a client creates a bag-linked recipe without a grind
- **THEN** the bag does not provide a grind default

#### Scenario: Activate without equipment
- **WHEN** a web client activates a recipe with no equipment selection
- **THEN** the current equipment remains active and the recipe's own grind applies
