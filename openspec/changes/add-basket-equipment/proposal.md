## Why

Equipment packages (`add-equipment-packages`) shipped the grinder as a package's
first component and were explicitly built to take more component kinds with **no
schema migration** — the `EquipmentItem.kind` axis and the "future kinds (basket,
tamper, …)" note in `equipmentstorage.h`. A recent PR (#1344) landed a curated,
AI-first basket database (`src/core/basketaliases.h`: 31 entries, `WallProfile`,
relative `FlowRate`, dose ranges, a `summary()` prose helper) — but it has **zero
consumers**. It is a foundation waiting to be wired.

This change wires baskets in as the package's **second component kind**. The goal
is narrow and concrete: let a user record *which basket they used* so a shot can be
reproduced, and feed that basket's identity + physical characteristics to the AI
advisor and MCP so dialing advice can reason across baskets. The single most
valuable AI signal — the **relative flow class** — cannot be read off one shot
(flow on a fixed basket is constant) but lets the advisor steer a grind change when
a user switches baskets or recreates a shot on different gear.

The basket is part of the package's **identity**: two baskets = two packages. This
is the user's explicit decision and the clean one — it makes `shots.equipment_id`
capture the grinder *and* basket for free (no new shot column), and makes the
package's "last dial" memory `(grinder + basket)`-scoped, which is *more* correct
(a restrictive basket wants a finer grind).

This supersedes the dose-centric framing of the stale `add-basket-settings`
proposal: dose ownership stays with the bean/recipe per the existing architecture;
this change is about basket *identity as equipment*, not dose.

## What Changes

- **Basket as an optional second `equipment_items` row** (`kind="basket"`, `brand`,
  `model`, near-empty `attrs`). A package owns at most one basket item; a package
  may have **no** basket (backward-compatible — every existing package is
  grinder-only, and many users won't set one). No DB schema migration: the
  `equipment_items` table already supports new kinds.
- **Basket specs are derived, not stored.** Wall profile, relative flow, precision,
  dose range, material all resolve from `BasketAliases::findEntry(brand, model)` at
  read time — exactly how the grinder item derives `rpmCapable`. A *custom*
  (off-registry) basket is a name with unknown specs. The basket `attrs` blob has
  no free-text sub-field analogous to the grinder's `burrs`.
- **Package identity widens from `(grinder)` to `(grinder + basket)`.** The
  copy-on-write dedup/fork engine (`findPackageBy…IdentityStatic`,
  `supersedeOrEdit…Static`) compares the basket identity too, so switching a basket
  on a used package forks a new package, and switching back to a prior combo dedups
  to the existing one. "No basket" is a distinct identity value.
- **Switch Equipment dialog gains a basket section** — a **vendor-first, two-level**
  (brand → model) picker mirroring the grinder flow, backed by new
  `SettingsDye.knownBasketBrands()` / `knownBasketModels(brand)` registry bridges.
  Basket selection is **optional** (a "no basket" / clear choice exists).
- **New suggestion-row primitive: a differentiator subtitle.** Basket models within
  a brand are similar (S-Works' Convex/Tapered/Stamped billets; Decent's *two*
  "14g" Waisted siblings whose doses overlap), so the model row MUST render a
  second line carrying the axis that actually separates siblings (from
  `summary()`), never a bare name and never a mere dose echo. `SuggestionField` is
  extended to optionally accept `[{value, description}]` and render the subtitle.
- **Equipment window surfaces the basket** — `EquipmentCard` shows a basket line
  (below the grinder/burrs), and `EquipmentInfoDialog` adds a basket `InfoRow`.
- **Dialing context gains a basket sub-object** — `currentBean` (and the shot
  snapshot it resolves through `equipment_id`) gains
  `basket: { brand, model, wallProfile, relativeFlow, precision, doseRangeG }`,
  resolved through the package's basket item. The relative flow class leads; the
  dose range enables a "dosing outside this basket's rated range" sanity signal.
- **MCP equipment surfaces expose the basket** — the `equipment_*` tools read and
  write the basket identity alongside the grinder, and dialing tools include the
  basket sub-object. Field naming follows the MCP unit/string conventions
  (`doseRangeG`, human-readable `wallProfile` / `relativeFlow` strings, never codes).

## Capabilities

### Modified Capabilities

- `equipment-package-model`: basket as an optional second `equipment_items` kind;
  identity widened to `(grinder + basket)` for dedup/copy-on-write; specs
  derive-at-read from `BasketAliases`; `SettingsDye` basket resolution + bridge
  methods.
- `switch-equipment-dialog`: optional vendor-first two-level basket picker; the
  differentiator-subtitle suggestion row; optional family grouping within a brand.
- `equipment-inventory-view`: `EquipmentCard` + `EquipmentInfoDialog` render the
  package's basket.
- `dialing-context-payload`: `currentBean`/shot resolution gains a basket
  sub-object (identity + derived specs); dose-range sanity exposure.
- `mcp-server`: `equipment_*` tools read/write the basket; dialing tools surface
  the basket sub-object, following MCP data conventions.

## Impact

**Dependency:** builds on `add-equipment-packages` (the `equipment_packages` /
`equipment_items` tables, `EquipmentStorage`, `SettingsDye` equipment resolution,
`SwitchEquipmentDialog`, `EquipmentCard`/`EquipmentInfoDialog`). No new tables and
no schema migration — only new `equipment_items` rows of a new kind.

**C++ / core:**
- `src/core/basketaliases.h`: gains the consumers it was built for (no data edit
  required; an optional `summary()`-style differentiator helper may be added).
- `src/core/settings_dye.{h,cpp}`: `knownBasketBrands()` / `knownBasketModels(brand)`
  Q_INVOKABLE bridges; `dyeBasketBrand/Model` (+ derived display) resolved through
  the active package's basket item; switch/create/update carry the basket identity.
- `src/history/equipmentstorage.{h,cpp}`: create/update write an optional
  `kind="basket"` item; `findPackageBy…IdentityStatic` / `supersedeOrEdit…Static`
  widen the dedup/fork key to include the basket; `EquipmentPackageView` /
  `toVariantMap()` resolve and flatten the basket item; device-import copies basket
  items.
- `src/history/shotprojection.{h,cpp}` + history queries: resolve basket identity +
  derived specs via the `equipment_id` JOIN (no new shot column — the snapshot is
  already the package).

**QML:**
- `qml/components/SwitchEquipmentDialog.qml`: optional basket brand/model section.
- `qml/components/SuggestionField.qml`: optional per-suggestion description subtitle.
- `qml/components/EquipmentCard.qml` + `qml/components/EquipmentInfoDialog.qml`:
  basket line / `InfoRow`.
- New translation keys (`equipment.dialog.basketBrand`, `…basketModel`,
  `equipment.card.basket`, `equipment.info.basket`), accessibility attributes.

**AI / MCP:**
- `src/ai/dialing_blocks.{h,cpp}`: `currentBean` block gains the basket sub-object;
  optional dose-range sanity field.
- `src/mcp/mcptools_dialing.cpp` + the `equipment_*` write tools: surface and accept
  the basket identity following MCP unit/string conventions.

## Non-Goals

- **Dose ownership.** Dose stays bean/recipe-scoped; the basket's dose *range* is a
  read-only advisory signal only.
- **Visualizer upload.** Visualizer has no basket field; upload shape is unchanged.
- **Pressurized / dual-wall baskets.** Deliberately excluded from the registry (they
  defeat DE1 flow/pressure profiling).
- **Tamper / portafilter / puck screen.** Future kinds; out of scope here.
