# Design — Equipment Packages

## Context

Today the grinder is not a thing the user owns; it is four columns copied onto every coffee bag. `SettingsDye` exposes `dyeGrinderBrand/Model/Burrs/Setting` and write-through to the active `coffee_bags` row (see `bean-bag-inventory`). The `GrinderAliases` registry (`src/core/grinderaliases.h`) already classifies known grinders, including a `variableRpm` flag, but nothing surfaces it.

The user's framing: an **Equipment package** is the physical kit used to pull a shot. It behaves like Beans — you add packages, you switch between them, you don't edit the active one inline. The grinder is its first component; baskets/tampers/etc. are coming, so the model must grow without re-migrating.

## Key Decisions

### 1. Container + components, not a grinder row
`equipment_packages` is a container; `equipment_items` holds typed components (`kind`, `brand`, `model`, `attrs_json`). Shared fields every kind has (`kind`, `brand`, `model`) are real columns so `getDistinctGrinder*` queries stay simple (`WHERE kind='grinder'`); kind-specific fields (`burrs`, `rpmCapable` now; basket size later) live in `attrs_json` (same blob convention as Bean Base data). **Adding a tamper later is a new `kind` row — no schema migration.** The dormant `add-basket-settings` change slots in here.

**Alternative rejected:** wide grinder columns on the package, refactor when the second component lands. Rejected because growth is a stated near-term certainty, so we pay the container cost up front to avoid a second migration.

### 2b. Copy-on-write immutability (REVISED — supersedes the snapshot discussion below)

Historical fidelity is achieved **without any grinder shadow data on shots**, by making a package's *identity* immutable once it is used:

**Immutability of identity, copy-on-write on edit.** A package's component **identity** is frozen once `shotCount > 0`. Editing identity does **not** mutate it — it **creates a new package** (copy-on-write), repoints every bag pointing at the old package (and the active selection) to the new one, and soft-deletes the old package out of inventory (it persists for the shots that reference it). An unused package (`shotCount == 0`) edits in place.
- **Grinder identity = `(brand, model, burrs)`** — burrs included. Same grinder body with swapped burrs is a *different* identity ⇒ a different package, naturally tracked via copy-on-write (old shots → burrs-X package, new shots → burrs-Y package).
- **Merge key = the full package signature** — the canonical set of *all* component identities, not just the grinder. Today (grinder-only) that reduces to brand+model+burrs; when a basket/tamper is added, two packages merge only when every component matches. Copy-on-write that produces an identity equal to an existing package **merges** into it (repoints to the existing) instead of forking a twin. Write the dedup/merge as a package signature so it extends without rework.

**Dial memory is not identity.** `last_grind_setting`/`last_rpm` are mutable MRU dial-memory, freely updated on every dial edit; never resolved through a shot's pointer, so they never churn a package. (Per-shot dial-in lives on the shot row, not the package — putting it on the package would fork a package every shot.)

**Name is a short label + state-based versioning.** `name` is a stored, user-editable short label (default "{brand} {model}"; becomes a kit label like "Espresso setup" with more components — never content-derived, which would grow unbounded). On copy-on-write the **new** (current) package keeps the name; the **old** package keeps its name too but is marked `in_inventory = 0` plus a **`superseded_by`** lineage pointer to the fork. Current-vs-old is rendered from that state at display time (e.g. "My Grinder · older" in shot history) — **not** baked into the name (no reserved characters, no re-mangling on repeated forks). Duplicate names among current packages are allowed; the `id` is the identity. The lineage chain also powers "dial history across burr swaps" later.

**Shots are pure pointer.** Migration 23 drops `grinder_brand/model/burrs` from `shots` **and** `coffee_bags`. A shot keeps only `equipment_id` (→ an immutable package) plus its per-shot **dial-in** (`grinder_setting` + `rpm`). Because the referenced package's identity can never change after the shot exists, the pointer alone is faithful history — no snapshot, no shadow columns.

**Search without shadow data.** Grinder identity lives only on `equipment_items`. Shot-history search resolves the term against `equipment_items` for **all** packages referenced by history — inventory or not (a sold grinder still surfaces its old shots, same as the beans history lane) — → matching `package_id`s → `shots.equipment_id IN (…)`; `shots_fts` drops its grinder columns. Pointer-resolved grinder hits don't carry FTS ranking/snippets (acceptable).

**Device transfer.** `equipment_packages` + `equipment_items` transfer with id-remap; `equipment_id` on both bags and shots is remapped on import (mirrors the bag-id remap). `superseded_by` is remapped too.

**Consistency note (intentional):** equipment uses pure-pointer-to-immutable; beans keep an identity snapshot on the shot (`shot.bean_brand`). Two different fidelity mechanisms in `shots`, intentionally — equipment is new and de-duplication is its whole point; beans are legacy. **Future note:** with multi-component packages, decide per-component vs per-package versioning granularity (a basket swap shouldn't necessarily fork the whole kit).

The snapshot-vs-reference discussion in §2 below is the earlier framing; §2b is the decision.

### 2. Pointer everywhere (reference semantics), dial-in stays snapshot
`coffee_bags.equipment_id` and `shots.equipment_id` point at a package. Grinder brand/model/burrs are resolved by following the pointer — strings are only materialized at external boundaries (Visualizer DYE payload, MCP, history display).

- **Editing a package** (fix `"83mm"` → `"83mm flat steel"`) flows to every bag and historical shot pointing at it. For durable hardware reused across hundreds of shots this is desirable, unlike a bean bag (a distinct physical bag → snapshot).
- **Replacing a grinder** = create a new package + soft-delete the old (`inInventory = 0`, row persists), so old shots still resolve to the old grinder.
- **Dial-in (grind setting, RPM) stays snapshotted** per bag and per shot, because it genuinely changes shot to shot. So a shot row carries `equipment_id` (reference) + `grinder_setting` + `rpm` (snapshots).

**Alternative considered:** snapshot grinder strings on each shot (frozen history). Rejected per the user's "pointer everywhere" preference; reference is the better fit for durable hardware.

### 3. Two independent dial memories
Grind setting / RPM are remembered in two places, each restored by its own switch:
- The **bag** keeps its last dial (bean-scoped) — switching beans applies it (unchanged from today).
- The **package** keeps `lastGrindSetting` / `lastRpm` (grinder-scoped) — switching equipment applies it.
- Editing grind/RPM **fans out to both** the active bag and the active package, so each dimension stays current and whichever you switch restores from that dimension. Switching equipment therefore never leaves the field blank; the value is a starting point, fully editable.

### 4. RPM capability is derived, not configured
`rpmCapable` is set when the package's grinder item is created:
- grinder **matched in `GrinderAliases`** → its `variableRpm` flag.
- grinder **not in the registry** (custom typed-in) → `true`, so the RPM field shows. This honors "when a user enters a grinder not in the table, show the RPM."

The registry is **not user-editable**; users pick from it or type a custom grinder. `rpmCapable` re-derives if a package's brand/model is edited.

### 5. Migration (migration 22, run-once)
Three independent operations in one pass:
1. **Grind/RPM split** — conservative, marker-gated. Match trailing `(\d+)\s*rpm$` (case-insensitive); on hit, `rpm` = the number, `grinder_setting` = the remainder trimmed; otherwise leave the setting untouched and `rpm` empty. Compatible with `parseGrinderSetting`'s existing rpm-suffix tolerance, so anything not split still parses.
2. **Package creation** — dedup key is **grinder identity only** `(brand, model, burrs)`. Current settings → the default package; each remaining distinct identity across bags+shots → its own package. Grind/RPM differences never enter the key, so a one-grinder user with many grind settings gets exactly one package.
3. **Linking** — every bag/shot row gets `equipment_id` = its matching package; the (split) grind + rpm stay on the row. Package `lastGrindSetting`/`lastRpm` seed from current settings (default package) or the grinder's most-recent shot (per-grinder packages).

Empty grinder strings → `equipment_id` NULL, no package.

### 6. Idle-button layout migration
The idle Equipment button must appear for **all** existing users, including those with customized layouts. `getLayoutObject()` (`src/core/settings_network.cpp`) already runs idempotent run-once migrations (statusBar injection, `text`→`custom`). Add one: scan all zones; if no item has `type=="equipment"`, insert `{"type":"equipment","id":"equipment1"}` immediately after the `beans` item (fallback: append to `bottomRight` if Beans was removed), then persist once. The default layout also gains the item next to `beans` for fresh installs.

## Boundaries

- **Visualizer** has no equipment concept and a flat, lossy grinder schema: `grinder_model` (brand+model combined), `grinder_setting`, `grinder_dose_weight`; no slot for brand-separate, burrs, or RPM. Upload resolves the pointer to those strings and **appends RPM to `grinder_setting`** (`"2.4 1400rpm"`), matching the convention many users already type by hand. Import is profile-only and untouched.
- **MCP** gains equipment awareness now: reads resolve through the package and expose package identity + rpm; `equipment_list`/`equipment_select`/`equipment_update` mirror `bag_list`/`bag_select`/`bag_update`. MCP data conventions apply (units in field names, `kind` as a string enum, ISO timestamps).
- **Grinder calibration** (`stop-at-weight-learning` / the in-flight `fix-grinder-calibration-cross-profile`) is keyed on resolved grinder model + burrs — inputs unchanged, now sourced via the package. This change does **not** re-architect calibration keying; it depends on / defers to that work.

## Risks

- **Mis-split on migration**: a non-RPM trailing number could be misread as RPM. Mitigated by requiring the literal `rpm` marker; anything else is left verbatim.
- **Over-creation of packages**: a typo in a historical grinder string spawns an extra package. Acceptable — it is correct (the string really differed) and the user can soft-delete it.
- **Reference-semantics surprise**: editing a package retroactively changes old shots. Stated explicitly to the user and accepted as correct for durable hardware; replacement is the create-new + soft-delete path.
- **Dependency ordering**: requires `bean-bag-inventory`'s `coffee_bags` schema. Migration 22 must run after migration 19's table exists.
