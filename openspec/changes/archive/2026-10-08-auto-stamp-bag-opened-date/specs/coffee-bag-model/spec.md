## ADDED Requirements

### Requirement: The active bag moves when it is finished
When the active bag is finished, the active selection SHALL move to the newest other in-inventory bag of the same coffee (canonical id, else roaster + coffee; `CoffeeBagStorage::successorBagStatic`, the same lookup recipes roll to), and SHALL be cleared when there is none, so later shots are never recorded against a finished bag.

#### Scenario: Finishing a bag with a restock waiting
- **GIVEN** the active bag and a newer in-inventory bag of the same coffee
- **WHEN** the active bag is marked finished
- **THEN** the newer bag SHALL become active and recipes using the finished bag SHALL point at it

#### Scenario: Finishing the last bag of a coffee
- **WHEN** the active bag is finished and no other bag of that coffee is in inventory
- **THEN** no bag SHALL be active

### Requirement: A new frozen bag does not take over from the bag in use
Creating a bag that is stored frozen SHALL NOT make it the active bag while another bag is active; a bag that is not frozen, or the first bag, still becomes active as before.

#### Scenario: Adding a delivery to the freezer
- **GIVEN** an active bag in use
- **WHEN** the user adds a new bag with freezing on
- **THEN** the active bag SHALL stay the one in use
