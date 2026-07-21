## Context

Three entities carry a user-editable name that is used as a picker label: equipment
packages, recipes, and bean bags. Nothing today stops two active items of the same
kind from sharing a name, and when they do the quick-pick pills / switcher lists /
readout bars render identical rows the user cannot tell apart.

Each entity already has a create/edit form and a SQLite-backed storage class with a
soft-delete / archive notion:

- **Equipment** — `SwitchEquipmentDialog.qml` form; `EquipmentStorage`
  (`src/history/equipmentstorage.cpp`). "Active" = `in_inventory = 1`; the switcher's
  `packages` list is already filtered to that. There is a precedent guard here:
  `duplicateOfId` (full grinder+basket+puck identity, trimmed/case-insensitive,
  self-excluding) disables Save and shows an inline warning, backed by a storage
  check in `requestCreatePackage`. The name guard mirrors this exactly.
- **Recipe** — recipe create/edit/rename/clone/from-shot surfaces; recipe storage.
  "Active" = non-archived. Also reachable via MCP (`recipe_create/update/clone/
  create_from_shot`) and ShotServer REST.
- **Bean bag** — bag create/edit form; bag storage. "Active" = in inventory. Also
  reachable via MCP (`bag_create/update`) and ShotServer REST. Bags may sync to
  Visualizer.

## Goals / Non-Goals

**Goals:**
- Block (not warn) saving an item whose name collides with another active item of the
  same kind, with immediate inline feedback in the form.
- Enforce the same invariant at the storage layer so MCP/REST/import cannot bypass it.
- One consistent comparison rule everywhere: trimmed, case-insensitive, self-excluded,
  active-scoped per entity.

**Non-Goals:**
- No auto-rename, no label decoration/disambiguation (the reverted approach).
- No migration and no rewrite of existing rows; pre-existing duplicates stay as-is.
- No cross-entity uniqueness (a recipe and a bag may share a name).
- No new user setting or toggle — the rule is always on.

## Decisions

### Block at save time, mirroring the equipment `duplicateOfId` pattern
The equipment form already proves the desired UX: a computed `*OfId` property, wired
into `canSave`, with an inline `Theme.warningColor` message. Each form adds a parallel
`nameDuplicateOfId` (or equivalent) computed over the active list it already holds,
comparing normalized names and excluding the edited item. Chosen over a warn-and-allow
toast because the whole point is to prevent the duplicate, not annotate it.

### Two layers, one rule
Client-side check gives instant feedback; the storage-layer check is the actual
invariant so non-form callers are covered (the user chose "forms + storage"). The
storage check **rejects** a colliding create/rename (returns a failure the caller
surfaces) rather than the equipment dedup's "return the existing id" semantics — for
names we want an explicit conflict, not a silent merge.

### Normalization defined once
Comparison key = `name.trimmed().toLower()` (Qt `QString::trimmed().toLower()`;
JS `String(name).trim().toLowerCase()`). Applied identically on both layers so the
form pre-check and the storage guard never disagree. Empty names are not subject to
the guard (equipment allows a blank name and derives "{brand} {model}"; the check
returns "no collision" for an empty entered name).

### "Active" is per-entity, read from the list the form already uses
- Equipment: the `packages` inventory list (already `in_inventory = 1`).
- Recipe: the non-archived recipe list.
- Bean bag: the in-inventory bag list.
No new queries are needed on the client; the storage guard uses the same
active-scoped predicate the entity's list query already uses.

## Risks / Trade-offs

- **[Pre-existing duplicates already in the DB]** → The guard is create/edit-time
  only; it never rewrites existing rows. A user who already has two "Niche Zero"
  packages keeps both; editing one to a *different* colliding name is still blocked,
  editing it in other ways is unaffected.
- **[Storage rejection must be surfaced, not swallowed]** → Every non-form caller
  (MCP tools, REST handlers, import) must propagate the new failure with a clear
  "name already in use" message; a silently dropped rejection would look like a no-op
  save. Callers are enumerated in tasks.md and each is updated.
- **[Bag identity is roaster+coffee, not a single field]** → The bag guard must
  compare the same user-facing name the UI labels bags by; tasks.md pins the exact
  field(s) from the bag scoping before coding so the check matches what the user reads.
- **[Visualizer-synced bags]** → Guard is local create/edit-time only and does not
  touch synced/canonical bag data, consistent with "no retro-rewrite of user data".

## Migration Plan

None. No schema change, no data migration. The change is additive validation; rollback
is reverting the branch.

## Open Questions

- Exact recipe and bag form/storage anchors and the precise "active" field names are
  being confirmed by the recipe and bean scoping passes; tasks.md is finalized once
  those land. Equipment anchors are already confirmed.
