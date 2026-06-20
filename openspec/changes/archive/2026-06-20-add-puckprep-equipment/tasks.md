# Tasks — Add Puck Prep Equipment

## 1. Storage model (equipment-package-model)
- [x] 1.1 Extend create/update in `equipmentstorage.{h,cpp}` to write an optional `kind="puckprep"` item (flags in `attrs`); `setPuckPrepItemStatic` upserts on any flag set, deletes when all clear
- [x] 1.2 Widen the dedup/fork identity (`findPackageBy…IdentityStatic`, `supersedeOrEditStatic`) to include the NORMALIZED puck-prep config; "no puck prep" is a distinct value, comparison is order-independent
- [x] 1.3 Resolve the puck-prep item in `EquipmentPackageView` / `toVariantMap()`; derive the `distribution` rollup at read time (pure function of flags)
- [x] 1.4 Carry puck-prep items through `importEquipmentStatic` + the full-identity merge-dedup match
- [x] 1.5 `SettingsDye` bridge: active puck-prep flags + derived `distribution` display; thread flags through switch/create/update
- [x] 1.6 Unit tests: optional puck prep, identity widening (fork/dedup/edit-in-place, order-independence), all-clear deletes the item, derive-at-read, import dedup (same gear + different prep must NOT merge)

## 2. Shot resolution + AI/MCP (dialing-context-payload, mcp-server)
- [x] 2.1 Resolve puck-prep flags via the `equipment_id` JOIN in `shotprojection`/`shothistory_types`/`loadShotRecordStatic` (new `LEFT JOIN equipment_items` `kind='puckprep'`; APPEND columns after the basket ones; update positional reads)
- [x] 2.2 Extend the `tst_dbmigration` JOIN regression to pin the new puck-prep column position (guards the silent column-shift the basket PR flagged)
- [x] 2.3 `dialing_blocks`: add the `puckPrep` sub-object to `currentBean` (flags + `distribution`); omit when absent
- [x] 2.4 MCP `equipment_*` tools: read + accept the optional puck-prep flags; dialing tools surface the sub-object per conventions
- [x] 2.5 `tst_dialing_blocks`: pin the `currentBean.puckPrep` payload (present / omitted / distribution derivation)

## 3. Picker UI (switch-equipment-dialog)
- [x] 3.1 Add a reusable labeled-checkbox row primitive (accessible `CheckBox` role + checked-state name)
- [x] 3.2 Add the optional puck-prep checkbox section to `SwitchEquipmentDialog` (WDT, Shaker, Puck screen, Bottom paper filter, RDT) in the two-column form layout; all-unchecked = no puck prep
- [x] 3.3 Translation keys + accessibility for the puck-prep section

## 4. Inventory view (equipment-inventory-view)
- [x] 4.1 `EquipmentCard`: puck-prep summary line (omit when none)
- [x] 4.2 `EquipmentInfoDialog`: puck-prep rows (flags + distribution; omit when none)

## 5. Verification
- [x] 5.1 Quick compile check via Qt Creator MCP; run equipment / dbmigration / dialing_blocks / mcptools_write suites
- [~] 5.2 Live (simulation) end-to-end. WRITE path confirmed live — `equipment_list` shows `puckPrep` (flags + derived `distribution`) on the active Niche Zero package, set via the UI. DIALING path covered by the `buildCurrentBeanBlock` unit test (the shared helper `dialing_get_context` uses). The live-shot leg (pull a simulated shot → confirm `currentBean.puckPrep`) was declined by the user, so left as a manual check.

## 6. Follow-ons (NOT in this change — tracked here)
- [~] 6.1 Tamper (base shape flat/convex/ripple) as one enum field on the puck-prep item
- [~] 6.2 Sift as a 6th flag (folds into the `distribution` rollup)
