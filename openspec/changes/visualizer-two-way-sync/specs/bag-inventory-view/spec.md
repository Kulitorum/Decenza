## ADDED Requirements

### Requirement: Finished bags are listed and can be restocked

The Beans window SHALL offer a "Show finished (N)" toggle, shown when any bag has `inInventory = false`, that lists the finished bags as dimmed cards in most-recently-used order. Tapping a finished card SHALL open it for editing. A finished card's actions SHALL be Restock, Edit and (for a linked bag) bean details. Restock SHALL open the new-bag form prefilled from the finished bag — identity, bean details, equipment and dial-in — with the roast date, freezer dates, opened date and notes left blank; saving it SHALL create a new bag. The finished bag SHALL stay finished, with its shots.

#### Scenario: Restocking a coffee whose bag was finished
- **GIVEN** a finished bag of Saka Gran Bar with a recipe linked to it
- **WHEN** the user opens Show finished, taps Restock on it, enters the new roast date and saves
- **THEN** a new Saka Gran Bar bag is in inventory with the old bag's details and dial-in, and the old bag is still finished

#### Scenario: No finished bags
- **WHEN** every bag is in inventory
- **THEN** no Show finished toggle is shown

### Requirement: A failed bag read is not an empty inventory or a missing bag

A bag query that fails, or a database that does not open, SHALL be reported as a failure: the inventory SHALL NOT be shown as empty, and the active bag selection SHALL NOT be cleared. Only a bag row that is genuinely absent SHALL count as not found.

#### Scenario: Bag table cannot be read
- **GIVEN** a database whose bag table cannot be queried
- **WHEN** the Beans window opens and the active bag is reloaded
- **THEN** the window says the bags could not be read, not "No bags yet", and the active bag stays selected
