# Design

## Context

See proposal.md for the motivation. These are the constraints that shape the approach:

- **Tabs load lazily.** Settings tabs are `Loader`s that instantiate on first visit (`SettingsPage.qml:245`). An index read from live objects at runtime would be missing every tab the user has not opened, so the index has to be produced statically.
- **Navigation is by name.** `SettingsPage.findChildByObjectName()` locates a card by `objectName` after its tab loads. Nothing currently identifies a control inside a card.
- **What is there today.** About 48 cards are hand-rolled `Rectangle { color: Theme.cardBackgroundColor; radius: Theme.cardRadius }` with an `objectName`. The tabs (~15.7k lines, debug tab excluded) hold roughly:
  - 124 `AccessibleButton`, 48 `StyledSwitch`, 20 `ValueInput`, 19 `StyledTextField`, 18 `AccessibleMouseArea`, 17 raw `MouseArea`, 11 `StyledComboBox`;
  - 13 `Repeater`s and 31 dialogs.
- **Names are mostly readable already.** Of 275 accessible-name bindings, 190 are a single literal `TranslationManager.translate("key", "fallback")` call.
- **Where checks run.** The default *desktop* build already runs build-free Python checks as stamped custom commands (the QML diagnostics gate, `scripts/qmllint_report.py`, with the stock `qmllint`). `text-invariants.yml` runs build-free Python checks on every PR whose paths touch `qml/**` or `src/**`.
- **A qmllint plugin cannot run on macOS.** Qt 6.12's public QQmlSA API would give type-resolved analysis, and a plugin was built and spiked. The Qt installer signs `qmllint` with the hardened runtime, so macOS refuses any plugin not signed by The Qt Company's team ("mapping process and mapped file (non-platform) have different Team IDs"). Running it would need a re-signed copy of `qmllint`, which was declined: the gate runs the stock binary.

## Goals / Non-Goals

**Goals:**
- One declaration per searchable thing, written where the thing is defined.
- A setting added without search info fails the desktop build with file and line.
- Results down to individual controls, with row-level highlight.
- Ranked, typo-tolerant, accent-insensitive matching.

**Non-Goals:**
- Searching content inside dialogs or popups. Each is reached through the control that opens it, and that control is the result.
- Searching dynamic list contents (individual BLE devices, themes, languages). The list as a whole is one result.
- Searching pages outside Settings beyond the existing Auto-Load Profile route.
- Translating keywords. They stay English synonyms, searched in every language, as today.
- Running enforcement on Android or iOS builds. Those builds consume the committed index.

## Decisions

### D1. `SettingsCard` is the only way to put a card on a settings tab

`qml/components/SettingsCard.qml` owns the card chrome (background, radius, padding) and the header text. It declares:

| Property | Required | Notes |
|---|---|---|
| `searchId` | yes | Becomes `objectName`. String literal. |
| `title` | yes | Literal `translate()` call. Rendered as the card header and used as the search title. |
| `description` | no | Literal `translate()` call. |
| `keywords` | no | Array of string literals. |
| `availability` | no | See D6. |
| `showHeader` | no | For the few cards whose content draws its own header (e.g. the Layout tab root). |

Required properties make a missing title a qmllint error, and the scanner (D2) rejects a card-styled `Rectangle` on a tab.

This also removes ~48 copies of the card styling, as CLAUDE.md's centralize rule asks.

*Alternative considered:* a `SettingsSearch` attached property on the existing `Rectangle`s. That needs no visual migration, but the card title would then be written twice, once in the header `Text` and once in the attached title. Those two copies drifting is exactly the defect class this change removes.

### D2. Enforcement and harvest are one Python scanner, failing closed on unknown types

`scripts/settings_search_index.py` is pure Python and build-free. It parses each settings tab file (from the tab table, D5) and each declared external host into an object tree. Its small QML tokenizer:
- understands strings (all three quote forms), comments, and JS blocks;
- tells an object declaration (`Type {`, `prop: Type {`, `Module.Type {`) from a JS block or object literal;
- **fails rather than skips** on anything it cannot place, such as unbalanced braces.

**No type information, so classification is closed-world.** One table in the script assigns every object type name that may appear on a settings tab to exactly one class:

| Class | Meaning | Examples |
|---|---|---|
| adjustment | needs its own search title | `StyledSwitch`, `AccessibleButton`, `ValueInput`, `StyledTextField`, `StyledComboBox`, `AccessibleMouseArea`, `StyledIconButton`, `TextArea` |
| composite | a project component holding adjustments; one result for the whole thing, title from the instance | `SettingsActionRow`, `UploadDestinationCard`, `UploadAccountSection` |
| view | its delegates are dynamic; the view needs one title and its delegate subtree is skipped | `Repeater`, `ListView`, `GridView` |
| overlay | content lives on the overlay, reached through the control that opens it; subtree skipped | `DecenzaDialog`, `Popup`, `Menu`, `DatePickerDialog` |
| structural | layout and display only | `ColumnLayout`, `RowLayout`, `Item`, `Text`, `Tr`, `Image`, `Connections`, `Timer` |

A type name on a tab that is in no class is an error naming the file, line and type. Adding a new control type therefore fails the check until someone decides what it is. A raw `MouseArea` with a click handler counts as an adjustment, which doubles as an accessibility catch: it must become an `AccessibleMouseArea` with a name.

For every `SettingsCard` the scanner then:
- checks `searchId`, `title`, `description`, `keywords` and `availability` are literals, and extracts them;
- requires a title (D3) for every adjustment, composite and view inside it;
- rejects duplicate titles within the card (case-insensitive).

Outside any `SettingsCard` on a tab file, it reports:
- a card-styled `Rectangle`, i.e. one whose `color` reads `Theme.cardBackgroundColor`, which is how every hand-rolled card is written today;
- any adjustment, composite or view.

**Where it runs:**
- in the default desktop build, as a stamped custom command beside the QML diagnostics gate, so the developer who writes the gap sees it;
- in `text-invariants.yml` on every PR (`--self-test`, then `--check`), so a change made without a desktop build is still caught.

*Alternatives considered:*
- **A qmllint plugin (QQmlSA).** It is type-resolved, but it cannot load into the stock `qmllint` on macOS (see Context).
- **`qmldom --dump`.** It gives an AST but no type resolution, and its JSON format is undocumented and internal.
- **Harvesting at runtime.** Ruled out by lazy tabs (see Context).

### D3. An adjustment's title comes from what it already says

The scanner takes the first of these that is present on the element:
1. `SettingsSearch.title`
2. `accessibleName`
3. `Accessible.name`
4. `text`
5. `title`
6. `label`

The chosen binding must either:
- start with a literal `TranslationManager.translate("key", "fallback")` call (a trailing `+ ": " + value` is allowed; the title is the translated prefix), or
- be `<id>.text` where `<id>` is a `Tr` element with literal `key`/`fallback`.

Anything else, such as a binding to a JS variable or a model role, is an error whose message says to add `SettingsSearch.title`.

`SettingsSearch` is a small C++ `QML_ATTACHED` type with properties `title`, `description`, `keywords`, `availability` and `route`. It holds no behaviour; it exists so the declaration is type-checked by qmllint and readable at runtime by navigation (D4). It is registered by macro, per CLAUDE.md, never `qmlRegister*`.

Reusing the accessible name means the common case needs no new text. Where a control's name is dynamic, the fix usually improves screen-reader output as well.

### D4. Results navigate to the control by its translated title

Each generated entry carries `tabId`, `cardId`, `key`, `fallback`, `keywords`, `availability`, `kind` (card or adjustment) and the card's title key.

After the tab loads and the card is found by `objectName` (existing code), `SettingsPage` looks for the adjustment. It walks the card's subtree, skipping invisible branches, for the first item whose `SettingsSearch.title`, `accessibleName` or `Accessible.name` starts with `translate(key, fallback)` in the current language. It scrolls that item into view and highlights its nearest row: the closest ancestor that is a direct child of a layout inside the card.

If the item is not found or not visible (for example, a row shown only when a toggle is on), it highlights the card and logs at `WARN` through the registered logging helper (`LOGGING.md`). That is a data-quality fault the scanner should have caught.

*Alternative considered:* giving every adjustment a generated `objectName`. That adds a second identifier per control to keep in sync, while the title is unique per card by D2 already.

### D5. The index is a committed generated file

The scanner:
1. Reads the tab table: the `tabs` array literal in `SettingsTabs.qml`. It fails loudly if the array stops being a literal.
2. Scans the tab files in that order.
3. Renders `qml/components/SettingsSearchIndex.generated.js`. It holds one `getSearchEntries(tr)` returning entries whose title is `tr(key, fallback)`, so the translation tooling still sees every key.
4. Compares the result with the committed file:
   - in the desktop build, a difference rewrites the file and fails once with "settings search index regenerated, commit it", and the next build passes;
   - with `--check` (CI), a difference only fails; nothing is written.

**Where things may appear:**
- A `SettingsCard` in a file that is neither a tab source nor a declared external host is an error.
- External routes: a section outside Settings declares `SettingsSearch.route: "profileSelector"` on its root, and the scanner accepts only routes `SettingsPage` handles. The list of routes is one table read by both the page and the scanner.
- Cards on `debugOnly` tabs are scanned but carry `availability: "debug"` (D6).

Mobile builds and CI tag builds use the committed file unchanged. The per-PR check catches a stale file from a mobile-only workflow before merge.

*Alternative considered:* generating at build time only, uncommitted. That puts Python on the critical path of every Android and iOS build for no gain over a committed file the PR check keeps honest.

### D6. Availability is a named condition from one table

There is one QML-visible table, `SettingsAvailability`, with entries such as:
- `android`: `Qt.platform.os === "android"`
- `simulator`: `Settings.app.simulatorAvailable`
- `debug`: `Settings.app.isDebugBuild`

`SettingsCard.visible` (and `SettingsSearch.availability` on a row) is computed from it. The search dialog filters entries through the same table. The scanner requires a literal name from the table.

This replaces the dialog's hard-coded `simulationMode` filter. It also fixes Launcher Mode, whose card sets `visible: Qt.platform.os === "android"` while search offers it everywhere.

### D7. Matching: vendored Fuse.js 7.5.0, as an ES module

- **Vendoring.** `fuse.mjs` (50 KB, Apache-2.0, compatible with the project's v3 licence) is vendored at a pinned version with its licence in `qml/third_party/fuse/`, written by `scripts/vendor_fusejs.py`. QV4 in Qt 6.12 cannot parse ES2018 object spread, so the script rewrites Fuse's five object-spread sites to `Object.assign`; it checks the upstream sha256 and fails if any rewrite misses. A small `SettingsSearchMatcher.mjs` imports it and is imported by `SettingsSearchDialog`.
- **Fuse index.** It is built once per language change, not per keystroke. With ~300 entries that costs well under a frame.
- **Keys and weights:**

  | Key | Weight |
  |---|---|
  | `title` | 0.45 |
  | `keywords` | 0.25 |
  | `cardTitle` | 0.12 |
  | `description` | 0.10 |
  | `fallbackTitle` (English) | 0.08 |

- **Options:** `ignoreDiacritics: true`, `ignoreLocation: true`, `includeScore: true`, and a threshold tuned against the existing keyword set (~0.35 as the starting point).
- **AND across words.** Fuse's token search (`useTokenSearch: true, tokenMatch: "all"`) keeps an entry only if every query word matched. It needs a custom `tokenize` function (split on whitespace and punctuation): Fuse's default tokenizer is `/[\p{L}\p{M}\p{N}_]+/gu`, and QV4's regex engine matches nothing with it, silently.
- **Short words.** Words of three characters or fewer need an exact substring match, so "de1" does not fuzz into noise. That matches today's `maxDist = 0` for short words.

*Alternatives considered:*
- **Extending the in-house Levenshtein matcher.** The user chose a maintained library.
- **rapidfuzz-cpp behind a C++ singleton.** It adds a C++ type and a header for a ~300-entry list.

### D8. Tests

**One new test file, `tests/tst_settingssearch.cpp`, using QJSEngine** (the `tst_recipesearch` pattern). It loads the generated index and the matcher modules and asserts:
- every keyword in the pre-migration index still finds its original card (see Migration);
- the spec scenarios: typos, accents, title over keyword, AND semantics, short-word exactness;
- every entry has a non-empty key, fallback and a known `tabId`.

*Defect shape:* a matcher or weighting change that silently stops a known query from finding its setting. No existing test covers settings search.

**Scanner self-test.** Inline fixtures in `settings_search_index.py --self-test`, each a small QML snippet with the error it must produce (or none). It runs per PR before `--check`. *Defect shape:* the scanner silently accepting a bypass. The self-test proves each rule can fail.

## Risks / Trade-offs

- **[No type information]** The scanner cannot know that a new project component wraps a switch.
  → Closed-world classification: an unclassified type name on a tab fails the check, so the gap is a decision someone has to make, not a silent miss. The classification table is the one place to review.
- **[QML the tokenizer does not model]** An unusual construct could be mis-read.
  → The tokenizer fails on anything it cannot place instead of skipping it, and its input is only the settings tab files and declared hosts.
- **[Fuse.js in QV4]** Resolved by the 1.1 spike: five object-spread sites rewritten by the vendoring script, and a custom tokenizer (D7).
- **[Large mechanical migration]** 12 tab files and ~280 controls; visual regressions are possible from the card swap.
  → Migrate one tab per commit, with screenshots before and after. `SettingsCard` reproduces the existing margins exactly. QML is tested manually per project practice, so every converted tab must be opened (the `Bound`/required-property delegate warning in CLAUDE.md applies to any delegate touched).
- **[Accessible-name prefix matching]** A name like "Steam" on one card and "Steam temperature" elsewhere is fine. Duplicates only matter within a card, and D2 rejects those.
- **[Result-list length]** ~300 entries instead of 57. Ranking (D7) keeps the best match on top. An empty query still lists everything, grouped by tab as today.
- **[Overlap with #2036]** If #2036 merges first, its script and `text-invariants.yml` step are deleted here, and its `settings-search-coverage` change is archived by its own PR. This change's MODIFIED requirement then replaces the restated text.

## Migration Plan

1. **Snapshot.** Export every `(keyword, cardId)` and `(title, cardId)` pair from the current `SettingsSearchIndex.js`, plus #2036's three entries, into a test fixture. These are the queries that must keep working.
2. Land the scanner, `SettingsCard`, `SettingsSearch`, `SettingsAvailability` and the matcher, with the scanner's rules active only for files that already use `SettingsCard`. This lets tabs migrate one at a time.
3. Migrate the tabs one at a time, moving each card's keywords from the old index onto the card or row.
4. Once every tab is migrated:
   - make the scanner's rules unconditional for all tab files;
   - delete `SettingsSearchIndex.js`;
   - switch the dialog to the generated file.
5. **Rollback.** Revert the merge commit. Nothing is persisted, and no setting or schema changes.

## Open Questions

- The exact Fuse threshold and weights are tuned against the snapshot fixture during implementation. They change no requirement.
