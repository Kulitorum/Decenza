## 1. State plumbing (read-only, additive)

- [x] 1.1 In `RecipeWizardPage.qml`, confirm/extend `refreshBagDetails()` and the bag state (`f*`) so the Bean card has `roastLevel` and `roastDate` in addition to `fRoaster`/`fCoffee`/`fBagId`/`fBeanBaseId`/`fBagBlob` (fields exist on `CoffeeBag`). — added `_selectedBagRoastDate`, set in both inventory reply and direct-select paths.
- [x] 1.2 Extend the equipment resolution in `requestInventory()` so the summary can read the full `EquipmentPackageView.toVariantMap()` fields (grinder brand/model/burrs, `rpmCapable`, basket brand/model/summary, puck-prep) for the linked package — not just `fEquipmentName`. Do NOT surface `lastGrindSetting`/`lastRpm`. — added `_selectedPackage`.
- [x] 1.3 Confirm the profile resolution is available to the cards: added `ProfileManager::profileCatalogInfoForTitle()` (editorType/beverageType/hasKnowledgeBase/temps by title) and a QML `resolveProfileObj()` helper; effective temp reuses `fProfileTempC` + `fTempDeltaC`.

## 2. Details card — show all recipe-owned values (D0/D2)

- [x] 2.1 Rewrote the Details value via `summaryDetailsValue()`: dose; yield WITH mode (ratio → "1:R → Wg"; fixed → "Wg"; dose-only); effective temperature (unit-aware `Theme.formatTemperature`, offset tagged); grind.
- [x] 2.2 Show rpm alongside grind ONLY when `fEquipmentRpmCapable` is true and an rpm is pinned.
- [x] 2.3 Tea scoping: portafilter tea shows leaf dose / yield / temperature and NO grind/rpm (gated by `activeTemplate.grind`); hot-water tea shows volume + temperature.
- [x] 2.4 Absent fields omitted cleanly (parts array joined by " · "; em dash only when nothing is set).

## 3. Bean card — photo + bag detail (D3)

- [x] 3.1 Added a `BeanThumbnail` to the Bean card via SummaryRow's new thumbnail slot, keyed Bean-Base-id-or-`bag-<id>` with link from `beanBaseData` (`bagImageLink()`).
- [x] 3.2 Enriched the Bean text (`summaryBeanValue()`): coffee name + roaster · roast level · roast age; kept "No bean"/"No tea" empty states.
- [ ] 3.3 Verify the layout in both 2-column (≥720px) and 1-column (<720px) grids at runtime; photo must not push the value text off-card. (Implemented; runtime check pending — folded into 7.3.)

## 4. Profile card — RICH, non-overridden characteristics + Info/KB buttons (D0/D4)

- [x] 4.1 Rich `summaryProfileValue()`: name + editor/type classification (`profileEditorLabel`: D-Flow / A-Flow / Pressure / Flow / Advanced) + notable beverage type + pressure/flow shape (`summaryProfileShape()`: frame count, peak pressure, max flow).
- [x] 4.2 Added the Profile Info "(i)" button (reusing `ProfileInfoButton` → `ProfileInfoPage`) in the card header via the new `headerActions` slot.
- [x] 4.3 Added the KB sparkle button (reusing `wizardKnowledgeDialog.openFor`); visible only when `profileCatalogInfoForTitle().hasKnowledgeBase`.
- [x] 4.4 Info/KB controls sit in the header left of the edit glyph; the card-wide select target is `z:-1` so their taps win (the profile-tile pattern) — distinct, non-colliding controls.
- [x] 4.5 Profile card does NOT restate temp/dose/yield/grind; falls back to title (+ any resolvable classification) when the profile object is unresolvable.
- [x] 4.6 "No profile"/hot-water: the Profile card stays hidden for hot-water tea (`visible: !isHotWaterTea`).

## 5. Equipment card — full package minus dial memory (D0/D5)

- [x] 5.1 Equipment card now renders the shared `EquipmentSummary` (grinder brand/model/burrs, basket, puck-prep) via SummaryRow's `contentComponent`; falls back to the plain name / "None" until inventory resolves.
- [x] 5.2 Grind/rpm deliberately NOT fed to `EquipmentSummary` (dial line stays empty), mirroring the inventory `EquipmentCard`; "None" kept when no package is linked.

## 6. i18n, polish, and consistency

- [x] 6.1 All new labels/units routed through `TranslationManager.translate` with English fallbacks; reused `equipment.card.lastRpm`. No hardcoded visible English.
- [x] 6.2 Single edit glyph per card preserved; tap-to-edit `step` routing intact on every card; no second navigational arrow (D0 — value lives on the card whose editor changes it, so it is obvious where to edit each thing).
- [x] 6.3 Hero `RecipeDrinkCard` untouched; the Details effective temperature uses the same `fProfileTempC` + `fTempDeltaC` the hero renders from.

## 6b. Windowed flow — unify create & edit (added mid-implementation)

- [x] 6b.1 Split the monolithic "details" step into a sub-page walk (`_detailsPage`): equipment → dose/yield/temp/grind → steam/water (only the drink's blocks) → summary, via `detailsPages()`/`detailsAdvance()`.
- [x] 6b.2 Equipment window first; Equipment summary card moved above the Dose/yield/temp/grind card (grinder before the grind/rpm it gates).
- [x] 6b.3 Each summary card opens its own window (`detailsPage` on `SummaryRow`); create walks the windows, edit jumps to one and returns — same screens both ways.
- [x] 6b.4 Back-navigation steps window-by-window then out to profile; hot-water-tea back path preserved.
- [x] 6b.5 Header button is context-aware: Continue/Review in the walk, Done when opened from the summary; Cancel/Save in edit sessions unchanged.
- [x] 6b.6 Equipment window uses inline tap-to-select tiles (`equipmentTileModel()` + shared `selectEquipment()`); removed the now-unused equipment picker dialog.
- [x] 6b.7 Dose/yield/temp/grind summary-card title names grind for coffee drinks; tea title omits it.

## 7. Verify

- [x] 7.0 Build via Qt Creator (Decenza checkout): succeeded, 0 errors, 0 warnings.
- [x] 7.1 Ran the built app across drink types; user reviewed the flow and cards ("looks good").
- [x] 7.2 D0 verified in the running UI — no value repeats across cards; each card opens the window that changes it.
- [x] 7.3 Layout: single-column full-width windows (Fix #1) + content-driven sizing keep windows/cards from clipping; verified on the desktop build.
- [x] 7.4 Profile card type classification + (i)/KB buttons exercised in the running app.

## 8. Docs

- [x] 8.1 Updated the recipe-wizard section of the GitHub wiki Manual to describe the per-window flow (equipment → numbers → steam/water → summary) and the enriched summary cards; pushed to the wiki repo.
