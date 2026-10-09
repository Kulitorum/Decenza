# Tasks

## 1. Spikes (prove the two unknowns before any migration)

- [x] 1.1 Vendor `fuse.mjs` 7.5.0 with its Apache-2.0 LICENSE under `qml/third_party/fuse/` (via `scripts/vendor_fusejs.py`, which rewrites the five ES2018 object-spread sites QV4 cannot parse) and add it to the QML module. Verified in QV4 with the stock `qml` tool on a scratch `.mjs`: "farenheit" and "celcius" find Temperature unit, accents fold, token search ANDs words once given a custom `tokenize` (QV4's regex has no `\p{L}`). The formal QJSEngine test is 5.2.
- [x] 1.2 Spike a QQmlSA qmllint plugin. Result: built and loaded only by an ad-hoc re-signed `qmllint`; the stock, Qt-signed binary refuses it on macOS (hardened runtime, different Team ID). Decision: drop the plugin, enforce with a Python scanner, keep the stock `qmllint` (design D2).

## 2. Snapshot today's search behaviour

- [x] 2.1 Export every `(title, cardId)` and `(keyword, cardId)` pair from the current `SettingsSearchIndex.js` into `tests/data/settings_search_snapshot.json`. Add #2036's three entries (temperatureUnit, sensorCalibration, steamHealth with their keywords). Verify the file lists 56 entries plus the external Auto-Load Profile route.

## 3. Building blocks

- [x] 3.1 Add the `SettingsSearch` attached type (`title`, `description`, `keywords`, `availability`, `route`) in C++, registered by `QML_ATTACHED` macro (no `qmlRegister*`, per QML_GOTCHAS.md). Add the `SettingsAvailability` table (`android`, `simulator`, `debug`). Verify with a clean build and qmllint gate pass.
- [x] 3.2 Add `qml/components/SettingsCard.qml` (required `searchId`, `title`; optional `description`, `keywords`, `availability`, `showHeader`) reproducing the existing card margins, radius and header style. Register it in `CMakeLists.txt`. Verify by converting one card (Temperature unit) and comparing before/after screenshots on desktop.
- [x] 3.3 Add the external-route table shared by `SettingsPage` and the scanner (today: `profileSelector`). Verify `SettingsPage` routes through it.

## 4. Scanner: enforcement and harvest

- [x] 4.1 Write `scripts/settings_search_index.py` with its QML tokenizer (strings, comments, JS blocks, object declarations; fails on what it cannot place) and the closed-world type table (adjustment, composite, view, overlay, structural). Implement the D2 rules:
  - unknown type names on a tab are errors;
  - literal card fields;
  - adjustments, composites and views need titles; overlay and delegate subtrees skipped; a clicked raw `MouseArea` is an adjustment;
  - duplicate titles per card;
  - a card-styled `Rectangle` or a stray adjustment outside a card on a tab file.

  Tab files come from the `tabs` literal in `SettingsTabs.qml`. Rules apply only to files containing `SettingsCard` until task 6.2.

  Verify each rule with an inline `--self-test` fixture that fails without the rule and passes with it.
- [x] 4.2 Implement D3 title resolution (`SettingsSearch.title` > `accessibleName` > `Accessible.name` > `text` > `title` > `label`; literal `translate()` prefix or `Tr` id). Report an error saying to add `SettingsSearch.title` when unresolvable. Verify with fixtures for each accepted form and two rejected forms.
- [x] 4.3 Generate the index per D5: scan tabs in tab-table order, validate routes and file placement, render `qml/components/SettingsSearchEntries.js`. Default mode rewrites a stale file and exits non-zero; `--check` only compares. Run it as a stamped custom command in the default desktop build and as a `text-invariants.yml` step (`--self-test`, then `--check`). Verify by building once (fails, file written), building again (passes), and confirming a hand edit to the generated file fails both the next build and `--check`.

## 5. Matcher and navigation

- [x] 5.1 Add `SettingsSearchMatcher.mjs` per D7: Fuse built once per language change, the weights, `ignoreDiacritics`/`ignoreLocation`, per-word AND with summed score, exact-substring rule for words of three characters or fewer. Read entries from the generated index and filter through `SettingsAvailability`.
- [x] 5.2 Add settings-search tests to `tests/tst_recipesearch.cpp` (QJSEngine, already evaluates shipping search JS) asserting:
  - every snapshot pair from 2.1 still finds its card;
  - the spec scenarios: "farenheit"/"celcius", the accented French title, title outranks keyword, German + "bluetooth", "launcher" absent off Android, "simulation" absent without simulator.

  Break the weighting and confirm the test goes red. Run via `mcp__qtcreator__run_tests`.
- [x] 5.3 Switch `SettingsSearchDialog.qml` to the matcher. Result rows show title, card title and tab badge, with Accessible role, name and focusable kept. Remove the Levenshtein code and the `simulationMode` special case. Verify in the running app (user starts it) that the dialog lists ranked results.
- [x] 5.4 Add row-level navigation to `SettingsPage.qml` per D4. Fall back to highlighting the card with a `WARN` through the registered logging helper (read `docs/CLAUDE_MD/LOGGING.md` first). Verify in the running app that a control result scrolls to and highlights its row, and that `check_log_markers.py` passes.

## 6. Migrate the tabs (one commit per tab, screenshots before and after, open every converted tab)

- [ ] 6.1 Convert every card to `SettingsCard`, moving each card's old keywords onto it, and give each adjustment a resolvable, card-unique title. Fix pre-existing accessibility gaps the scanner surfaces: raw `MouseArea` becomes `AccessibleMouseArea`, and dynamic names get `SettingsSearch.title`. Platform-conditional cards and rows move to `availability`. Each tab is verified by a green gate, the snapshot test, and opening the tab in the running app. Tabs:
  - [x] Connections
  - [x] Machine (includes Temperature unit, Launcher Mode `android`, Simulation Mode `simulator`)
  - [x] Calibration (includes Sensor Calibration, Steam Health)
  - [ ] History & Data
  - [x] Themes
  - [x] Layout (root card, `showHeader: false`)
  - [x] Screensaver
  - [ ] Shot Upload
  - [ ] AI
  - [ ] MQTT
  - [ ] Language & Access
  - [ ] About (incl. firmware card)
  - [ ] Debug (`availability: "debug"`)
- [ ] 6.2 Declare the Auto-Load Profile section on `ProfileSelectorPage.qml` with `SettingsSearch.route: "profileSelector"`. Make the scanner's rules unconditional for all tab files and delete `qml/components/SettingsSearchIndex.js`. Verify:
  - the gate passes on the whole tree;
  - `grep -r SettingsSearchIndex.js qml src` is empty;
  - adding a bare `Rectangle { color: Theme.cardBackgroundColor }` with a `StyledSwitch` to any tab fails the build. Revert after checking.
- [ ] 6.3 If #2036 has merged: delete `scripts/check_settings_search_index.py` and its `text-invariants.yml` step, and confirm the workflow header no longer mentions it. If it has not: comment on #2036 that this change carries its three entries and checks. Verify with the PR state via `gh pr view 2036`.

## 7. Documentation

- [ ] 7.1 CLAUDE.md QML conventions: one short bullet. A settings card is a `SettingsCard`, controls need a readable title, the build enforces both, and the generated index is committed. Put the detail in `docs/CLAUDE_MD/QML_GOTCHAS.md` next to the qmllint gate section, including how to read a scanner error and how to classify a new type. Verify that both files reference `scripts/settings_search_index.py`.
- [ ] 7.2 Add Fuse.js to wherever bundled third-party licences are credited (alongside Noto Sans Math). Verify the licence text ships in the app bundle.
- [ ] 7.3 Wiki manual (Kulitorum/Decenza.wiki): shorten the Settings search entry to say any setting, including individual switches, can be found by name or synonym, and the result jumps to it. Cut by half before committing. Hold the push for release unless told otherwise.

## 8. Integration

- [ ] 8.1 Full suite via `mcp__qtcreator__run_tests` (scope `all`) and a clean desktop build with the gate passing on the whole tree.
- [ ] 8.2 Manual pass in the running app (user starts it), in English and one accented language:
  - every spec scenario;
  - the old index's top queries (wake, power, scale, backup, mqtt, firmware);
  - results for three controls inside multi-control cards land on the right row.

## Workflow follow-up

- Open the PR, then run `/pr-review-toolkit:review-pr` before merge (waivers are Jeff's call).
- Archive with `openspec archive searchable-settings-adjustments --yes` as the PR's final commit; read the `text-invariants` run for that commit before merging.
