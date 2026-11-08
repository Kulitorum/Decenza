# Proposal

## Why

A bag's last grind lacks the profile context needed to make it meaningful, and equipment is not an attribute of the beans. Recipes can capture those choices together while still allowing people to make coffee without recipes.

## What Changes

- All bags follow one rule: they do not store or display equipment, grind or RPM, and selecting one leaves the current equipment and grind alone. **BREAKING:** the upgrade removes these fields from existing bags, which no longer recall those settings on selection.
- On upgrade, remove the bag fields in one database migration. The active recipe, equipment, live grind and RPM live in settings and are left untouched; no equipment is created. A failed migration rolls back and retries next launch while the app runs normally.
- Show a one-time startup notice only to users who appear to rely on grind stored in multiple bags without recipes for those bags or defined equipment, assessed by the same migration before the fields are removed.
- Recipes retain their own grind independently of equipment. **BREAKING:** activating a recipe with no equipment no longer falls back to its bag's equipment; it keeps the currently selected equipment.
- Interactive recipe creation defaults grind from the last known matching shot-history grind for the bean when available, not from the bag. Define identity matching and coordinate history lookups so they do not overwrite that default or the user's edits. The user can edit the default; show profile and equipment context when known. Other creation surfaces never silently copy the bag's grind.
- Preserve existing shot history and the remaining bag data. Continue capturing equipment and grind on new shots and supporting manual equipment switching without recipes.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `coffee-bag-model`: Remove bag equipment and grind fields and behavior for all bags.
- `bag-detail-editing`: Remove bag equipment and grind editing.
- `bag-inventory-view`: Restock without bag-owned equipment or grind.
- `change-beans-dialog`: Create, restock and edit bags without equipment or grind.
- `brew-settings-equipment`: Resolve equipment from the active package and keep grind off bags.
- `switch-equipment-dialog`: Switch equipment and its dial without writing a bag.
- `grind-value-entry`: Remove bag grind controls and bag-based recipe defaults.
- `equipment-package-model`: Maintain equipment deletion, merging and identity edits without bag links.
- `recipe-model`: Replace bag-based recipe grind defaults with shot-history defaults.
- `recipe-wizard`: Offer the last known bean grind with context as an editable default.
- `recipe-activation`: Remove the bag equipment fallback without changing recipe-owned grind.
- `shot-review-carry-forward`: Stop carrying equipment or grind corrections into bags.
- `shotserver-bags`: Align web and API bag creation and editing with the shared bag behavior.
- `shotserver-recipes`: Align web recipe creation and activation with the recipe rules.
- `mcp-server`: Align bag and recipe tools and their documented contracts with the same rules.
- `data-transfer-coverage`: Preserve shot history, other bag data and recoverable legacy recipe grinds across backup and device transfer without restoring removed bag fields.

## Impact

Bag storage and migrations, active setup and shot-review write-through, equipment, recipe creation and activation, shot-history lookups, shared native/web UI, MCP and HTTP surfaces, tests, and the user manual are affected. Existing shot records and other bag information remain unchanged; bag equipment, grind and RPM are removed.
