## Why

Equipment packages and recipes can each be given a name that already belongs to
another active item. Two items with the same name are indistinguishable in the
quick-pick pills, the switcher lists, and the readout bars — the user cannot tell
which is which, and selecting one is a guess. (This surfaced as two identical
"Niche Zero" equipment pills whose packages differed only by puck prep.) The app
should stop a collision at the moment the name is entered, rather than papering over
it later by auto-decorating labels.

## What Changes

- When a user names (or renames) an **equipment package** or a **recipe**, the
  create/edit form blocks saving while the entered name matches another **active**
  item of the same kind: the Save/confirm control is disabled and an inline
  "name already in use" message appears near the name field.
- The comparison is **trimmed and case-insensitive**, and **excludes the item being
  edited** (renaming an item to a casing/whitespace variant of its own name is allowed).
- "Active" is scoped per entity:
  - **Equipment** — packages in inventory (`in_inventory = 1`).
  - **Recipe** — non-archived recipes.
- The same rule is enforced at the **C++ storage layer** for each entity, so the
  non-form creation/rename paths (MCP tools, ShotServer REST) cannot create
  duplicates either. Storage rejects a colliding create/rename and reports the failure
  to its caller; it does **not** auto-rename or silently succeed.
- The **app, MCP, and ShotServer behave identically** — same scope, same
  normalization, and each reports a collision as an identifiable name conflict rather
  than a generic failure, for renames as well as creations.
- No migration and no rewriting of existing data. The rule applies only to
  create/edit going forward; any pre-existing duplicates are left as-is.

**Out of scope:** bean bags. A bag's identity is roaster + coffee rather than a
user-typed name, and the existing behaviour is correct as-is — bags are unchanged.

## Capabilities

### New Capabilities
- `active-name-uniqueness`: The rule that an equipment package, recipe, or bean bag
  may not be saved with a name that duplicates another active item of the same kind —
  the per-entity definition of "active", the trimmed/case-insensitive/self-excluding
  comparison, the form-level block (disabled Save + inline message), and the
  storage-level enforcement that covers non-form (MCP/REST/import) paths.

### Modified Capabilities
<!-- None: the three model specs (equipment-package-model, recipe-model, coffee-bag-model)
     keep their existing requirements; this change adds a new cross-cutting constraint
     capability rather than altering their behavior. -->

## Impact

- **QML forms** (client-side pre-check + inline message + disabled Save):
  - Equipment: `qml/components/SwitchEquipmentDialog.qml` (mirrors the existing
    `duplicateOfId` full-identity guard with a name-only guard).
  - Recipe: `qml/pages/RecipeWizardPage.qml`.
- **C++ storage guards** (reject colliding create/rename, active-scoped):
  - `src/history/equipmentstorage.cpp` (`requestCreatePackage` / `requestUpdatePackage`).
  - `src/history/recipestorage.cpp` (`requestCreateRecipe` / `requestCloneRecipe` /
    `requestUpdateRecipe`).
- **Non-form callers** that must surface the new failure: MCP recipe tools
  (`recipe_create`, `recipe_clone`, `recipe_create_from_shot`, `recipe_update`) and the
  ShotServer REST equipment + recipe endpoints.
- **i18n**: new translation keys for the inline "name already in use" messages.
- **Wiki manual**: note the uniqueness rule on the Equipment and Recipes pages.
- Import paths are deliberately left unguarded — they restore existing user data
  rather than accept a newly typed name.
- No new user settings; no schema migration.
