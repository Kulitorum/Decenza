## Why

Equipment packages (`add-equipment-packages`) and baskets (`add-basket-equipment`,
merged in #1345) established the package as a switchable bundle of typed component
kinds (grinder, basket) with copy-on-write identity. **Puck prep** is the next
user-requested kind.

It does not fit the grinder/basket mold — it is not a branded object with a
vendor → model identity. It is the user's **distribution / prep routine** (WDT,
shaker, …). It earns a place because it is the one input the channeling detector
*cannot get from telemetry*: the detector ([docs/SHOT_REVIEW.md](docs/SHOT_REVIEW.md))
reports *that* a shot channeled; puck prep tells the advisor whether to blame
**prep** or **grind/dose**. That is the same role `relativeFlow` played for baskets
— the signal the AI can't infer and must be told.

Most people run one or two prep routines and switch them by switching packages, so
puck prep is modeled at the **package-identity level** like basket: a distinct prep
is a distinct package, reached by the same fork/dedup machinery.

## What Changes

- **Puck prep as an optional `kind="puckprep"` `equipment_items` row.** Its `attrs`
  is a small **checklist config**: `{ wdt, shaker, puckScreen, paperFilter, rdt }`
  (all booleans). No brand/model, no registry, **no schema migration** — the
  `equipment_items` kind axis already supports it.
- **Package identity widens** `(grinder + basket)` → `(grinder + basket + puckprep)`.
  The copy-on-write dedup/fork engine compares the puck-prep config too; "no puck
  prep" is a distinct identity value. Because this identity is a **flag-set** rather
  than a brand/model string, the comparison normalizes the config to a canonical
  form (the one true delta from the basket pattern).
- **Derived `distribution` rollup** (`none` | `light` | `thorough`) computed at
  read time as a pure function of the flags (WDT or shaker → thorough, else RDT → light, else
  none). No registry — mirrors how basket specs are derived, not stored.
- **Edit Equipment form gains a puck-prep CHECKBOX section** — a new UI primitive in
  the form (not a `SuggestionField` vendor picker). All-unchecked = no puck-prep
  item (optional, like basket).
- **`currentBean` gains a `puckPrep` sub-object** (the flags + the `distribution`
  rollup) for the advisor. The rollup flips channeling advice between "fix your
  prep" and "prep is solid → look at grind/dose/basket."
- **`equipment_*` MCP tools** read and write the puck-prep flags; the equipment
  **card** and **info dialog** show the prep.

## Capabilities

### Modified Capabilities

- `equipment-package-model`: optional `kind="puckprep"` item; identity widened to
  `(grinder + basket + puckprep)` compared as a normalized flag-set; `distribution`
  rollup derived at read; `SettingsDye` puck-prep bridge.
- `switch-equipment-dialog`: optional puck-prep **checkbox** section; all-unchecked
  = no puck prep; edits honor package-identity (fork/dedup) semantics.
- `equipment-inventory-view`: `EquipmentCard` + `EquipmentInfoDialog` render the
  package's puck prep.
- `dialing-context-payload`: `currentBean` gains a `puckPrep` sub-object (flags +
  derived `distribution`), omitted when the package has no puck prep.
- `mcp-server`: `equipment_*` tools read/write the puck-prep flags following MCP
  conventions.

## Impact

**Dependency:** builds on `add-equipment-packages` + `add-basket-equipment` (the
`equipment_packages` / `equipment_items` tables, `EquipmentStorage` identity engine,
`SettingsDye` resolution, `SwitchEquipmentDialog`, `EquipmentCard`/`EquipmentInfoDialog`,
the `currentBean` builder, the `equipment_*` MCP tools). No new tables, no schema
migration — a new `equipment_items` row of a new kind.

**C++ / core:**
- `src/history/equipmentstorage.{h,cpp}`: write/read an optional `kind="puckprep"`
  item (flags in `attrs`); widen the dedup/fork identity to include the normalized
  puck-prep config; resolve + flatten it (with the derived `distribution`) in
  `EquipmentPackageView::toVariantMap`; carry puckprep items through device import.
- `src/core/settings_dye.{h,cpp}`: `dyePuckPrep*` display resolution; thread the
  flags through switch/create/update.
- `src/ai/dialing_blocks.h`: `currentBean` gains the `puckPrep` sub-object + the
  `distribution` derivation.
- `src/history/shothistorystorage*.{cpp,h}` + `shotprojection.h`: resolve the
  puck-prep flags via the `equipment_id` JOIN (new `LEFT JOIN equipment_items`
  for `kind='puckprep'`; append columns after the basket join — mind the positional
  reads, the spot the basket PR's regression test guards).
- `src/mcp/mcptools_write.cpp` + `mcptools_dialing.cpp`: surface/accept the flags.

**QML:**
- `qml/components/SwitchEquipmentDialog.qml`: a puck-prep checkbox section (a small
  reusable labeled-checkbox row), in the two-column form layout.
- `qml/components/EquipmentCard.qml` + `EquipmentInfoDialog.qml`: a puck-prep line/row.
- New translation keys; accessibility (`Accessible.role: CheckBox`, names/state).

## Non-Goals / Future Follow-ons

- **Tamper** (base shape: flat / convex / ripple — a functional axis the advisor can
  use for edge-vs-center reasoning). Deferred per the user; additive later as one
  enum field on the puck-prep item.
- **Sift** (a 6th flag, distinct from shaker). Additive later as one more boolean.
- **Per-shot puck-prep capture.** Puck prep is a package-level *routine*, not a
  per-shot field. (More accurate but more friction; out of scope.)
- **Tamp pressure, nutation, dosing funnel, tool brands.** AI-noise — excluded so
  users aren't trained to record data the advisor can't act on.
- **Explicit "I do no prep" vs "not recorded."** v1 collapses all-unchecked to "no
  puck-prep item" (omitted from AI context). The distinction is a possible later
  refinement (see design.md).
