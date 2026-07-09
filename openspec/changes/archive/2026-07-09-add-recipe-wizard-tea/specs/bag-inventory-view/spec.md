# bag-inventory-view Specification (delta)

## MODIFIED Requirements

### Requirement: Add New Bag entry point
The Beans window SHALL provide two creation entry points: an "Add Coffee" action (primary treatment) that opens the Change Beans dialog in its existing creation mode, and an "Add Tea" action (secondary treatment beside it) that opens the Change Beans dialog in tea creation mode. Each entry point stamps the created bag's kind.

#### Scenario: Add coffee from inventory
- **WHEN** the user taps "Add Coffee"
- **THEN** the Change Beans dialog SHALL open in its search-first coffee flow
- **AND** on bag creation, the new bag SHALL appear in inventory and become the active bag

#### Scenario: Add tea from inventory
- **WHEN** the user taps "Add Tea"
- **THEN** the Change Beans dialog SHALL open in tea mode
- **AND** the created bag SHALL have kind "tea" and appear in inventory
