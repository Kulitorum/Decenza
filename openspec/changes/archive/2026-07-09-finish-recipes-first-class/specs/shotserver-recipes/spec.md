## ADDED Requirements

### Requirement: Web recipe API round-trips the hot-water block
The ShotServer recipe API and `/recipes` management page SHALL accept, persist, and reflect a recipe's optional hot-water block, mirroring how the steam block is handled. Create and update handlers SHALL parse a `hotWater` body field into the recipe's hot-water block; recipe detail responses and the web form SHALL expose it; and promotion from a shot SHALL prefill it from the shot's hot-water snapshot when present.

#### Scenario: Web create with hot water
- **WHEN** a client creates or updates a recipe through the web API with a `hotWater` field
- **THEN** the hot-water block is persisted and reflected in the recipe's web representation and in the app immediately

#### Scenario: Web promotion carries hot water
- **WHEN** the user promotes a shot that recorded a hot-water snapshot from the web shot browser
- **THEN** the created recipe's hot-water block matches that snapshot
