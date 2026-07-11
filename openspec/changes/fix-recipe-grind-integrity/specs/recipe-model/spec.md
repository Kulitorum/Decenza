## REMOVED Requirements

### Requirement: Grind inherit-or-pin
**Reason**: Replaced by "Recipe-owned grind" (see ADDED Requirements) — the two-live-write-path model (bag write-through when inheriting vs. recipe-pin write-through when pinned) was the root cause of two production bugs (a post-shot bag write spuriously reverting a pinned recipe's grind; a recipe re-activation race losing an in-flight grind edit), because it required coordinating two async writers via a suspend flag and echo counters that didn't cover every writer. The repo owner directed collapsing to a single model: a recipe's grind always lives on the recipe.
**Migration**: Existing recipes with an empty `grindPinned` (inherit mode) are backfilled once, at upgrade time, by copying their linked bag's current `grinderSetting`/`rpm` into `grindPinned`/`rpmPinned` (migration 30 in `ShotHistoryStorage`). No user action required. Recipes that were already pinned are unaffected.

## ADDED Requirements

### Requirement: Recipe-owned grind
Grind SHALL always live on the recipe: every recipe of a grind-bearing drink type (whether or not it has a linked bag) stores its own `grindPinned` (free-form text, stored opaquely) and optional `rpmPinned`; grind-less drink types (tea, hot-water tea) store none, exactly as today. There is no bag-inherit mode: a recipe's own grind is never read *from* the bag at activation time. Editing grind while a recipe is active writes immediately to that recipe's own fields, and — independently, per `coffee-bag-model`'s "Bean/grinder edits write through to the active bag" — the same edit also writes immediately to the linked bag, since the bag always mirrors the most recently dialed grind regardless of what's driving it. A recipe with no linked bag stores grind/rpm locally exactly as before (unaffected by this change).

#### Scenario: Editing grind on an active recipe updates the recipe and the bag together
- **WHEN** a recipe is active and the user edits its grind or rpm
- **THEN** that recipe's own `grindPinned`/`rpmPinned` change immediately
- **AND** the linked bag's stored grind/rpm are also updated immediately to the same value (coffee-bag-model)

#### Scenario: Sibling recipes on the same bag are independent
- **WHEN** two recipes are linked to the same bag and one recipe's grind is edited
- **THEN** the other recipe's own `grindPinned`/`rpmPinned` is unchanged, even though the shared bag's stored grind just changed — no recipe ever reads its grind from the bag

#### Scenario: Bag re-dial does not affect any recipe's grind
- **WHEN** the linked bag's stored grind changes (via a live edit while no recipe governs it, a different recipe's edit, or a manual bag edit)
- **THEN** no *other* recipe's own `grindPinned`/`rpmPinned` changes as a result

### Requirement: New-recipe grind defaults from the bag, once
When a recipe is created with a linked bag, the bag's current `grinderSetting`/`rpm` SHALL be read exactly once, at creation, to supply the recipe's grind default. On the wizard this is an **editable default offered in the field** — not silently copied and not a live link; the user SHALL be free to accept it as-is or change it before saving, and whatever is on the field at save time becomes the recipe's own stored value, permanently independent of the bag from that point on. On non-interactive create surfaces (MCP, web), the same rule applies at save time in storage: a create that **omits** grind while linking a bag SHALL adopt the bag's current grind/rpm as the recipe's own value (mirroring the existing bag-link save-time normalization), while a create that supplies an **explicitly empty** grind SHALL store it empty — omission means "use the sensible default", explicit empty means "no grind". Promote-from-shot (wizard and MCP `recipe_create_from_shot`) SHALL default grind/rpm from the **shot's own recorded values** — the exact dial that produced the shot being promoted — not from the bag's current dial. The rpm default SHALL only be offered when the recipe's selected equipment reports grinder rpm capability. This default SHALL NOT re-occur on subsequent views of an already-created recipe; the user's own edits (or lack thereof) are authoritative from that point on.

#### Scenario: New recipe defaults to the bag's current dial
- **WHEN** the user creates a recipe and links a bag whose current grind is "18" with rpm 1200
- **THEN** the recipe's grind field shows "18" and rpm field shows 1200 as editable defaults (if the chosen equipment is rpm-capable)
- **AND** the recipe is saved with whatever is on the field at that point, independent of later bag changes

#### Scenario: User overrides the offered default before saving
- **WHEN** the user creates a recipe, the grind field defaults to the bag's "18", and the user changes it to "20" before saving
- **THEN** the recipe saves with "20" as its own grind — the bag's value was only ever an offered starting point, not something silently written into the recipe

#### Scenario: Rpm does not default for a non-rpm grinder
- **WHEN** the user creates a recipe with equipment whose grinder does not report rpm
- **THEN** the rpm field has no default and is not shown

#### Scenario: Editing an existing recipe does not re-offer the default
- **WHEN** the user reopens an existing recipe's details for editing
- **THEN** the grind/rpm fields show the recipe's own stored values, not a fresh read of the bag's current dial

#### Scenario: MCP create omitting grind adopts the bag's dial
- **WHEN** an MCP or web client creates a recipe linking a bag whose current grind is "18", without providing a grind value
- **THEN** the recipe saves with "18" as its own grind

#### Scenario: Explicitly empty grind is respected
- **WHEN** a create supplies an explicitly empty grind value alongside a linked bag
- **THEN** the recipe saves with no grind — the empty value is not overridden by the bag default

#### Scenario: Promote-from-shot defaults from the shot, not the bag
- **WHEN** the user promotes a shot that was pulled at grind "17", and the linked bag's dial has since moved to "18"
- **THEN** the new recipe's grind defaults to "17" (the shot's recorded value), editable before saving
