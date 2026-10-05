## ADDED Requirements

### Requirement: A bag edit is pushed without waiting for a shot upload

A bag edit that changes a Visualizer-mapped field (from the bag editor, an AI fill or MCP `bag_update`) SHALL be pushed to the linked Visualizer bag at once unless Coffee Management is known to be off for the account. It SHALL NOT wait for a shot upload to confirm Coffee Management. A bag whose push failed SHALL be retried after a shot upload and at the end of each edit-pull pass.

#### Scenario: AI fill on a device that does not upload shots
- **GIVEN** the desktop app, with no shot uploaded this session
- **WHEN** the user fills a synced bag's details with AI and saves
- **THEN** the bag's fields reach Visualizer without any shot being uploaded
