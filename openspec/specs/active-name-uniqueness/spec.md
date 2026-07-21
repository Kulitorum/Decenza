# active-name-uniqueness Specification

## Purpose

An equipment package or a recipe cannot be saved under a name that another ACTIVE item of the same kind already holds — two same-named items are indistinguishable in the quick-pick pills, switcher lists, and readout bars, so a collision is stopped at the moment the name is entered rather than papered over afterwards by decorating labels. "Active" is scoped per entity (equipment: in inventory; recipe: non-archived), so removing or archiving an item frees its name. The rule is enforced both in the in-app forms and at the storage layer, so the app, MCP, and ShotServer all behave identically for creates and renames. It applies going forward only and never rewrites existing data.

## Requirements

### Requirement: Active equipment package names are unique

The system SHALL prevent an equipment package from being saved with a name that
matches another **active** equipment package, where "active" means the package is in
inventory (`in_inventory = 1`). The name comparison SHALL be trimmed and
case-insensitive, and SHALL exclude the package currently being edited.

#### Scenario: Creating a package whose name matches an active package
- **WHEN** the user enters a package name equal (ignoring surrounding whitespace and
  case) to the name of another in-inventory package
- **THEN** the create form disables its Save control and shows an inline
  "name already in use" message near the name field, and no package is created

#### Scenario: Reusing the name of a removed (out-of-inventory) package
- **WHEN** the user enters a package name that matches only packages that are no
  longer in inventory
- **THEN** the name is accepted and the package can be saved

#### Scenario: Renaming a package to a casing/whitespace variant of its own name
- **WHEN** the user edits a package and changes its name only in surrounding
  whitespace or letter case
- **THEN** the save is allowed (the package is excluded from its own collision check)

### Requirement: Active recipe names are unique

The system SHALL prevent a recipe from being saved (created, renamed, cloned, or
created from a shot) with a name that matches another **non-archived** recipe. The
comparison SHALL be trimmed and case-insensitive, and SHALL exclude the recipe
currently being edited.

#### Scenario: Naming a recipe the same as an active recipe
- **WHEN** the user enters a recipe name equal (ignoring whitespace and case) to
  another non-archived recipe's name
- **THEN** the form disables Save and shows an inline "name already in use" message,
  and no recipe is created or renamed

#### Scenario: Reusing the name of an archived recipe
- **WHEN** the entered recipe name matches only archived recipes
- **THEN** the name is accepted

### Requirement: Enforcement covers non-form creation paths

The system SHALL enforce the active-name-uniqueness rule at the storage layer for each
entity, so that programmatic creation and rename paths (MCP tools and ShotServer REST
endpoints) cannot introduce a duplicate active name. On a collision the storage
operation SHALL fail and report the failure to its caller; it SHALL NOT auto-rename
the item and SHALL NOT silently succeed.

#### Scenario: MCP or REST create with a duplicate active name
- **WHEN** a non-form caller requests creation of an equipment package or recipe with
  a name that duplicates an active item of the same kind
- **THEN** the storage operation is rejected and the caller receives an explicit
  failure identifying the name conflict, with no item created

### Requirement: The app, MCP, and ShotServer behave identically

The in-app forms, the MCP tools, and the ShotServer REST endpoints SHALL apply the
same active-name-uniqueness rule with the same scope and the same normalization, and
SHALL each report a name collision as an explicit, identifiable name conflict rather
than a generic failure. This SHALL hold for renames as well as creations.

#### Scenario: The same collision is reported the same way on every surface
- **WHEN** the same duplicate name is submitted through the in-app form, an MCP tool,
  and the ShotServer REST endpoint — whether creating or renaming
- **THEN** all three reject the write, none creates or renames anything, and each
  reports a conflict that identifies the name as the cause

### Requirement: Only a name change can collide

The system SHALL evaluate the uniqueness rule against what an operation actually
CHANGES, not against which fields it submits. Saving an item under the name it
already holds SHALL always be permitted, whatever other items exist. Restoring an
archived item to active SHALL be treated as introducing its name.

This is load-bearing because every save path submits the name on every save, and
because an item whose name was auto-derived rather than typed (an equipment package
created with a blank name derives "{brand} {model}") can legitimately already share
a name with another. Testing the submitted name alone would refuse those saves and
leave the affected items permanently uneditable.

#### Scenario: Re-saving an item under its own name
- **WHEN** the user edits any other field of an item and saves, leaving the name as it
  was — including when another active item already shares that name
- **THEN** the save succeeds and the other field's change is applied

#### Scenario: Editing one of two items that already share a name
- **WHEN** two active items of the same kind already share a name and the user opens
  either one and changes something other than the name
- **THEN** the save control stays enabled and the save succeeds

### Requirement: Existing data is not rewritten

The rule SHALL apply only to create and edit operations going forward. The system
SHALL NOT migrate, rename, or otherwise rewrite any pre-existing items, including any
duplicate active names that already exist before this change. Import and restore
paths, which reproduce data the user already had rather than accept a newly entered
name, SHALL NOT be subject to the rule.

#### Scenario: Pre-existing duplicates remain untouched
- **WHEN** two active items of the same kind already share a name at the time this
  change ships
- **THEN** both items are left exactly as they are, and neither is renamed or removed
