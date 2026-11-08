# Spec Delta

## MODIFIED Requirements

### Requirement: Restock SHALL create a new bag from an existing one

Restock SHALL be offered on finished bags and on bags in inventory. It SHALL open the new-bag form prefilled from that bag's identity, bean details, dose and yield, with roast date, freezer dates, opened date and notes left blank. Saving SHALL create a new bag and SHALL leave the original bag's state unchanged.

#### Scenario: Restock on a bag still in use keeps it in inventory

- **WHEN** the user taps Restock on a bag in inventory and saves a new roast date
- **THEN** a new bag of that coffee SHALL be added and the original SHALL stay in inventory
