## ADDED Requirements

### Requirement: A recipe whose bag is finished offers Restock

A recipe card (active or archived) whose linked bag exists but is finished SHALL show a Restock action. It SHALL open the new-bag form prefilled from that bag, with its dates and notes blank, and the bag saved from it SHALL become the recipe's bag. The finished bag SHALL stay finished. The web Recipes page SHALL offer the same action, completed in the `/beans` editor.

#### Scenario: Restock from the recipe
- **GIVEN** a recipe linked to a finished bag of Saka Gran Bar
- **WHEN** the user taps Restock on the recipe card, enters the new roast date and saves
- **THEN** a new Saka Gran Bar bag is in inventory and the recipe is linked to it
