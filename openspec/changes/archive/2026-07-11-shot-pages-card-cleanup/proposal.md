## Why

On the Shot Detail and Shot Review pages, grind/rpm — a per-shot dial-in that belongs to the bean bag or the recipe — currently leaks into the Equipment card, and is echoed 3–4 times per page (metrics-row Grind cell, bean-card title suffix, Equipment card, and a proposed header append). Meanwhile a user swiping between shots to compare them (issue #1447) has no at-a-glance data line and must scroll to read each shot's key numbers. This change makes every card show only the data it keeps, and adds one glanceable Shot Plan snapshot line at the top. It supersedes and replaces the deleted `shot-detail-header-grind-rpm` change, whose bespoke header grind/rpm append is subsumed by the snapshot line.

## What Changes

- **Shot Plan snapshot line (new)** — both pages gain a second line under the title (`<name> · <date>`) that renders a per-shot snapshot of the Shot Plan sentence (e.g. `18.0g in · 42.0g · Yemen West Haraz · grind 25 · 1400 rpm`). Reuses `ShotPlanText.qml`, fed from the shot's frozen snapshot instead of the live dial. Its **fields and order mirror the user's own Shot Plan widget** (the first `shotPlan` widget in their idle layout), minus `profile` and `temperature` — both already shown in the title line above — so the line matches what they configured without a second picker. RPM renders only for grinders that report it (`grinderRpmCapable`), so a stale/spurious recorded RPM (e.g. a Niche Zero) never shows. This is the swipe-to-compare surface from issue #1447. It **replaces** the header grind/rpm title append the deleted change proposed.
- **Equipment card: no grind/rpm** — on both shot pages, stop feeding `grindSetting`/`rpm` into `EquipmentSummary`. The card shows grinder, burrs, basket, and puck prep only. (The inventory `EquipmentCard.qml` already had this applied.)
- **Grind's home follows provenance** — grind/rpm renders on the **recipe card when the shot used a recipe** (`recipeId > 0`), otherwise on the **bean card**. Retire the bean-card-title `Beans (9)` grind suffix.
- **Recipe card (new)** — shown only when `shotData.recipeId > 0`. Recipe identity (name, drink type, profile) is live-resolved by `recipeId`; the grind/rpm it displays comes from the shot's own snapshot fields, never the recipe's possibly-since-edited pin. No schema change (a recipe with linked shots can only be archived, never hard-deleted, so the row always resolves).
- **REMOVED** — the dedicated metrics-row Grind cell on Shot Detail. Grind now appears exactly twice per page: the snapshot line (glanceable) and its one owning card (canonical).
- **NOT in scope** — a *new* field picker: rather than build one, the snapshot line reuses the field selection the user already made on their Shot Plan widget (issue #1447's configurability, satisfied by reuse). No DB migration.

## Capabilities

### New Capabilities
- `shot-plan-snapshot-line`: a per-shot Shot Plan snapshot readout line at the top of the Shot Detail and Shot Review pages, rendered from the shot's frozen snapshot via a parameterized `ShotPlanText`.
- `shot-recipe-card`: a recipe card shown on the shot pages when the shot used a recipe, plus the cross-card rule that grind/rpm lives on the recipe card (recipe used) or bean card (no recipe) and never on the Equipment card.

### Modified Capabilities
- `shot-detail-metrics`: removes the top-level metrics-row Grind cell requirement — grind moves to the snapshot line and its owning card.

## Impact

- Affected code: `qml/pages/ShotDetailPage.qml`, `qml/pages/PostShotReviewPage.qml` (top-of-page snapshot line, Equipment/Bean card feeds, new recipe card block, metrics-row cell removal); `qml/components/ShotPlanText.qml` (parameterize the few getters that read live singletons — rpm + rpm-capable, temperature override, beverage type, cleaning flag — with live-default properties so the home-screen widget is unchanged); reuse of `qml/components/RecipeDrinkCard.qml` and `qml/components/EquipmentSummary.qml`.
- Data: already available — `shotData.recipeId` is projected (`shotprojection.cpp`); grind/rpm, bean, and equipment are already snapshotted per shot. Live-resolve of recipe identity relies on the `recipestorage.cpp` invariant that shot-linked recipes cannot be deleted.
- No database schema change, no migration, no C++ storage change.
- Accessibility: the snapshot line and recipe card need proper `Accessible` roles/names; fix pre-existing violations in touched files. All new user-visible strings internationalized.
