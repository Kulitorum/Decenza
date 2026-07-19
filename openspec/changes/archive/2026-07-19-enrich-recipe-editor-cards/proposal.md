## Why

The recipe wizard's summary page (the "Edit Recipe" surface) was mostly empty, and its editing flow was uneven. Each component card showed either nothing useful or a single reductive line: the Details card read only "18.0g → 36.0g" even when the recipe also pinned temperature, grind, ratio, and yield mode; the Bean card showed only a name with no photo; the Profile card showed only the profile name; the Steam/milk and Equipment cards showed only a title. And every card — Details, Steam, Hot water, Equipment — opened the *same* monolithic "details" step, so it was never clear where to go to change a given value, and grind/rpm could be edited before the grinder that gates them was chosen. The summary should be a legible at-a-glance record of the whole recipe, and each card should lead to a dedicated editor that is identical whether creating or editing.

## What Changes

**Enriched summary cards**
- **Dose, yield, temp & grind card** — show every value that window sets, not just dose→yield: dose, yield with its mode (fixed weight vs. ratio, e.g. "18.0g → 36.0g (1:2.0)"), effective temperature (unit-aware, offset tagged), and grind (with rpm only when the grinder is RPM-controlled). Drink-type-scoped: tea has no grind; hot-water tea shows volume + temperature. The card title names grind for coffee drinks.
- **Bean card** — add the bag photo (via the existing `BeanThumbnail` cache) and richer detail: coffee, roaster, roast level, roast age.
- **Profile card** — a RICH read-out of profile detail *other than* the params the recipe overrides: name + editor/type classification (D-Flow, A-Flow, Pressure, Flow, Advanced) + notable beverage type + a pressure/flow shape summary, plus the "(i)" Profile Info and sparkle knowledge-base buttons. It does not restate the recipe-overridden temp/dose/yield/grind.
- **Equipment card** — the full package (grinder, basket, puck-prep) via the shared `EquipmentSummary`, **excluding grind setting and rpm**, which are recipe-owned and shown on the Dose/yield/temp/grind card.
- Guiding rule (**D0**): each card reads out the recipe data its own editor changes, and no value repeats across cards — so it is obvious where to edit each thing.

**Unified, windowed flow (create == edit)**
- The one "details" step is broken into a sequence of dedicated windows walked after profile: **Equipment → Dose/yield/temp/grind → Steam and/or Water (only the blocks the drink has) → Summary**.
- **Equipment comes first** (and its summary card sits above Dose/yield/temp) so the grinder is chosen before the grind/rpm it gates.
- The same windows serve both paths: **create** walks them; **edit** jumps straight to a window from the tapped summary card and returns to the summary. One flow, one set of screens.
- The equipment window presents in-inventory packages as inline **tap-to-select tiles** (plus "None"), replacing the picker-field-opens-dialog pattern.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `recipe-wizard`: Two requirements change. (1) **"Drink-type-first step sequence"** / details step — the post-profile "details" step becomes a sequence of dedicated windows (equipment → dose/yield/temp/grind → steam/water as needed), equipment first, walked on create and jumped-to on edit. (2) **"Summary page is the edit surface"** — each card's value summary presents the full set of values its editor sets (no repetition across cards); cards are ordered Equipment before Dose/yield/temp/grind; each card opens its dedicated window; equipment is chosen via inline tiles.

## Impact

- **QML**: `qml/pages/RecipeWizardPage.qml` — the step machine (new `_detailsPage` sub-window walk), the summary cards' enriched value builders, the `SummaryRow` component (thumbnail / header-action / content slots), the equipment window's inline tiles, and card ordering/routing.
- **C++**: `ProfileManager::profileCatalogInfoForTitle()` added (read-only catalog metadata: editorType/beverageType/hasKnowledgeBase/temps by title) to feed the rich Profile card. No schema, BLE, persistence, or MCP changes.
- **Data models (read-only)**: recipe, bag (photo/roast fields), profile (catalog metadata + resolved object), equipment package (`EquipmentPackageView` fields minus dial memory) — all surfaced from data the models already hold.
- **Image rendering**: bag photo via `BeanThumbnail`; no plain-`Text` emoji.
- **Wiki manual**: the recipe wizard / Edit Recipe manual page should describe the windowed flow and enriched cards.
