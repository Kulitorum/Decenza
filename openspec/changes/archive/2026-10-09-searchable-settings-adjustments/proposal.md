# Proposal

## Why

Settings search is backed by a hand-written list, `qml/components/SettingsSearchIndex.js`, that lives apart from the cards it describes. The list has drifted. Three cards (Temperature unit, Sensor Calibration, Steam Health) were never added, so "fahrenheit" found nothing ([#2036](https://github.com/Kulitorum/Decenza/pull/2036)). Launcher Mode is offered on every platform although its card exists only on Android. Below card level almost nothing is findable: the tabs hold about 280 interactive controls, and only a handful of rows were hand-listed as results. The matcher returns hits in file order with no ranking and does not ignore accents. A separate list will keep drifting however carefully it is checked: #2036 needed a 447-line checker just to keep the card list honest.

## What Changes

- **Search info lives on the card.** A new `SettingsCard` component replaces the ~48 hand-rolled card `Rectangle`s on the settings tabs. Its search id, title, optional description, keywords and platform availability are declared once, on the card, and the card renders its own header from the same title.
- **Every adjustment on every card is its own search result.** Each interactive control on a card (switch, slider, value input, text field, combo box, button) becomes a result. Its title is taken from the translated accessible name the accessibility rules already require it to have. An optional `SettingsSearch` attached property adds keywords or overrides the title. Tapping such a result scrolls to that control and highlights its row, not the whole card.
- **Leaving something out of search fails the build.** A build-free Python scanner runs in the default desktop build and on every PR. It knows every object type that may appear on a settings tab, and an unknown type fails until someone classifies it, so a new kind of control cannot slip past. It fails when:
  - a card on a settings tab is not a `SettingsCard`;
  - a control has no title it can read;
  - two results on one card share a title;
  - a card's search fields are not literals;
  - search content sits somewhere navigation cannot reach.
- **The index is generated, not written.** The scanner harvests every card and control into a committed, generated index file. The desktop build rewrites a stale file and fails once; the PR check fails on one. The hand-written `SettingsSearchIndex.js` is deleted.
- **Availability is declared once.** A platform or build condition (Android only, simulator compiled in, debug build) is set on the card or row. It drives both whether the item is shown and whether search offers it. This replaces the dialog's one-off Simulation Mode filter and fixes Launcher Mode appearing on non-Android platforms.
- **Better matcher: Fuse.js.** Fuse.js 7.5.0 (Apache-2.0) is vendored into the QML module. Results are ranked (title over keyword over description), tolerate typos and ignore accents, so translated titles match too. English keywords and English fallback titles stay searchable in every language. Every word in the query must match, as today.
- **Out-of-settings destinations** (today only Auto-Load Profile on the profile selector) are declared on the section that hosts them, in the same way.
- **Carried over from #2036:**
  - Temperature unit, Sensor Calibration and Steam Health become searchable, with that PR's keywords.
  - Each of its checks either becomes a build-time rule or can no longer happen, because the index is derived from the cards.
  - The search-index requirement is restated: it still names a `tabIndex` field and is silent on `externalRoute`.
  - `scripts/check_settings_search_index.py` and its `text-invariants.yml` step are not kept. If #2036 merges first, this change removes them.
- **Docs:** the CLAUDE.md QML conventions and the wiki manual's Settings search entry are updated.

## Capabilities

### New Capabilities

None. Settings search is already owned by `settings-ui`.

### Modified Capabilities

- `settings-ui`:
  - **Settings Search Dialog** gains ranked fuzzy matching, accent-insensitive matching and row-level navigation.
  - **Settings Search Index** becomes an index derived from the cards. Build-time enforcement covers every card and every adjustment, and availability is declared once.

## Impact

- **QML:**
  - All 13 settings tabs under `qml/pages/settings/` (12, plus Debug): cards converted to `SettingsCard`, controls given readable titles where they lack one.
  - New `qml/components/SettingsCard.qml`.
  - `SettingsSearchDialog.qml` (matcher, result rows) and `SettingsPage.qml` (row-level scroll and highlight).
  - `ProfileSelectorPage.qml` (external route declaration).
  - `qml/components/SettingsSearchIndex.js` removed; a generated index file replaces it.
- **C++:**
  - A `SettingsSearch` attached type and a `SettingsSearchLocator` singleton that finds a result's row. The availability conditions and routes live in a QML singleton, `SettingsSearchRegistry`.
- **Build and CI:** a new `scripts/settings_search_index.py`, run as a stamped custom command in the default desktop build (`CMakeLists.txt`) and as a step in `text-invariants.yml`. `scripts/qmllint_report.py` now treats `.mjs` module files as inputs.
- **Third party:** `fuse.mjs` 7.5.0 plus its Apache-2.0 licence, listed wherever bundled third-party licences are credited.
- **Tests:** matcher ranking and generated-index loading in a QJSEngine test (the `tst_recipesearch` pattern), plus inline scanner fixtures run by its `--self-test`.
- **No user data, settings or MCP surface changes.** Translation keys are reused wherever the English matches; new keys only where a control has no existing translated name.
