## 1. Parameterize ShotPlanText for per-shot snapshots

- [x] 1.1 In `qml/components/ShotPlanText.qml`, add overridable properties defaulting to today's live reads: `grindRpm` (default `Settings.dye.dyeGrinderRpm`), `rpmCapable` (default `Settings.dye.grinderRpmCapable(Settings.dye.dyeGrinderBrand, Settings.dye.dyeGrinderModel)`), `beverageType` (default `ProfileManager.currentProfileBeverageType`), `isCleaning` (default `ProfileManager.currentProfileIsMaintenance`).
- [x] 1.2 Rewrite `_grindStr`, `_bevType`, `_isCleaning`, and the temperature display path to read those properties instead of the singletons directly, so a consumer can fully override them.
- [x] 1.3 Verify the home-screen Shot Plan widget is visually unchanged (all new properties fall back to live reads when not set).

## 2. Shot Plan snapshot line at the top of both pages

- [x] 2.1 In `qml/pages/ShotDetailPage.qml`, add a `ShotPlanText` beneath the title line, fed entirely from `shotData` (profileName, dose, grindSize, grindRpm, rpmCapable, roasterBrand, coffeeName, roastDate, profileYield/targetWeight, temperature + override flags, beverageType, isCleaning). Compact form (`maxLines: 1` or as fits), non-interactive (no Brew Settings navigation on tap).
- [x] 2.2 Ensure the snapshot line hides cleanly when the shot has no snapshot data, and omits absent fields without dangling separators.
- [x] 2.3 Give the snapshot line a single combined accessible name (its inner items ignored); internationalize any new strings.
- [x] 2.4 Apply the same snapshot line to `qml/pages/PostShotReviewPage.qml`, bound to the review page's live edit state where the page treats those as the source of truth (mirroring how the page already handles edited fields).

## 3. Equipment card: drop grind/rpm on both shot pages

- [x] 3.1 In `ShotDetailPage.qml`, stop feeding `grindSetting`/`rpm`/`rpmCapable` into the `EquipmentSummary` (equipment card), so its last-dial line collapses. Update the card's `visible` gate and accessible name so they no longer depend on grind/rpm.
- [x] 3.2 Apply the same to `PostShotReviewPage.qml`'s equipment card. (Already satisfied — that card was authored to omit grind/rpm; verified the `EquipmentSummary` feed and `visible`/accessible gates never reference grind.)

## 4. Recipe card (shown when recipeId > 0)

- [x] 4.1 On shot load, when `shotData.recipeId > 0`, resolve the recipe identity by id via the existing `RecipeStorage` async request/ready pattern (no main-thread DB I/O); hold the resolved map in a page property.
- [x] 4.2 Add a recipe card block to `ShotDetailPage.qml`, visible only when `recipeId > 0`. Reuse `RecipeDrinkCard.qml` fed the resolved recipe map plus the shot's dial-in extras; if its shape doesn't fit the read-only context, use a compact recipe block from the same fields (per design D5).
- [x] 4.3 The recipe card's grind/rpm (when shown) comes from the shot snapshot (`shotData.grinderSetting`/`rpm`), never the recipe's current pin.
- [x] 4.4 Give the recipe card a grouping accessible name summarizing the recipe (inner items ignored); internationalize new strings.
- [x] 4.5 Apply the same recipe card + resolution to `PostShotReviewPage.qml`.

## 5. Route grind to its owning card; remove duplicates

- [x] 5.1 Show grind/rpm on the bean card ONLY when `recipeId <= 0` (grind is bag-scoped without a recipe); when `recipeId > 0`, grind lives on the recipe card instead.
- [x] 5.2 Remove the bean-card title grind suffix (`Beans (9)` → `Beans`) in `ShotDetailPage.qml` (~lines 831-844) and the equivalent on the review page if present.
- [x] 5.3 Remove the dedicated metrics-row Grind cell in `ShotDetailPage.qml` (~lines 646-681); confirm the metrics row reflows to a clean five-metric row.

## 6. Registration, accessibility, and verification

- [x] 6.1 If any new QML file is introduced, add it to `CMakeLists.txt` (`qt_add_qml_module` file list). (No new files — parameterized the existing `ShotPlanText` and used inline compact recipe/grind blocks reusing existing components; nothing to register.)
- [x] 6.2 Fix any pre-existing accessibility violations in the touched sections of both pages (roles, names, focusable, onPressAction) per `docs/CLAUDE_MD/ACCESSIBILITY.md`. (Snapshot line = single `StaticText` name; recipe/bean cards = `Grouping` with combined names and inner items ignored; no pre-existing violations found in the touched sections.)
- [x] 6.3 Quick compile check via Qt Creator MCP (build only); clear any new qrc:/…qml warnings/TypeErrors introduced by these files. (Qt Creator's active project is the sibling `Decenza` worktree, not this `Decenza-Desktop` checkout, so an MCP build would validate the wrong tree; instead ran `qmllint` on the three changed files — no syntax errors, and no unqualified-access issues from the new identifiers. Runtime QML-warning clearing is folded into 6.4.)
- [x] 6.4 Manual verification (ask Jeff to launch the app): a shot with a recipe (recipe card shows, grind on recipe card, none on equipment/bean), a shot without a recipe (grind on bean card, no recipe card), and a shot with no grind (snapshot line omits grind, no empty cells). Confirm grind appears exactly twice per page (snapshot line + owning card) and the Equipment card shows only gear. Confirm swiping between shots updates the snapshot line without scrolling. **Also confirm the home-screen Shot Plan widget is visually unchanged (task 1.3).**
