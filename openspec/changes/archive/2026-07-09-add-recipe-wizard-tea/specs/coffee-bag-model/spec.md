# coffee-bag-model Specification (delta)

## ADDED Requirements

### Requirement: Bags carry a kind set at creation
The `coffee_bags` table SHALL gain a `kind` TEXT column (`"coffee"` default, `"tea"`), added by migration with kCols registration. The kind SHALL be set by the creation entry point and SHALL NOT be editable afterwards (no editor toggle; a mis-created zero-shot bag is deleted and recreated). The kind SHALL ride backup restore and device-to-device transfer, and pre-migration bags SHALL default to coffee. Bag surfaces (inventory cards, unified bean search, idle pills, MCP bag tools) SHALL be able to read the kind; the recipe wizard's bean step filters by it.

#### Scenario: Existing bags stay coffee
- **WHEN** the migration runs on an existing database
- **THEN** every existing bag has kind "coffee" and no behavior changes

#### Scenario: Kind survives transfer
- **WHEN** a tea bag is imported via device transfer or backup restore
- **THEN** it arrives with kind "tea"

### Requirement: Tea bags store structured brewing data in the blob
For tea bags, the `beanBaseData` blob vocabulary SHALL include: `teaType` (black/green/oolong/white/herbal/pu-erh), `garden` (estate), `cultivar`, `flush`, `brewTempC` (number, Celsius), `leafGramsPer100Ml` (number), and `steepTime` (display string), alongside the shared descriptive keys (origin, region, tastingNotes). These are schemaless blob keys — no migration. Absent keys mean "vendor did not state it"; consumers SHALL treat them as empty, never inferring values.

#### Scenario: Brewing data seeds without guessing
- **WHEN** a tea bag has no `brewTempC`
- **THEN** the recipe wizard uses its per-tea-type default temperature and does not invent a bag value

#### Scenario: Coffee bags unaffected
- **WHEN** a coffee bag's blob is read
- **THEN** the tea keys are simply absent and no coffee surface changes
