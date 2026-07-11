## Context

The Shot Detail (`qml/pages/ShotDetailPage.qml`) and Shot Review (`qml/pages/PostShotReviewPage.qml`) pages render a set of cards (bean, equipment) plus a metrics row and a header. Grind/rpm currently appears in up to four places per page, including the Equipment card, which conceptually should describe only the gear. A separate feature request (issue #1447) asks for an at-a-glance data line so shots can be compared by swiping without scrolling.

Key existing facts we build on:
- A shot already snapshots its dial-in (`grinderSetting`, `rpm`), bean identity, equipment identity, and `profile_name`. These are frozen per shot — the established "historical shot = frozen record" pattern.
- A shot stores `recipeId` (the recipe active at shot start), projected to QML via `shotprojection.cpp`. Only the id is stored — no recipe name/plan snapshot.
- `RecipeStorage` refuses to hard-delete a recipe that any shot references (`recipestorage.cpp` — "shot rows can always name their recipe"); such recipes can only be archived. So resolving a recipe by id from a shot always succeeds.
- `ShotPlanText.qml` renders the home-screen Shot Plan sentence. Most inputs are already plain properties with live defaults (its own comment notes they are "parameterizable so non-live consumers can render THEIR overrides"). A few getters still read live singletons directly.
- `EquipmentSummary.qml` is a shared renderer; when not fed `grindSetting`/`rpm` its last-dial line collapses (already used to drop grind from the inventory card).
- `RecipeDrinkCard.qml` is the single recipe card, but it expects a full recipe map and page-agnostic extras.

## Goals / Non-Goals

**Goals**
- Every card shows only the data it keeps; grind/rpm leaves the Equipment card.
- Grind/rpm has exactly one canonical card home per shot: recipe card if `recipeId > 0`, else bean card.
- A per-shot Shot Plan snapshot line at the top of both pages (issue #1447's glanceable comparison surface).
- A recipe card on both pages when a recipe was used, with zero schema change.
- Grind appears exactly twice per page: snapshot line + owning card.

**Non-Goals**
- Issue #1447's fully user-configurable field picker (fixed snapshot line ships first; configurability is a possible follow-up reusing the layout readout schema).
- Any DB schema change, migration, or C++ storage change.
- Changing the home-screen Shot Plan widget's behavior.

## Decisions

### D1: Snapshot line reuses `ShotPlanText`, fed from shot data
Render the top line with `ShotPlanText`, overriding its input properties with the opened shot's snapshot values instead of the live dial. **Why**: one renderer means the snapshot sentence can never drift from the home-screen plan's format, wrapping, and i18n. **Alternative considered**: a bespoke sentence built inline on each page (what the deleted change did for the header) — rejected as duplicative and drift-prone.

`ShotPlanText` needs a small parameterization pass for the getters that still read live singletons directly, each exposed as an overridable property defaulting to today's live read (so the home widget is untouched):
- `_grindStr`: reads `Settings.dye.dyeGrinderRpm`, `grinderRpmCapable(...)`, `dyeGrinderBrand/Model` → add `grindRpm`, `rpmCapable` properties.
- `_tempStr`: reads `Settings.brew.hasTemperatureOverride` → already partly covered by `tempOverridden`; ensure the display path uses the property.
- `_isCleaning` / `_bevType`: read `ProfileManager.currentProfile...` → add `beverageType`, `isCleaning` properties.

### D2: Recipe card = live-resolved identity + shot-snapshot dial-in
The recipe card resolves name/drink-type/profile live by `recipeId`, but shows the grind/rpm the shot actually used (from the shot snapshot), never the recipe's current pin. **Why**: the delete-guard invariant guarantees the row survives, so live-resolve is safe and follows renames; but "what was actually pulled" is a fact of the shot, so its dial-in must come from the frozen snapshot. This needs no new columns. **Alternatives considered**: (a) snapshot recipe fields onto the shot — durable and consistent with bean/equipment, but requires an `ALTER TABLE` and a migration for no functional gain given the invariant; (b) show the recipe's current pin on the card — rejected because it would misreport historical shots after a pin edit.

### D3: Grind's card home follows provenance (`recipeId > 0`)
One boolean decides where grind renders: recipe card when a recipe was used, bean card otherwise. **Why**: matches the mental model "grind lives in the bag or the recipe"; avoids showing grind on two cards. The Equipment card is never a grind home.

### D4: Remove the metrics-row Grind cell; retire the bean-title suffix
With the snapshot line providing the glanceable grind and the owning card providing the canonical one, the dedicated metrics-row Grind cell and the `Beans (9)` title suffix are redundant and removed. **Why**: keeps grind to two intentional surfaces per page and restores a clean five-metric row.

### D5: Reuse `RecipeDrinkCard` where practical
Prefer feeding `RecipeDrinkCard` a recipe map resolved from `recipeId` plus the shot's dial-in extras, rather than authoring a second recipe card. **Why**: single recipe-card renderer, consistent look. If its map/extras shape doesn't fit the read-only shot context cleanly, fall back to a compact recipe block built from the same fields — decided during implementation.

## Risks / Trade-offs

- **[Async recipe resolve causes a brief empty/late card]** → resolve on shot load via the existing storage request/ready pattern; the card is `visible` only once identity is available (and always when `recipeId > 0`, showing at least the name). Avoid main-thread DB reads.
- **[`ShotPlanText` parameterization regresses the home widget]** → every new property defaults to the current live read; the home-screen widget passes nothing new, so its behavior is byte-for-byte unchanged. Verify the home Shot Plan visually after the change.
- **[Grind hidden for users who relied on the metrics cell]** → grind remains on the page (snapshot line + owning card), only the duplicate is gone; call this out in the PR.
- **[Recipe card shows a renamed/archived recipe differently than at shot time]** → intended (identity follows the live recipe; the dial-in stays frozen); documented in the spec scenarios.

## Migration Plan

Pure QML display change; no data migration. Ship both pages together. Rollback is reverting the QML. The deleted `shot-detail-header-grind-rpm` change is already removed and superseded by this one.

## Open Questions

- Whether to place the recipe card first (leftmost/top) or after the bean card in the card row — resolve visually during implementation; the spec does not constrain order.
- Whether `RecipeDrinkCard` can be reused directly for the read-only shot context or a compact variant is warranted (D5) — decided when wiring the resolved map.
