## MODIFIED Requirements

### Requirement: Add New Bag entry point
The Beans window SHALL provide two creation entry points: a "Bag of Coffee" action (primary treatment) that opens the Change Beans dialog in its existing creation mode, and a "Bag of Tea" action (secondary treatment beside it) that opens the Change Beans dialog in tea creation mode. Each entry point stamps the created bag's kind. The labels name what is added — a bag — so "coffee" is not read as a bean.

#### Scenario: Add coffee from inventory
- **WHEN** the user taps "Bag of Coffee"
- **THEN** the Change Beans dialog SHALL open in its search-first coffee flow
- **AND** on bag creation, the new bag SHALL appear in inventory and become the active bag

#### Scenario: Add tea from inventory
- **WHEN** the user taps "Bag of Tea"
- **THEN** the Change Beans dialog SHALL open in tea mode
- **AND** the created bag SHALL have kind "tea" and appear in inventory
