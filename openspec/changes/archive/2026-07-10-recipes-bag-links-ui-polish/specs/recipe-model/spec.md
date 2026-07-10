# recipe-model Delta

## ADDED Requirements

### Requirement: Recipes link a specific bag
A recipe SHALL link a specific bag via a `bag_id` column (kCols-registered, CREATE TABLE + migration step, riding transfer/backup import with id remapping like `equipment_id`). Bean identity fields (Bean Base canonical id, roaster, coffee) SHALL be retained on the recipe as a display fallback and as the matching key for automatic relinking (see `recipe-bag-lifecycle`). Activation SHALL use the linked bag directly — no most-recently-used resolution. A recipe MAY still have no bag at all (bean-less recipes per the optionality ladder).

#### Scenario: Two open bags of the same bean
- **WHEN** two recipes link two different open bags of the same bean and each is activated in turn
- **THEN** each activation selects exactly its own linked bag and inherits that bag's grind

#### Scenario: Bean-less recipe unaffected
- **WHEN** a recipe with no bag link is activated
- **THEN** the active bag is unchanged and recipe-local grind applies, exactly as before

### Requirement: Existing recipes migrate to bag links
A one-time forward migration SHALL populate `bag_id` for existing recipes by resolving each recipe's bean identity to the current open bag (canonical id first, else case-insensitive roaster+coffee, most recently used first — the previous resolver's logic, run once). Recipes whose bean has no open bag SHALL migrate with no bag link and present as stale.

#### Scenario: Migration resolves the open bag
- **WHEN** the database migrates with a recipe whose bean has one open bag
- **THEN** the recipe's `bag_id` points at that bag and behavior is unchanged from the user's perspective

#### Scenario: Migration with no open bag
- **WHEN** the database migrates with a recipe whose bean has no open bag
- **THEN** the recipe migrates without a bag link and shows the bag-finished state until relinked

## MODIFIED Requirements

### Requirement: Grind inherit-or-pin
Grind SHALL remain a bag property by default: a recipe with no pinned grind inherits its linked bag's grind (and rpm) at all times, and the wizard SHALL display the inherited values read-only. A recipe MAY override with a pinned grind value (free-form text, stored opaquely) and an optional pinned rpm — the override covers grind and rpm together and SHALL be freely toggleable off (returning to inherit). A recipe with no linked bag SHALL store its grind/rpm locally on the recipe.

#### Scenario: Re-dial updates sibling recipes
- **WHEN** the bag's grind is changed (with or without a recipe active)
- **THEN** every recipe inheriting from that bag reflects the new grind

#### Scenario: Pinned recipe is isolated
- **WHEN** a recipe has a pinned grind and the bag's grind changes
- **THEN** the pinned recipe's grind is unchanged

#### Scenario: Sibling bags are independent
- **WHEN** two recipes inherit grind from two different bags of the same bean and one bag is re-dialed
- **THEN** only the recipe linked to the re-dialed bag reflects the change

## REMOVED Requirements

### Requirement: Bean linking resolves to the current open bag
**Reason**: Bean-level resolution picks the most-recently-used open bag, which silently selects the wrong bag (and the wrong inherited grind) for users running multiple bags of the same bean at different ages — e.g. frozen vs. counter bags. Replaced by a hard per-bag link.
**Migration**: The `bag_id` migration resolves each existing recipe to its current open bag once (same resolver logic, run at migration instead of every activation). Continuity across bag turnover is provided by the new `recipe-bag-lifecycle` capability (roll-on-finish, wake-on-restock) instead of activation-time resolution.
