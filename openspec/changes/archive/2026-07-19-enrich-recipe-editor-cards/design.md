## Context

The "Edit Recipe" page is the **summary step of the recipe wizard** (`qml/pages/RecipeWizardPage.qml`), not a standalone page. The summary layout is a `Flickable`/`ColumnLayout` (`summaryColumn`, lines ~2706–2822): a WYSIWYG hero (`RecipeDrinkCard`) followed by a responsive `GridLayout` of tappable `SummaryRow` cards (the `SummaryRow` component is defined at lines ~1560–1612). Each card's `value` string is built inline from the wizard's `f*` form-state properties (declared lines ~154–200, seeded by `applyRecipeMap` lines ~307–362).

Today those value strings are reductive:
- **Details** (lines ~2761–2780): bespoke inline logic showing only dose→yield (+ ratio suffix, + a temp-delta tag). No grind, no clear temperature.
- **Bean** (~2744–2752): `roaster + coffee` text only; no photo.
- **Profile** (~2753–2758): profile title only.
- **Steam/milk** (~2785–2798): pitcher + milk weight (this one is already reasonable).
- **Equipment** (~2817–2821): equipment package *name* only.

The data needed to enrich these already exists in the models and is largely already resolved into wizard state or reachable from it:
- Recipe (`src/history/recipestorage.h`, `struct Recipe`): dose (`doseG`), yield spec (`yieldValue`+`yieldMode`, vocabulary in `src/core/yieldspec.h`), temperature as a **signed offset** (`tempOffsetC`), grind (`grindPinned`), rpm (`rpmPinned`). Overrides are stored as deltas/specs, never absolutes.
- Bag (`src/history/coffeebagstorage.h`, `struct CoffeeBag`): `roasterName`, `coffeeName`, `roastLevel`, `roastDate`, `beanBaseId`, `beanBaseData` (JSON snapshot holding the product `link` used for the photo). Image is rendered via the shared **`BeanThumbnail`** cache widget (imageKey = Bean Base id or `bag-<id>`, link = product URL) — the same path `RecipeDrinkCard`/`RecipesPage` already use.
- Profile (`src/controllers/profilemanager.h`, `src/profile/profile.h`): `profileTargetTemperature` (`espresso_temperature`), `profileTargetWeight` (`target_weight`), `currentProfileBeverageType`, `steps[]` with per-step temps/pressure/flow. Resolved by title via `findProfileByTitle`/`getProfileByFilename`. `RecipesPage.qml` already extracts `{tempC, yieldG, stepTemps}` via `_numbersFromProfileObj` / `profileNumbersFor` (lines ~116–163).
- Equipment (`src/history/equipmentstorage.h`, flattened by `EquipmentPackageView::toVariantMap()` in `equipmentstorage.cpp` ~189–228): grinder brand/model/burrs + `rpmCapable`, basket brand/model/summary, puck-prep. The package's `lastGrindSetting`/`lastRpm` are dial-memory and are **recipe-owned data**, not equipment identity.

Constraints: QML-only styling via `Theme`; all user text internationalized (bindings over `TranslationManager.translate` / `Tr`); emoji only through `Theme.emojiToImage`/`replaceEmojiWithImg`, never plain `Text`; the summary already has the "single edit glyph per card, no double-arrow" rule that must be preserved.

## Goals / Non-Goals

**Goals:**
- Make every summary card show the full set of values its own editor sets, drink-type-scoped.
- Details card: dose, yield **with mode** (ratio shown as "1:2.0 → 36.0g", fixed weight shown plainly), temperature (effective, = profile baseline + offset), grind, and rpm only when the grinder is RPM-capable.
- Bean card: bag photo (via `BeanThumbnail`) + roaster, coffee, roast level and roast date/age.
- Profile card: profile identity + characteristics the recipe does NOT override (never restate temp/dose/yield/grind).
- Equipment card: full package (grinder brand/model/burrs, basket, puck-prep) **excluding** grind setting and rpm value.
- Keep the existing card grid, tap-to-edit routing, and single-glyph rule unchanged.

**Non-Goals:**
- No recipe/bag/profile/equipment schema changes — this only surfaces data already stored.
- No layout redesign of the wizard or its steps; no change to how editing works.
- No changes to the hero `RecipeDrinkCard` beyond what it already shows (it already renders photo + plan line).
- No new persisted settings, no BLE, no MCP surface changes.
- Not unifying the two temperature/plan formatters into one helper (out of scope; see Decisions).

## Decisions

### D0 — Guiding principle: each card reads out the recipe data its own edit action changes; no value repeats
Every card SHALL surface the maximum useful data that *editing that card* would change on the recipe — and nothing that another card already owns. The recipe is the subject: a card shows the values that flow into this recipe from the thing the card edits, rendered as they affect the recipe (effective values, e.g. temperature = profile baseline + offset). A given value appears on exactly one card:
- Recipe-overridden numbers (dose, yield/ratio, effective temperature, grind, rpm) live on **Details** — because editing "Details" is what changes them.
- Profile-owned behavior (which frames run: pressure/flow shape, beverage type) lives on **Profile** — because editing "Profile" is what changes that, and it is *not* what Details changes.
- Bag identity + photo lives on **Bean**; package identity lives on **Equipment**, excluding the dial memory (grind/rpm) that the recipe owns.

This makes each card an honest read-out of "what does changing this do to my recipe," maximizes useful data, and eliminates duplication (D4/D5 are specializations of this rule).

### D1 — Enrich in place within the wizard summary cards, don't restructure
The card `value` strings are built inline in `RecipeWizardPage.qml`. We enrich those inline builders (and, where a photo is needed, extend the `SummaryRow`/Bean card to host a `BeanThumbnail`). No new page or component split.
- **Why:** The cards, their tap-routing, and their form-state sources already live here; the change is content, not structure. A new component would fragment the summary's state wiring for no benefit.
- **Alternative considered:** Extract a reusable "recipe summary card" component. Rejected as scope creep — only the Bean card needs a non-text element, and the rest are one-line string tweaks.

### D2 — Details card shows effective temperature and grind, reusing existing resolution
The Details value gains temperature and grind. Temperature is the **effective** value (profile baseline + `tempOffsetC`); we reuse the same profile-number resolution the hero already relies on (`profileTempC` from `profileNumbersFor`/`_numbersFromProfileObj`) rather than re-deriving. Grind reads `grindPinned`; rpm reads `rpmPinned` and is shown **only** when `fEquipmentRpmCapable` is true (state already present at line ~175).
- **Why:** Keeps one source of truth for the profile baseline; avoids a second temperature formatter. The rpm-capability gate is already computed and matches the details step's own behavior.
- **Yield mode:** Show ratio explicitly when `fYieldMode === "ratio"` ("1:R → Wg"), fixed weight plainly, dose-only when no yield. This generalizes the existing inline logic rather than replacing it.
- **Alternative considered:** Route the Details card through `ShotPlanText` (the shared plan-line formatter). Rejected for this change: `ShotPlanText` is tuned for the hero's frame-temp sentence ("84 · 94°C") and fragment ordering; forcing the compact card subtitle through it risks regressing the hero and widens blast radius. The card subtitle stays a bespoke (but now complete) builder. Unifying the two formatters is noted as a future cleanup.

### D3 — Bean card renders the photo via the existing `BeanThumbnail` cache
Add a `BeanThumbnail` to the Bean card using the same imageKey/link derivation already used at `RecipeWizardPage.qml` ~2727–2729 and `RecipesPage.qml` ~367–385 (Bean Base id or `bag-<id>`; link from `beanBaseData`). Text detail (roaster, coffee, roast level, roast date/age) comes from wizard bag state; where `roastLevel`/`roastDate` aren't already in wizard state, extend `refreshBagDetails()` to carry them (they exist on `CoffeeBag`).
- **Why:** Reuses the proven cache path — no new image loading, no `.qrc` work, offline-safe. Matches the bean-step tile the spec references.
- **Emoji/asset safety:** photo is an `Image`/`BeanThumbnail`, never a colour glyph in `Text`; any decorative emoji (if used) goes through `Theme.emojiToImage`.

### D4 — Profile card is a RICH read-out of non-overridden profile characteristics, plus Info and KB affordances
The recipe overrides exactly `{temp offset, yield spec, dose, grind, rpm}`. The Profile card therefore shows profile-owned detail the recipe cannot override, and does **not** restate temperature/dose/yield/grind (those live on the Details card). Per the user's direction the card is **rich**, not a one-liner. It shows:
1. Profile **name**.
2. The profile's **editor/type classification** (Advanced, D-Flow, A-Flow, Pressure, Flow). This is the same classification the wizard header already renders ("Latte · D-Flow / Q"). Reuse that existing derivation (title-prefix membership for D-Flow/A-Flow per the project convention, editor `type` otherwise) rather than re-deriving.
3. **Beverage type** (`currentProfileBeverageType` / `beverageTypeForTitle`).
4. A **substantive pressure/flow shape summary** derived from the resolved profile object's `steps[]` — frame/step structure and its characterizing values (e.g. step count, and the shape's key pressure/flow figures where they are profile-defined and not recipe-pinned). Richer than the concise hint originally considered.
5. The two info affordances the profile step's tiles already offer, wired to the same components: the **Profile Info page "(i)" button** and, when the profile has a knowledge-base entry, the **knowledge-base sparkle ("AI DB") popup**. Both operate without leaving the summary.

- **Why:** Directly satisfies the user's ask for a rich profile summary with the type and both info buttons, while still avoiding duplication of the recipe-overridden numbers (D0).
- **Sourcing / reuse:** the "(i)" and sparkle affordances already exist on the profile selector page and the wizard's profile-step tiles (per the recipe-wizard spec); reuse those components/popups rather than building new ones. The type classification reuses the header's existing logic.
- **Single-glyph reconciliation:** the "no two glyphs with the same meaning" rule targets ambiguous duplicate navigation (the old dose→yield arrow beside a nav arrow). The Info and KB buttons are distinct, clearly-purposed, non-navigational controls and coexist with the one edit glyph — as they already do on the profile-step tiles. Lay them out so the edit glyph remains the unambiguous "open this step" affordance.
- **Alternative considered:** Show the profile's *default* temp/yield next to the recipe's overrides as a "was → now". Rejected — that belongs on the Details card's editing view, not the read-only profile summary, and would reintroduce the double-meaning the spec forbids.

### D5 — Equipment card lists package identity, excludes dial memory
Read the flattened `EquipmentPackageView` fields (grinder brand/model/burrs, basket brand/model/summary, puck-prep) and compose a multi-line/joined summary. Explicitly exclude `lastGrindSetting` and `lastRpm` (package dial memory) — those are represented by the recipe's own grind/rpm on the Details card.
- **Why:** Matches the user's instruction ("show all the equipment, exclude grind and rpm as they belong to recipe") and the model's own documentation that these two fields are dial memory, not identity.
- **Sourcing:** the equipment package is already resolved for the recipe (`requestInventory()` sets `fEquipmentName`); extend the resolution to keep the fuller `toVariantMap()` view for the card rather than only the name.

### D6 — Graceful degradation and i18n
Every enriched summary omits absent fields (no empty separators, no "undefined"), and all labels/units are internationalized via existing `Tr`/`translate` patterns. Bag-less recipes keep the "No bean" state; hot-water tea keeps "No profile" and shows volume/temperature on the numbers card instead of dose→yield.

### D7 — Unified windowed flow: the details step becomes a sub-page walk (added during implementation)
Per user direction, the single "details" step is split into dedicated windows walked after profile — **equipment → dose/yield/temp/grind → steam and/or water (only the drink's blocks) → summary** — and the SAME windows serve editing (a summary card jumps straight to its window). Implemented as a `_detailsPage` sub-state gating one section at a time inside the existing details `StackLayout` child, rather than adding new top-level StackLayout steps.
- **Why this mechanism:** it delivers real separate windows (one section visible at a time, its own Continue/back) with far less risk than restructuring the step machine and re-plumbing the creation prefill; the create-walk vs. edit-jump distinction rides the existing `_fromSummary` flag (Continue advances in the walk, returns to summary when opened from a card).
- **Navigation:** `detailsPages()` returns the ordered windows for the drink (equipment, numbers, +steam if milk, +water if water); `detailsAdvance()` steps forward, landing on the summary past the last; the back arrow steps back through them then out to profile. Summary cards set `_detailsPage` then `openStep("details")`.
- **Alternative considered:** genuine per-step StackLayout children. Rejected as higher-risk churn for no user-visible difference.

### D8 — Equipment first, and chosen via inline tiles
The equipment window is the first post-profile window (and the Equipment summary card sits above the numbers card), because the grinder's RPM capability gates the rpm field on the numbers window — the grinder must be picked before grind/rpm. The window presents the in-inventory packages as inline tap-to-select tiles (a "None" tile plus `equipmentTileModel()` = in-inventory packages, always keeping the linked one), replacing the picker-field-opens-dialog pattern; the now-unused equipment picker dialog was removed. A shared `selectEquipment(pkg)` mutator backs the tiles so selection state can't diverge.

### D9 — `ProfileManager::profileCatalogInfoForTitle()` for the rich Profile card
Added one read-only `Q_INVOKABLE` returning the scan-time catalog metadata for a title (filename, editorType, beverageType, hasKnowledgeBase, temps) so the Profile card gets the editor-type classification and KB presence in one call, without a per-render file read. Mirrors the existing `findProfileByTitle` catalog lookup; no schema/MCP impact.

## Risks / Trade-offs

- **[Card text overflow on narrow tablets / CJK-Arabic locales]** → Multi-value summaries are longer. Keep `SummaryRow.value` word-wrapping (already present), rely on content-driven sizing (never fixed widths per the bundled-font rule), and use compact separators (" · "). Verify the 1-column (<720px) layout.
- **[Profile characteristic summary is fuzzy to define]** → Scope it to a short, deterministic descriptor from `steps[]` (beverage type + a one-line shape hint) rather than an open-ended frame listing; if a profile object can't be resolved (uninstalled, only `profileJson`), fall back to title + beverage type only.
- **[Two temperature formatters diverge]** → We intentionally don't unify them here; mitigate by sourcing the Details temperature from the same `profileTempC`/offset the hero uses, so the number matches even though the formatting code differs. Note the unification as future cleanup.
- **[Bag detail not fully in wizard state]** → `roastLevel`/`roastDate` may need to be threaded through `refreshBagDetails()`. Low risk: fields exist on `CoffeeBag`; this is additive read-only wiring.
- **[Photo cache miss / no link]** → `BeanThumbnail` already handles missing images with a placeholder; a bag with no `beanBaseData` link shows the same fallback the hero/list already show. No new failure mode.

## Migration Plan

Pure additive UI enrichment — no data migration, no persistence or protocol change. Ships in the normal build. Rollback is a straight revert of the QML (and any small `refreshBagDetails()`/equipment-resolution additions); no stored data is affected. Update the recipe-wizard / Edit Recipe wiki manual page to show the enriched summary as part of this change.

## Open Questions

- Profile card: RESOLVED — rich; name + editor/type classification + beverage type + shape summary (frame count · peak bar · max ml/s) + "(i)" and sparkle buttons.
- Equipment card: RESOLVED — the shared `EquipmentSummary` component (grinder title/burrs, basket line, puck-prep line), dial line unfed.
- Flow shape: shows frame count plus peak pressure / max flow across steps; adequate for a card. Revisit if a richer profile descriptor is wanted later.
- Deferred: the two temperature formatters (card builder vs. `ShotPlanText`) remain separate — unification is a future cleanup, not part of this change.
