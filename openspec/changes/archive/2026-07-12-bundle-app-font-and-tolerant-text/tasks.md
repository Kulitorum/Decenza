## 1. Bundle the font asset

- [x] 1.1 Add `resources/fonts/Roboto-{Regular,Medium,Bold,Light}.ttf` + `OFL.txt` license. Instanced from the canonical google/fonts variable font (`ofl/roboto/Roboto[wdth,wght].ttf`) at wght 400/500/700/300, wdth 100. **Note:** current Roboto is **SIL OFL 1.1**, not Apache-2.0 (artifacts corrected). Weights match usage: Regular+Bold (Theme roles), Medium (EspressoPage/coaching/timeline), Light (2 screensaver spots).
- [x] 1.2 Registered in `resources/resources.qrc` (already wired into CMake via `qt_add_resources`); resolves at `:/fonts/…`
- [x] 1.3 Resources build + addressable via `qrc:/` — verified: built via Qt Creator, fonts/icons embedded and render on Mac.

## 2. Register and default the font at startup

- [x] 2.1 `src/main.cpp`: `QFontDatabase::addApplicationFont` for each file before the QML engine loads; logs the family, warns + continues on -1
- [x] 2.2 `app.setFont(QFont(bundledFamily))` sets the app-wide default family (size left to `Theme.scaled()`)
- [x] 2.3 `qml/Theme.qml` font roles have no explicit family → inherit the bundled default (verified: no `font.family` in Theme roles)
- [x] 2.4 `Theme.monoFontFamily` is an explicit per-platform family used via `font.family:` at call sites → still overrides the default, unaffected
- [x] 2.5 Medium (500) + Light (300) instanced as real static weights under family "Roboto" (weightClass 500/300 verified) so `Font.Medium`/`Font.Light` select them, not a synthesized weight
- Runtime confirmation of 2.3–2.5 happens on Jeff's device (see §3/§8)

## 3. Verify determinism

- [x] 3.1 Startup log shows registered Roboto family — confirmed on Mac (`[Font] Bundled application font set: "Roboto"`). Android to be confirmed on the released build.
- [ ] 3.2 (deferred post-merge) Advance-width parity macOS vs physical Android — Android side pending.
- [x] 3.3 No appearance regression on key screens — verified on Mac (Idle, Shot History, Shot Detail, search dialog, sort icons via computer-use).

## 4. Container tolerance — dead `elide` on rich text

- [x] 4.1 Audited all `Text.RichText` usages across the codebase per-element. Confirmed the dead pattern is **`Text.RichText` + `elide`** (Qt ignores elide on RichText); **`Text.StyledText` + `elide` works** (per Qt 6 docs), and `RichText` + `wrapMode` works.
- [x] 4.2 **Approach changed (better):** fix = switch the dead labels from `Text.RichText` → `Text.StyledText` (elide then works, stays single-line, emoji `<img>`/`<font>` still render) instead of wrapping. This avoids all height/shared-component regression risk — `EquipmentSummary` is shared by `EquipmentCard`, `ShotDetailPage`, `PostShotReviewPage`, so wrapping would have changed height in the shot pages.
- [x] 4.3 Applied to all 18 confirmed dead-elide elements: `EquipmentSummary` (3), `ComparisonShotTable` (2), `AutoFavoriteInfoPage` (6), `CustomItem` (1 compact), `CustomEditorPopup` (1 bar preview), `ShotDetailPage` (2: title + barista), `PostShotReviewPage` (1 title), `ShotHistoryPage` (2 shot-list rows — the reported list). Left alone: `RichText`+`wrap` labels (work), `ExpandableTextArea` (wrap+clip collapsed preview — wraps, not a mid-glyph clip), `ScreensaverPage` author (no elide), AutoText+elide elements (elide works).
- [x] 4.4 N/A — no labels were switched to wrap, so no container-height regression to check. StyledText preserves the original single-line elided layout.

## 4b. Standardize on StyledText (folded in)

- [x] 4b.1 Confirmed from Qt source (`qquickstyledtext.cpp`): StyledText `<img>` honors an `align` attribute (`middle`/`top`) but ignores CSS `style=`. Updated `Theme.replaceEmojiWithImg` to emit `align="middle"` (kept `style="vertical-align:middle"` for any RichText caller) so inline emoji stay centered under StyledText.
- [x] 4b.2 Converted **all** remaining explicit `Text.RichText` labels to `Text.StyledText` (ProfileKnowledgeDialog, ExpandableTextArea, CustomEditorPopup ×2, CustomItem ×2, ScreensaverPage, ShotDetailPage recipe, PostShotReviewPage recipe) — verified each uses only StyledText-supported markup (`<b>/<i>/<font>/<a>/<img>/<br>`), no tables/CSS. `TextEdit.RichText` editable field left as-is (elide N/A).
- [x] 4b.3 Zero explicit `textFormat: Text.RichText` remain in `qml/` (grep-verified).
- [x] 4b.4 Documented the convention: `Theme.replaceEmojiWithImg` comment updated, new `QML_GOTCHAS.md` section ("elide is silently ignored on Text.RichText"), and one-liner added to `CLAUDE.md`.

## 5. Container tolerance — fixed-size popups/dialogs

- [x] 5.1 `searchHelpDialog` (`ShotHistoryPage.qml`) wrapped in a height-capped `ScrollView` (`Math.min(content, page.height - 80)`, `clip: true`) — a wider/fallback font or long translation now scrolls instead of overflowing the screen — reported site #2
- [x] 5.2 Audited other reported-screenshot dialogs: **Edit Bag** (`ChangeBeansDialog`) already uses Flickable/ScrollView (safe). `EquipmentInfoDialog` has no scroll container — flagged for the on-device pass; not restructured blind (short info dialog, not in the overflow screenshots).

## 6. Retire superseded diagnostics

- [x] 6.1 **Reviewed — kept `[#582 diag]`.** It is issue #582's (Lenovo tablet gap) instrumentation, reused during triage; removing it would harm that separate open issue. No `#1469` probe was ever added (the metric probe was discussed and declined), so there is no #1469 scaffolding to remove.

## 7. Accessibility & conventions (touched files)

- [x] 7.1 Full pre-existing-violation sweep of every touched file (policy: fix pre-existing issues in files you touch, not just your diff). Fixed: hardcoded colors (`#2E7D32`→`Theme.successColor` ShotHistoryPage; `#555555`→`Theme.primaryColor` CustomEditorPopup, also correcting a preview fidelity mismatch); Unicode-glyphs-as-icons replaced with new tintable SVGs (sort ▼/▲ → SortAscending/Descending.svg via new `AccessibleButton.tintIcon` opt-in; align ◀●▶ → AlignLeft/Center/Right.svg via `ColoredIcon`); ExpandableTextArea mobile a11y (container exposed as a Button so screen-reader users can open the editor); removed dead `LabeledComboBox` component in PostShotReviewPage.
- [x] 7.2 No user-visible strings added or changed (attribute-only + structural wrap); all existing text stays behind `TranslationManager.translate`/`Tr` and `Theme` styling. `ScrollView` uses no hardcoded colors/sizes.

## 8. Build, verify, and hand off

- [x] 8.1 Built via Qt Creator (0 errors/warnings) + desktop visual check on Mac.
- [ ] 8.2 (deferred post-merge) GitHub Android CI build for a physical-device check.
- [ ] 8.3 (deferred post-merge) #1469 reporter confirms on Android + Windows.
- [x] 8.4 `/opsx:archive` run as the final branch commit before merge.
