# bag-inventory-view Specification

## Purpose
Governs the Beans window's bag-inventory list that replaced the old preset list: cards that show less when more is known (canonical vs. partial data), the single life-stage-appropriate removal action per card (delete before any shot, "Bag finished" after), the idle-page MRU bag-pill widget, and in-place editing that never disturbs existing shots' immutable bean snapshots.

## Requirements
### Requirement: Beans window shows bag inventory
The Beans window SHALL display a list of bags where `inInventory = true`, replacing the current preset list.

#### Scenario: Inventory with canonical bags
- **WHEN** the Beans window is opened and the user has canonical-linked bags
- **THEN** each bag SHALL display: roaster name, coffee name, canonical verified badge, key attributes (origin · variety · process), roast age, and defrost age if `defrostDate` is set

#### Scenario: Inventory with non-canonical bags
- **WHEN** a bag has no `beanBaseId`
- **THEN** the card SHALL display roaster name, coffee name, roast age, and a "Find in Bean Base" nudge link — but SHALL NOT display empty attribute fields

#### Scenario: Empty inventory
- **WHEN** no bags are in inventory
- **THEN** the window SHALL display a prompt to add the first bag, with an "Add New Bag" action

### Requirement: Bag cards adapt to data confidence
Bag cards SHALL follow the "show less when more is known" principle: canonical-linked bags show a single dense attribute line; non-canonical bags show only what is available.

#### Scenario: Full canonical card
- **WHEN** a bag has `beanBaseId` and `beanBaseData` with origin, variety, and process
- **THEN** the card SHALL render all three on one line (e.g., "Ethiopia · Washed · Heirloom")

#### Scenario: Partial data card
- **WHEN** a bag has only roaster and coffee name (no canonical)
- **THEN** the card SHALL show only those two fields — no empty placeholders

### Requirement: "Bag finished" removes bag from inventory
The system SHALL provide a "Bag finished" action on each bag card. Activating it SHALL set `inInventory = false`, removing the bag from the inventory view. Historical shots referencing the bag SHALL be unaffected.

#### Scenario: Bag finished
- **WHEN** the user activates "Bag finished" on a bag
- **THEN** the bag SHALL disappear from the inventory view immediately
- **AND** shots that were made with that bag SHALL retain their bean snapshot

#### Scenario: Active bag marked empty
- **WHEN** the user finishes the currently active bag
- **THEN** `activeBagId` SHALL be cleared
- **AND** the bean summary in shot contexts SHALL show "No beans selected"

### Requirement: Add New Bag entry point
The Beans window SHALL provide an "Add New Bag" action that opens the Change Beans dialog in creation mode.

#### Scenario: Add new bag from inventory
- **WHEN** the user taps "Add New Bag"
- **THEN** the Change Beans dialog SHALL open
- **AND** on bag creation, the new bag SHALL appear in inventory and become the active bag

### Requirement: Idle-page bean widget shows inventory bags
The idle-page bean layout widget (`BeansItem.qml`) SHALL display the 5 most recently used inventory bags (`inInventory = true`, MRU order) as selectable pills, replacing the showOnIdle-filtered preset pills. Tapping a pill SHALL set `activeBagId`. The full inventory remains on the Beans page.

#### Scenario: Selecting a bag from the idle page
- **WHEN** the user taps a bag pill on the idle page
- **THEN** `activeBagId` SHALL be set to that bag
- **AND** the pill SHALL show a selected state

#### Scenario: Bag marked empty disappears from idle widget
- **WHEN** a bag is marked empty
- **THEN** its pill SHALL be removed from the idle widget (visibility criterion is `inInventory`, the dropped `showOnIdle` flag has no successor)

### Requirement: Bags are editable in place
Each bag SHALL be editable from its card via an Edit action, opening the Bag Details form in edit mode. All bag fields are editable (roaster, coffee name, roast date, roast level, notes, startWeightG, freeze dates, canonical link). Edits modify the existing bag row — no new bag is created, and `activeBagId` is unaffected.

#### Scenario: Fixing a typo on a bag
- **WHEN** the user edits a bag's coffee name and saves
- **THEN** the same bag row SHALL be updated in place
- **AND** shots already linked to the bag keep their snapshots unchanged (snapshots are immutable history)

#### Scenario: Adding a roast date later
- **WHEN** the user edits a bag that was created without a roast date and enters one
- **THEN** the bag SHALL store it and roast-age displays SHALL appear from then on

### Requirement: Re-buying the same coffee flows through Change Beans
There SHALL be no per-card save-as shortcut (removed — it added clutter without saving meaningful effort). Buying the same coffee again: finish the old bag, then the coffee surfaces in the Change Beans search as a history/canonical result whose selection pre-fills the creation form (roast date blank).

#### Scenario: New bag of a previously finished coffee
- **WHEN** the user finishes a bag and later searches for the same coffee in Change Beans
- **THEN** the coffee SHALL appear as a history (and/or canonical) result
- **AND** picking it SHALL pre-fill the creation form with roast date blank

### Requirement: One removal action per card, following the bag's life
Each bag card SHALL show exactly ONE removal action, chosen by whether shots reference the bag (`shotCount` from the inventory query): a delete (trash) action while no shot references it (a mistaken creation — removes the row entirely), and "Bag finished" once shots exist (leaves inventory, history kept). Storage SHALL still refuse deleting a referenced bag as a stale-count safety net.

#### Scenario: Deleting a mistakenly created bag
- **WHEN** a bag has zero linked shots
- **THEN** the card SHALL show the delete action (and not "Bag finished")
- **AND** activating it SHALL remove the bag row from the database entirely

#### Scenario: First shot converts the action
- **WHEN** the first shot is saved against a bag
- **THEN** the card SHALL show "Bag finished" instead of delete (inventory refreshes via bagsChanged)

