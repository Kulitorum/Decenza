## Why

Grinder identity (brand, model, burrs) currently lives **inside every coffee bag** — `SettingsDye` writes `grinderBrand/Model/Burrs/Setting` through to the active `coffee_bags` row. A user with one grinder and 20 bags has that grinder duplicated 20 times, and there is no first-class concept of "the equipment I make coffee with." This blocks an obviously-coming need: the user's setup is more than a grinder (basket, tamper, portafilter, puck screen, machine), and all of it wants one home.

This change extracts equipment into a first-class **Equipment package** — a switchable container, parallel to Beans, whose first component is the grinder. The grinder's *identity* becomes canonical and de-duplicated; the *dial-in* (grind setting + a new RPM field) stays where it belongs as a per-bean/per-shot value. Modeling the package as a container of typed components means adding a basket or tamper later is a new component kind, **not another migration**.

## What Changes

- New **`equipment_packages`** table (container: optional name, `inInventory`, `lastUsed`, **`lastGrindSetting`**, **`lastRpm`**) and **`equipment_items`** table (typed components: `kind="grinder"`, `brand`, `model`, `attrs_json` holding `burrs` + `rpmCapable`). A package owns one grinder item today; more kinds slot in later with no schema migration.
- **BREAKING** `coffee_bags` and `shots` replace the grinder identity columns (`grinder_brand`, `grinder_model`, `grinder_burrs`) with a single **`equipment_id`** foreign key pointing at a package (**reference semantics** — editing a package flows to history; soft-delete via `inInventory = 0` preserves old shots). Both tables **keep** `grinder_setting` and **gain** `rpm` as dial-in snapshots.
- New **RPM dial-in field**, shown in Brew Settings only when the package's grinder is RPM-capable. `rpmCapable` is **derived, not user-toggled**: a grinder matched in `GrinderAliases` uses its existing `variableRpm` flag; a custom grinder typed in by the user (not in the registry) is treated as capable so the field shows.
- **BREAKING** Brew Settings grinder fields (brand/model/burrs text inputs in `BrewDialog.qml`) replaced by a **read-only "Equipment: …" row + Switch Equipment button** (mirroring Switch Beans). Grind setting and RPM remain editable inline as dial-in preferences.
- New **Equipment window** (`EquipmentPage.qml`) — empty state + "Add Equipment", a card per package — mirroring `BeanInfoPage.qml`.
- New **Switch Equipment dialog** — pick an existing package or create one (grinder brand/model/burrs, with registry-backed suggestions exactly as the old fields had), mirroring `ChangeBeansDialog.qml`.
- New **idle-page Equipment button** placed next to Beans in the default layout, **injected into every existing user's saved layout** by an idempotent run-once layout migration so it is initially visible for all users (and movable afterwards).
- **Grinder-scoped dial memory**: switching equipment applies the package's `lastGrindSetting`/`lastRpm` to the active bag and Brew Settings (never blank, still editable), just as switching beans applies the bag's. Editing grind/RPM writes through to **both** the active bag and the active package.
- **Migration (migration 22, run-once on first launch):**
  - **Split grind + RPM**: best-effort parse of every existing `grinder_setting` on bags and shots — a trailing `(\d+)\s*rpm` token (e.g. `"24 1400rpm"`) is split into `grinder_setting = "24"` and `rpm = 1400`; anything without an explicit `rpm` marker is left exactly as-is.
  - **Create packages**: one default package from the user's current grinder settings, plus one package per distinct historical grinder *identity* (brand/model/burrs) found across bags and shots. Differing grind/RPM values **never** create a package.
  - **Link**: every bag and shot gets an `equipment_id` pointing at its matching package; each row keeps its (now split) grind setting + rpm.
- **MCP gains equipment awareness now**: grinder reads resolve through the package and expose package identity + rpm; three new tools — **`equipment_list`**, **`equipment_select`**, **`equipment_update`** — mirror the existing `bag_*` tools.
- **Visualizer**: upload resolves `equipment_id` → `"brand model"` + setting + dose (shape unchanged); **RPM is appended to the `grinder_setting` string** (`"2.4 1400rpm"`) since Visualizer has no RPM field. Burrs has no Visualizer field today and remains dropped. **Import is unchanged** (it imports profiles only — grinder strings are display-only metadata, never persisted to a local shot/bag).

## Capabilities

### New Capabilities

- `equipment-package-model`: `equipment_packages` + `equipment_items` data model, DB schema, migration 22 (grind/rpm split, package creation, linking), reference semantics + soft-delete, `SettingsDye` resolution and dual write-through, `rpmCapable` derivation
- `equipment-inventory-view`: Equipment window + idle-page Equipment button + idempotent layout-injection migration + 4-place layout-widget registration
- `switch-equipment-dialog`: Switch Equipment dialog — pick or create a package, registry-backed grinder suggestions, switch applies the package's last dial
- `brew-settings-equipment`: Brew Settings grinder block reworked to a read-only Equipment summary + Switch Equipment button, with grind setting + RPM (when capable) as editable dial-in

### Modified Capabilities

- `dialing-context-payload`: grinder context resolves via `equipment_id` and gains the `rpm` dial-in
- `visualizer-upload-persistence`: upload resolves `equipment_id` → grinder strings; RPM appended to `grinder_setting`
- `mcp-server`: grinder reads resolve via the package + expose package/rpm; new `equipment_list` / `equipment_select` / `equipment_update` tools
- `shot-save-filter`: shot snapshots capture `equipment_id` + `rpm` (in place of the dropped grinder identity columns)

## Impact

**Dependency:** builds on the in-progress `bean-bag-inventory` change — assumes the `coffee_bags` table and its `grinder_*` columns exist (the source of the migration).

**C++ / Settings:**
- `src/core/grinderaliases.h`: `variableRpm` already present (no registry edit needed); used to seed `rpmCapable`. Registry stays hardcoded.
- `src/core/settings_dye.{h,cpp}`: `dyeGrinderBrand/Model/Burrs` become **read-only**, resolved through the active bag's `equipment_id`; `dyeGrinderSetting` + new `dyeGrinderRpm` write through to **both** bag and active package; new `activeEquipmentId` + switch logic applying the package's last dial.
- New `src/history/equipmentstorage.{h,cpp}`: `EquipmentPackage`/`EquipmentItem` value types + DB CRUD via `withTempDb()`, following the `CoffeeBagStorage` background-thread pattern.
- `src/history/shothistorystorage.cpp`: **migration 22** (equipment tables; `coffee_bags`/`shots` gain `equipment_id` + `rpm`, drop grinder identity columns; grind/rpm split; package creation + linking).
- `src/history/shotprojection.{h,cpp}` + history queries: `grinderBrand/Model/Burrs` resolved via `equipment_id` JOIN; `getDistinctGrinder*` re-scoped to `equipment_items WHERE kind='grinder'`.
- `src/network/visualizeruploader.cpp`: resolve `equipment_id` → strings; append rpm to `grinder_setting`.

**QML:**
- New `qml/pages/EquipmentPage.qml` (mirrors `BeanInfoPage.qml`), `qml/components/EquipmentCard.qml`, `qml/components/SwitchEquipmentDialog.qml` (mirrors `ChangeBeansDialog.qml`).
- `qml/components/BrewDialog.qml`: grinder brand/model/burrs inputs → read-only Equipment summary + Switch Equipment button; grind + RPM dial-in inputs (RPM shown when capable).
- New `qml/components/layout/items/EquipmentItem.qml` + 4-place registration (`CMakeLists.txt`, `LayoutItemDelegate.qml`, `LayoutCenterZone.qml`/editor palette + chip label map, `shotserver_layout.cpp`).

**Database:**
- Migration 22 in `shothistorystorage.cpp`: new `equipment_packages` + `equipment_items` tables; `coffee_bags`/`shots` `equipment_id` (nullable FK) + `rpm`; drop `grinder_brand`/`grinder_model`/`grinder_burrs`; grind/rpm split; package dedup + linking.
- Layout config (`settings_network.cpp`): default layout gains an `equipment` item next to `beans`; idempotent run-once migration injects it into existing saved layouts.

**MCP:**
- `mcpresources.cpp` + `mcptools_dialing.cpp` + `mcptools_ai.cpp`: grinder reads resolve via the package; dialing surfaces gain `rpm` + package identity.
- `mcptools_write.cpp`: new `equipment_list`, `equipment_select`, `equipment_update` tools mirroring `bag_*`.

**Transfer/backup:**
- `ShotHistoryStorage::importDatabaseStatic`: migrate `equipment_packages` + `equipment_items`, remap `coffee_bags.equipment_id` and `shots.equipment_id` to new package ids (alongside the existing bag-id remap).
- `SettingsSerializer`: `dye/activeEquipmentId` excluded from export (like `dye/activeBagId`).
