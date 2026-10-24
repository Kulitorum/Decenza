# Grind Calibration Anchors on the Equipment Package (Phase 2)

> Split from `fix-grinder-calibration-cross-profile` (archived 2026-07-07). Phase 1 —
> the #1223 harm-reduction fix (within-coffee conversion key, extrapolation cap,
> directional-only fallback) — shipped in PR Kulitorum/Decenza#1236. This change is
> Phase 2. It was first implemented in PR Kulitorum/Decenza#2070 as a separate
> per-(model, burrs) store whose key replaced the mined one; review found that design
> bypassed every Phase 1 safeguard, so this change was rewritten before landing in
> the same PR.

## Why

Phase 1 mines a grind conversion key from the user's own history: within one roast
batch, dialed-in shots on two profiles far enough apart on the UGS scale form a
pair, and the median of enough agreeing pairs becomes the key. On real databases the
mined data rarely clears the publish gates, so most users get only directional
guidance — and they cannot see that, or why. Nothing tells them the app already
calibrates automatically, how far along it is, or what one deliberate pair of shots
would add.

## What Changes

- **Anchors belong to the equipment package.** A package may hold one *fine* and one
  *coarse* calibration anchor: references to two of its own shots
  (`equipment_packages.calib_fine_shot_id` / `calib_coarse_shot_id`). No separate
  settings store.
- **Anchors join the automatic calibration, under its rules.** A valid anchor pair is
  one more within-batch pair in Phase 1's mining: same coffee batch, same RPM,
  ≥ 2 UGS apart, no quality badges; marking a shot counts as "dialed-in", and one
  shot per side is allowed. A pair that fails these is refused when marked. A key
  publishes on ≥ 3 agreeing mined pairs (as today) **or** one valid anchor pair; when
  both exist the median includes the anchor pair, and an anchor slope outside the
  mined spread is reported in the panel and the log. No new `"calibrated"`
  confidence — the key's source is a `basis` field (`history` / `anchors` / `both`).
- **The calibration becomes visible.** A Grind calibration panel on the equipment
  package (Equipment page; placement TBD — card or edit dialog) shows what history
  has found, the anchors, whether they agree, and why advice is still directional
  when it is. Tapping an anchor opens its shot; each anchor can be cleared.
- **The post-shot review page updates it.** "Mark as fine / coarse anchor" on any
  espresso shot with equipment — recipe shots included — writes to that shot's
  package, refusing an ineligible shot with the reason.
- **Equipment archive.** A used package's removal becomes **Archive**, with "Show
  archived (N)" and **Restore** on the Equipment page (app and web), and recreating
  archived gear restores it instead of duplicating it. Found while verifying: the main
  package on a real database had been removed yet stayed in use, invisible.
- **Web parity.** ShotServer `/equipment` shows the same panel; the web shot page
  offers the same mark actions.
- Removed from the first #2070 draft: the `SettingsGrinder` domain, the precedence
  override, the default-off long-hop gate, and the snapshot threading through the
  MCP and advisor workers.

## Capabilities

### New Capabilities

- `grinder-ugs-calibration`: package-owned calibration anchors — storage and their
  lifecycle (delete, merge, fork, device transfer), mark-time eligibility, how a pair
  combines with mined pairs, the equipment calibration panel, the post-shot mark
  action, and ShotServer parity.

### Modified Capabilities

- `dialing-context-payload`: the grinderCalibration block counts a package's anchor
  pair as a within-batch pair, scopes by equipment package, drops the never-built
  `"calibrated"` confidence, and adds `basis` / `markedAnchors` when anchors exist.

## Impact

- **Schema**: migration 46 adds two nullable columns to `equipment_packages`.
- **Code**: `src/ai/dialing_blocks.cpp` (mining), `src/history/equipmentstorage.*`
  (anchor storage, merge, import), `src/history/shothistorystorage.cpp` (migration,
  import post-pass), `src/ai/aimanager.*` (async calibration request),
  `qml/components/GrindCalibrationPanel.qml` (new), `EquipmentInfoDialog.qml`,
  `EquipmentCard.qml`, `PostShotReviewPage.qml`, ShotServer equipment and shot pages.
- **Users**: no change for anyone who never marks an anchor, apart from the new
  read-only panel. The panel is the "informed choice": most users with enough history
  will see they need not do anything.
