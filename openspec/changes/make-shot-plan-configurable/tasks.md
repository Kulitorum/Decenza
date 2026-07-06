# Tasks — Make the Shot Plan Widget Configurable

## 1. Groundwork & de-risking

- [x] 1.1 Verify the item-property mechanism round-trips a JSON array: `setItemProperty(itemId, "shotPlanItems", [...])` → layout JSON → `getItemProperties` → QML, and the web `/api/layout/item` POST path; extend to pass arrays through if scalar-only (design D4 fallback: comma-joined string) — VERIFIED, no changes needed: QJsonValue::fromVariant/toVariant pass arrays through on both the QML and web POST paths
- [x] 1.2 Verify `Text.StyledText` + `wrapMode: Text.Wrap` + `maximumLineCount: 2` + `elide: Text.ElideRight` renders correctly with `<b>` spans (desktop + one mobile platform); if elide fails with StyledText, note the TextMetrics-based degrade from design D3 — VERIFIED on macOS with the qml runtime: lineCount=2, truncated=true, short text unaffected; mobile visual check folded into task 6.2

## 2. ShotPlanText rendering (ordered items + sentence toggle + wrap)

- [x] 2.1 Replace the six `show*` booleans on `ShotPlanText.qml` with `itemOrder` (string list) and `sentence` (bool) properties; keep the per-item segment formatters (dose-in, grind+rpm, roast date, yield-override arrow, temperature-override highlight) unchanged
- [x] 2.2 Implement fragment format: present items joined with the standard separator in `itemOrder` order (single shared `_build(fmt, sep)` still produces plain + rich)
- [x] 2.3 Implement sentence format: scaffold consumes `doseYield`/`profile`/`temperature` + beverage word; remaining items trail in `itemOrder` order; degrade table per design D2 (no-temperature variants, no-profile → fragment fallback); cleaning/descale warning unconditional in both formats
- [x] 2.4 Add the two new temperature-less sentence translation keys (`shotplan.sentenceNoTemp`, `shotplan.sentenceNoYieldNoTemp`) with English fallbacks (TranslationManager uses dynamic string discovery — no catalog registration required)
- [x] 2.5 Fix overflow: width-bind the text (icon-aware), `wrapMode: Text.Wrap`, `maximumLineCount: 2` (1 in compact), `elide: Text.ElideRight`; natural implicit size computed from `planText.implicitWidth` (NOT `row.implicitWidth` — Row positioners report actual child widths, which would collapse the binding chain); `Layout.maximumWidth` caps added for `shotPlan` in LayoutCenterZone + LayoutBarZone since RowLayout does not shrink items below implicit width (verified in a qml harness: capped delegate → 2 lines, truncated, height grows)

## 3. ShotPlanItem wiring & migration

- [x] 3.1 Add the single legacy-derivation function in `ShotPlanItem.qml`: use `modelData.shotPlanItems` when a non-empty array, else derive from `shotPlanShow*` booleans in canonical order with `shotPlanShowProfile` expanding to `profile` + `temperature`; `shotPlanSentence` absent → true
- [x] 3.2 Pass `itemOrder`/`sentence` to both ShotPlanText instances (compact maxLines 1, full maxLines 2) with width clamped to the granted zone width; a11y name plumbing unchanged (`text` API preserved)
- [x] 3.3 Confirm `shotPlanShowSteamPlan` behavior is untouched (steam swap + steam plan text unchanged — SteamPlanText and `_steamMode` not modified)

## 4. In-app editor (ScreensaverEditorPopup)

- [x] 4.1 Build the chip-bar UI for `itemType === "shotPlan"`: "Shown" Flow of chips (drag reorder via DelegateModel live-swap + move transition, per-chip remove) and "Available" row (activate to append), editing a working copy (shared derivation extracted to `qml/components/layout/ShotPlanConfig.js`, registered in CMakeLists)
- [x] 4.2 Add the "Sentence style" toggle alongside the existing "Steam plan (while steaming)" toggle
- [x] 4.3 Add the live preview line (read-only ShotPlanText bound to the working state)
- [x] 4.4 Accessible reorder fallback: per-chip move-left/right controls when a screen reader is active (mirror LayoutEditorZone's pattern); full Accessible roles/names on chips and rows
- [x] 4.5 Widen the dialog for `shotPlan` (560 scaled; working-copy Save writes `shotPlanItems` + `shotPlanSentence` + `shotPlanShowSteamPlan` only; Cancel discards; legacy display booleans never written)
- [x] 4.6 New translation keys for "Shown", "Available", "Sentence style", chip labels (`shotPlanEditor.itemProfile`/`itemTemperature` new; roaster/coffee/grind/roastDate/doseYield reuse existing), and a11y announcements — all with English fallbacks, dynamic discovery

## 5. Web layout editor parity (shotserver_layout.cpp)

- [x] 5.1 Replace the six shot-plan display checkboxes with an ordered item list UI (up/down reorder + add/remove `spShownList`/`spAvailableList`, aria-labels) plus Sentence style and Steam plan checkboxes; also fixed pre-existing missing `shotPlan` entry in SS_TITLES
- [x] 5.2 Load path: prefer `shotPlanItems`, else derive from legacy booleans (same rule incl. profile → profile + temperature expansion; `spItemsFromProps` mirrors ShotPlanConfig.js with a keep-in-sync note)
- [x] 5.3 Save path: POST only the new keys (`shotPlanItems` array, `shotPlanSentence`, `shotPlanShowSteamPlan`) via the existing auto-save debounce; round-trip verified structurally (both editors read/write the same keys; array pass-through confirmed in task 1.1) — live cross-editor check folded into 6.3

## 6. Verification & docs

- [ ] 6.1 Manual pass on desktop: defaults render identically to pre-change; reorder, remove, sentence-off, cancel, two instances with different configs
- [ ] 6.2 Manual pass on a tablet-sized window: long plan (all chips + long bean name) wraps to two lines and elides instead of clipping
- [ ] 6.3 Legacy-layout check: a layout saved with old `shotPlanShow*` keys (incl. Roaster/Grind off) renders and opens in both editors correctly with no writes until Save
- [x] 6.4 Run the test suite via Qt Creator MCP (build succeeded; 2561 passed, 0 failed, 0 warning-emitting); the plain-text builder lives in QML and has no existing unit coverage — nothing to adjust
- [x] 6.5 Checked `docs/CLAUDE_MD/*` and the wiki Manual: no existing references to the shot-plan option keys or a Shot Plan page — nothing to update

## 7. Review fixes (post-PR automated review)

- [x] 7.1 Empty item list is now persistable: both derivation implementations (`ShotPlanConfig.js`, web `spItemsFromProps`) gate on array PRESENCE, not non-emptiness — saving "show nothing" no longer silently resurrects the defaults from legacy booleans
- [x] 7.2 Shown chip container a11y role corrected to StaticText (its actions are the child move/remove buttons; it had Button role with no press action)
- [x] 7.3 Comment accuracy pass: doseYield consumed-vs-trailing nuance and profile-anchor fallback in the ShotPlanText header + editor toggle comment; `_planDragging` purpose; "six legacy item booleans" phrasing vs the live steam key; sync-note implementation count; provenance and temporal wording in ShotPlanConfig.js
- [x] 7.4 Round-2 review fixes: `setItemProperty`/`setItemPropertyList` return bool + warn on stale itemId or unstorable/invalid value (JS undefined); web endpoint 404s failed writes + unknown-id GET, 400s missing value; popup save() logs failure; `itemsFor` rejects malformed (string/object) stored lists with a warning and takes the same legacy branch as the web side; web unknown-key labels render the escaped raw key; regression test hardened (non-canonical order, stale-id false, invalid-value refusal with prior value intact); null-recovery comment nuance both sides
- [x] 7.5 Round-2 cleared without change: CustomEditorPopup `segments` verified safe (QVariantList from C++ surfaces as a Sequence and round-trips typed — only JS-constructed arrays wrap as QJSValue, confirmed against Qt 6.11 qv4engine.cpp); web partial-save already toasts via the shared apiPost error handler and self-heals on the next autosave
