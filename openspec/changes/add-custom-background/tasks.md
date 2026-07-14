## 1. Settings: background image path

- [x] 1.1 Add `backgroundImagePath` `Q_PROPERTY` (QString, default empty, persisted) to `SettingsTheme` (`src/core/settings_theme.h` / `.cpp`), following the existing property pattern (e.g. `skin`).
- [ ] 1.2 Verify the property round-trips through the app's settings persistence (save, restart, reload) with an empty default.

## 2. C++: locally-cached catalog image accessor

- [x] 2.1 Add `QVariantList getCachedCatalogImages() const` to `ScreensaverVideoManager` (`src/screensaver/screensavervideomanager.h` / `.cpp`): iterate `m_cacheIndex` directly (not `m_catalog`, which only holds the currently-selected category and is replaced on every switch), keep entries whose cached file extension is `.jpg`/`.jpeg`/`.png`, return `{id, type, filename, path (absolute), bytes, author}` per item — same shape as `getPersonalMediaList()`. See design.md Decision 2 for why `m_catalog`-based filtering was wrong.
- [x] 2.2 Expose the new method as `Q_INVOKABLE` so QML can call it from the picker dialog.
- [x] 2.3 Confirm no changes to `startBackgroundDownload()`, `queueAllVideosForDownload()`, or the rate limiter — this method must be read-only.

## 3. QML: shared page background component

- [x] 3.1 Create `qml/components/ThemedPageBackground.qml`: renders an `Image` (`asynchronous: true`, `fillMode: Image.PreserveAspectCrop`, `sourceSize` capped to device resolution, `source: "file:///" + Settings.theme.backgroundImagePath`) when the path is non-empty and the file loads successfully; falls back to `Rectangle { color: Theme.backgroundColor }` when the path is empty or `Image.status === Image.Error`.
- [x] 3.2 Add `qml/components/ThemedPageBackground.qml` to `CMakeLists.txt`'s `qt_add_qml_module` file list.
- [x] 3.3 Replace `background: Rectangle { color: Theme.backgroundColor }` with `background: ThemedPageBackground {}` in: `IdlePage.qml`, `BeanInfoPage.qml`, `RecipesPage.qml`, `SteamPage.qml`, `HotWaterPage.qml`, `FlushPage.qml`, `EquipmentPage.qml`, `SettingsPage.qml`.

## 4. QML: LayoutPreview background-image support

- [x] 4.1 Add `property string backgroundImageSource: ""` to `qml/components/layout/LayoutPreview.qml`.
- [x] 4.2 When non-empty, replace the flat `Rectangle { anchors.fill: parent; color: Theme.backgroundColor }` (current lines 36-39) with an `Image` using the same fill/crop/async conventions as `ThemedPageBackground.qml`; keep the flat Rectangle as the fallback when empty.
- [x] 4.3 Verify `SettingsLayoutTab.qml`'s existing use of `LayoutPreview` (`previewBox`, line 587) is unaffected when `backgroundImageSource` is left unset (default behavior unchanged).

## 5. QML: background picker dialog

- [x] 5.1 Create `qml/components/BackgroundPickerDialog.qml`: modal `Dialog` shell modeled on `CustomEditorPopup.qml` (`Overlay`-anchored, `Theme.surfaceColor`/`Theme.cardRadius` styling, `closePolicy: Dialog.CloseOnEscape`).
- [x] 5.2 Add `qml/components/BackgroundPickerDialog.qml` to `CMakeLists.txt`'s `qt_add_qml_module` file list.
- [x] 5.3 Build the combined image source list: `ScreensaverManager.getPersonalMediaList()` filtered to images, plus `ScreensaverManager.getCachedCatalogImages()` (task 2.1/2.2).
- [x] 5.4 Implement the `GridView` thumbnail grid modeled on `EmojiPicker.qml`'s delegate (selection ring, `Image` thumbnail with `BeanThumbnail.qml`-style async/capped-`sourceSize`/placeholder loading, `MouseArea` emitting the selected path), including a "None" tile that clears the selection.
- [x] 5.5 Embed `LayoutPreview { backgroundImageSource: <currently highlighted thumbnail path> }` in the dialog so the preview updates live as the user taps thumbnails, before any save.
- [x] 5.6 Wire a confirm action that writes the chosen path (or clears it, for "None") to `Settings.theme.backgroundImagePath`, and a cancel/dismiss path that discards the in-progress selection without changing the saved setting.
- [x] 5.7 Add a short in-dialog hint noting that more stock images appear over time as they finish downloading in the background.

## 6. QML: settings entry point

- [x] 6.1 Add a "Background" row/button inside the Theme Mode card (`objectName: "themeMode"`) in `qml/pages/settings/SettingsMachineTab.qml` (~lines 596-694), following the existing `RowLayout` row pattern, that opens `BackgroundPickerDialog`.
- [x] 6.2 Add an entry to `qml/components/SettingsSearchIndex.js` for the new control so it's reachable via Settings search (matching the `themeMode` card's existing `cardId`).

## 7. Accessibility

- [x] 7.1 Ensure the Background button, picker dialog, thumbnail grid items, and "None" tile each have `Accessible.role`, `Accessible.name`, `Accessible.focusable: true`, and `Accessible.onPressAction`, per `docs/CLAUDE_MD/ACCESSIBILITY.md`.
- [ ] 7.2 Verify focus order through the picker dialog (open → grid → confirm/cancel) is sane for TalkBack/VoiceOver.

## 8. Manual verification

- [ ] 8.1 With no background set, confirm all 8 target pages render exactly as before (regression check).
- [ ] 8.2 Set a personal-upload image as background; confirm it renders on all 8 pages in both light and dark theme mode.
- [ ] 8.3 Set a cached stock/catalog image as background; confirm same as above.
- [ ] 8.4 Confirm pages outside the 8 (e.g. Espresso/shot screen, Dialing Assistant, Profile Editor) are unaffected.
- [ ] 8.5 Delete the personal media file backing the current background (via "Clear personal media"); confirm the 8 pages fall back to the flat background color rather than showing a broken image.
- [ ] 8.6 Clear the background via the "None" tile; confirm all 8 pages revert to the flat background color.
- [ ] 8.7 Visually assess legibility of foreground widgets on Recipes, Equipment, and Settings (the busiest of the 8 pages) with a high-contrast background image selected; note in the PR whether a scrim on the page body (beyond the bar scrim) is needed (per design.md's open question) rather than deciding upfront.

## 9. Documentation

- [x] 9.1 Add a short section to `docs/CLAUDE_MD/SETTINGS.md` or a new note documenting the `backgroundImagePath` property and its "grows over time" catalog-caching behavior, so future triage doesn't mistake a sparse picker grid for a bug.
- [ ] 9.2 Document the feature in the user-facing GitHub wiki manual (`Kulitorum/Decenza.wiki.git`, `Manual` page): how to open the picker (Theme Mode card → Background), that images come from the screensaver media library (personal uploads + stock, images only), that it applies to every page in the app in both light and dark mode (including behind the top/bottom bars and card panels), and that newly-selected stock images may take time to appear if not yet cached.

## 10. QML: extend background through shared chrome bars

- [x] 10.1 Add `root.currentPageHasThemedBackground` derived property to `qml/main.qml`: initially true when `Settings.theme.backgroundImagePath` is non-empty and `pageStack.currentItem.objectName` matched an 8-page allowlist; simplified in task 11 to `Settings.theme.backgroundImagePath.length > 0` once coverage went universal (no page list needed).
- [x] 10.2 `qml/components/StatusBar.qml` (global top bar, shown on every page): when `root.currentPageHasThemedBackground`, render at 62% opacity (`Qt.rgba(color.r, g, b, 0.62)`) instead of fully opaque.
- [x] 10.3 `qml/pages/IdlePage.qml`'s own bottom nav bar (predates/bypasses the shared `BottomBar` component): same 62%-opacity treatment, gated directly on `Settings.theme.backgroundImagePath.length > 0`.
- [x] 10.4 `qml/components/BottomBar.qml` (shared back-button bar used by ~25 pages): same 62%-opacity treatment, gated on `root.currentPageHasThemedBackground`.

## 11. QML: expand page coverage from 8 pages to universal

- [x] 11.1 Simplify `root.currentPageHasThemedBackground` in `qml/main.qml` to `Settings.theme.backgroundImagePath.length > 0` — remove the `_themedBackgroundPageNames` allowlist now that every page is covered.
- [x] 11.2 Swap `background: Rectangle { color: Theme.backgroundColor }` → `background: ThemedPageBackground {}` in the remaining 22 pages: `EspressoPage`, `ProfileEditorPage`, `ShotDetailPage`, `DescalingPage`, `AutoFavoriteInfoPage`, `SimpleProfileEditorPage`, `PostShotReviewPage`, `ProfileSelectorPage`, `FlowCalibrationPage`, `VisualizerMultiImportPage`, `VisualizerBrowserPage`, `ShotHistoryPage`, `ProfileInfoPage`, `DialingAssistantPage`, `ShotComparisonPage`, `RecipeEditorPage`, `AISettingsPage`, `ProfileImportPage`, `RecipeWizardPage`, `AutoFavoritesPage`, `settings/StringBrowserPage`, `settings/AddLanguagePage`.

## 12. QML: page-level card translucency

- [x] 12.1 Add `Theme.cardBackgroundColor` to `qml/Theme.qml`: `Qt.rgba(surfaceColor.r, g, b, 0.62)` when `Settings.theme.backgroundImagePath` is set, else opaque `surfaceColor`.
- [x] 12.2 Sweep `qml/pages/` (Flush, Hot Water, Steam, Recipes, and the 22 pages from task 11) plus `EquipmentCard.qml`/`BagCard.qml`/`RecipeDrinkCard.qml`: switch page-level content cards from `color: Theme.surfaceColor` to `color: Theme.cardBackgroundColor`. Leave dialog/popup backgrounds (`background: Rectangle { color: Theme.surfaceColor; ... }` inside a `Dialog`/`Popup`) and toast/transient notifications (sized by `text.implicitWidth`, anchored to a screen edge, high `z`) untouched — see design.md Decision 7 for the classification rule.
- [x] 12.3 Sweep all 13 `qml/pages/settings/Settings*Tab.qml` files with the same rule.
- [ ] 12.4 **Follow-up, not yet done**: sweep `qml/components/` (~43 files, ~59 occurrences of `Theme.surfaceColor`) — shared sub-widgets, many nested inside cards already covered by 12.2/12.3. Deferred pending a look at how the current translucency level reads in the running app (nested double-translucency is a genuinely different question from the page-level sweep).
- [ ] 12.5 Manual verification: on each page with a background set, confirm both bars and page-level cards show the image through a semi-transparent scrim; with no background set, confirm no visual change anywhere.

## 13. QML: fix LayoutPreview gaps found during testing

- [x] 13.1 `qml/components/layout/LayoutPreview.qml`: default `backgroundImageSource` to `Settings.theme.backgroundImagePath` instead of `""`, so any plain `LayoutPreview {}` (e.g. `SettingsLayoutTab.qml`'s layout-only preview) reflects the real saved background automatically. `BackgroundPickerDialog`'s explicit override (the candidate thumbnail) still wins as before.
- [x] 13.2 Add the same status-bar/bottom-bar scrim treatment (mirroring `StatusBar.qml`/`IdlePage.qml`) to `LayoutPreview.qml`'s own mockup bars (`previewStatusBar`, `previewBottomBar`) — these are a hand-built mockup, not the real components, so they didn't inherit the earlier bar-scrim fix automatically.

## 14. QML: recessed/inset form-control fill (found during testing)

Found live: the AI Provider tab's unconfigured-provider chips (OpenRouter, Ollama) rendered as opaque blocks — their fallback color was `Theme.backgroundColor`, used throughout the app as a "blend into the flat page" fill for recessed controls, not just this one chip. With a photo background, "blend into the flat page" has no flat color to match.

- [x] 14.1 Add `Theme.insetBackgroundColor` to `qml/Theme.qml`: same 62%-opacity-scrim pattern as `cardBackgroundColor`, but over `backgroundColor` instead of `surfaceColor`. Used for controls where the box/track shape needs to stay visible (unlike a decorative chip, which can go fully transparent).
- [x] 14.2 `qml/pages/settings/SettingsAITab.qml`: unconfigured provider chip — `Theme.backgroundColor` → `"transparent"` (fully see-through, not scrimmed; this specific chip has no content of its own to protect, and the border still outlines it) when a background image is set, else unchanged.
- [x] 14.3 `qml/components/StyledTextField.qml` and `SuggestionField.qml`: `fieldColor` default → `Theme.insetBackgroundColor` (cascades to every text field in the app that doesn't override `fieldColor`).
- [x] 14.4 `qml/components/StyledSwitch.qml`: unchecked track fill → `Theme.insetBackgroundColor`.
- [x] 14.5 `qml/components/StyledTabButton.qml`: active-tab background (both the main Rectangle and the bottom-corner-cover Rectangle) → `Theme.insetBackgroundColor`.
- [ ] 14.6 **Follow-up, not yet done**: ~15 pages still inline their own `Theme.backgroundColor`-as-unselected-pill fallback directly (e.g. `FlushPage.qml:292` preset pills, `HotWaterPage.qml:379/587/627` vessel/mode pills, `ProfileEditorPage.qml`'s frame-type pill rows, `ProfileSelectorPage.qml:1112`) rather than going through a shared component — same pattern as 14.1-14.5 but not yet swept, since each is a per-file judgment call (transparent vs. `insetBackgroundColor` vs. leave alone) rather than a single shared-component fix. Deferred pending a look at how the shared-component fixes read live.

## 15. QML: lighter scrim style + BottomBar id-shadow bug fix (found during testing)

Live testing surfaced two more issues: (1) the 0.62-opacity scrim used everywhere read as too heavy/dark compared to the AI Provider tab's pre-existing "configured" chip look (`Qt.rgba(0.2, 0.7, 0.3, 0.25)`, already in the app before this change) — direct user preference for the lighter style, applied consistently; (2) `BottomBar.qml`'s bar scrim (task 10.4) never actually worked on any of its ~25 call sites — a real bug, not a preference issue.

- [x] 15.1 Add `Theme.backgroundScrimAlpha` (0.25, replacing the earlier 0.62) and a `Theme.scrimColor(baseColor)` helper function to `qml/Theme.qml`; `cardBackgroundColor`/`insetBackgroundColor` now call it instead of hand-rolling `Qt.rgba(...)`.
- [x] 15.2 **Bug fix**: `qml/components/BottomBar.qml`'s own root `Rectangle` is `id: root` — this shadows `main.qml`'s `id: root` (the `ApplicationWindow`) within `BottomBar.qml`'s document scope, so the task-10.4 binding `root.currentPageHasThemedBackground` was silently resolving against `BottomBar`'s own Rectangle (no such property → `undefined` → the ternary always took the opaque branch). The bar scrim never rendered on any page using the shared `BottomBar` component. Fixed by checking `Settings.theme.backgroundImagePath.length > 0` directly instead of reaching through `root` at all — sidesteps the shadow entirely and is simpler now that coverage is universal (see task 11.1).
- [x] 15.3 Removed `main.qml`'s now-unused `currentPageHasThemedBackground` (was only ever `Settings.theme.backgroundImagePath.length > 0` after task 11.1 — every remaining reader now checks that directly, so the indirection added nothing).
- [x] 15.4 `qml/components/StatusBar.qml`: same direct-check simplification as 15.2 (StatusBar's own id is `statusBarRoot`, not `root`, so it didn't have the shadow bug — this is a simplification, not a fix).
- [x] 15.5 `qml/pages/IdlePage.qml` bottom bar and `qml/components/layout/LayoutPreview.qml`'s mockup bars: route through `Theme.scrimColor()` instead of inline `Qt.rgba(..., 0.62)`.
- [x] 15.6 `qml/pages/settings/SettingsAITab.qml`: unconfigured-provider chip — `"transparent"` (task 14.2) → `Theme.insetBackgroundColor`, matching the tinted-glass look of the adjacent "configured" chips instead of disappearing entirely.
- [x] 15.7 `qml/components/layout/items/CustomItem.qml`: idle-screen action tiles (Recipes/Beans/Steam/Hot Water/Flush/Equipment/etc. — all compiled to `CustomItem` via `LayoutItemDelegate.compileToCustom()`) and user-authored Custom widgets now scrim `_effectiveBackground` via `Theme.scrimColor()` when a background image is set. Text/icon content color (`_contentColor: Theme.primaryContrastColor`) intentionally left unchanged — only the fill goes translucent, matching every other scrim in the app.
- [x] 15.8 **Legibility tuning**: `Theme.backgroundScrimAlpha` 0.25 → 0.4 — `textSecondaryColor` (used for roaster/origin/notes metadata on cards, e.g. Beans inventory) wasn't reliably legible against busy/bright photo regions at 0.25, while bold `textColor` had enough weight to survive. Fixed via the shared alpha constant (uniform, one-line) rather than special-casing `textSecondaryColor` to match primary when a background is active, which would erase the app's text-hierarchy convention specifically in that one state instead of fixing the underlying contrast. Not applied to the AI Provider tab's pre-existing "configured" chip green tint (`Qt.rgba(0.2, 0.7, 0.3, 0.25)`, hardcoded, unrelated to this feature — it's translucent regardless of whether a background image is set) — now slightly lighter than the adjacent unconfigured chip's `insetBackgroundColor`, a minor pre-existing-code mismatch out of scope here.

## 16. QML: live click-through QA pass across every page (found during testing)

Drove the running (simulation-mode) app directly via computer-use across all ~30 pages and all 13 Settings tabs, looking for cards/pills/panels the earlier automated sweeps (tasks 12, 14) missed — either because the grep pattern only matched exact `color: Theme.surfaceColor` text (missing ternaries like `color: cond ? X : Theme.surfaceColor`) or because the occurrence lived in a shared component not covered by the page-level sweep.

- [x] 16.1 `qml/components/PresetPillRow.qml`, `qml/components/ValueInput.qml` (compact `valueDisplay`): unselected/inactive fill → `Theme.insetBackgroundColor` / `Theme.cardBackgroundColor` respectively. High-leverage fixes — both are shared components used by Beans/Recipes/Steam/Equipment/Hot Water/Flush idle popups and by every numeric stepper field in the app, so each one-line fix cascades across dozens of screens at once.
- [x] 16.2 `qml/pages/ShotHistoryPage.qml:631` — main shot-row delegate `color: isSelected ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor` missed by the earlier exact-match sweep (task 12.2) because the target text isn't immediately after `color:`. Fixed to `Theme.cardBackgroundColor`. Prompted a broader non-exact grep (`Theme\.surfaceColor` anywhere on the line) that surfaced ~40 more ternary-pattern misses across the codebase; roughly a dozen of the clearest were fixed alongside this one (`SteamPage.qml`, `FlushPage.qml`, `HotWaterPage.qml`, `ProfileSelectorPage.qml`, `ProfileEditorPage.qml` frame-type pills, `AISettingsPage.qml` — a separate older AI provider page distinct from `settings/SettingsAITab.qml`, `PostShotReviewPage.qml`, `ShotComparisonPage.qml`, `ShotDetailPage.qml`, `settings/AddLanguagePage.qml`, `settings/StringBrowserPage.qml`, `RecipeWizardPage.qml`, `components/FavoritesListView.qml`, `settings/SettingsConnectionsTab.qml`'s USB connection log panel).
- [x] 16.3 Settings tabs not yet visually spot-checked in earlier sessions — `SettingsScreensaverTab.qml` (Auto-Wake day-of-week pills, the Video Category list rows, the video-download progress track), `SettingsHomeAutomationTab.qml` (MQTT connection-status pill, Publish Interval combo box background, REST API endpoint box), `SettingsLanguageTab.qml` (translation-progress / English-base-language info boxes), `SettingsUpdateTab.qml` (current-version box, update-status area, inline release-notes box) — all had inset content boxes still on flat `Theme.backgroundColor`; switched to `Theme.insetBackgroundColor`. Modal `Dialog`/`Popup` backgrounds in the same files (delete confirmation, submission result, retry status, firmware update, release notes popup, donate dialog) were deliberately left opaque — they sit above content per the existing DIALOG classification rule (design.md Decision 7).
- [x] 16.4 **Root-caused the previously-deferred top-left compact icon row**: `qml/components/layout/items/MiniGHCItem.qml` (the "Mini GHC" simulated-hardware-buttons widget, shown on the idle screen when running headless/simulated with no physical Group Head Controller). Both its compact mode (5 small icon buttons in the status-bar-height row) and full/grid mode filled buttons with solid, unscrimmed `Theme.primaryColor`/`buttonColor` — visually inconsistent with every other action tile on the idle screen (task 15.7). Added a `_effectiveColor()` helper (compact mode, same document scope as `root`) and a `_effectiveButtonColor` property on the inline `MiniGHCButton` component (full mode — inline `component` blocks are separately-scoped types and can't reach an enclosing `id`, so this needed its own scrim property rather than reusing `root`'s helper); both apply `Theme.scrimColor()` when a background image is set. Full-mode card background also switched `Theme.surfaceColor` → `Theme.cardBackgroundColor` for consistency with every other layout-widget card.
- [x] 16.5 Rebuilt (`Decenza-Desktop`, zero errors/warnings) to confirm all ~20 files touched in this pass compile cleanly.

## 17. QML: Settings tab bar legibility + textSecondaryColor reversal (found during testing)

User-reported screenshot: the Settings tab bar's unselected tab labels (`Machine`, `Calibration`, `History & Data`, etc.) were unreadable against a busy background photo — text sitting directly on the page background with only a 1px border line behind it, no surface at all.

- [x] 17.1 `qml/pages/SettingsPage.qml`: gave the `TabBar`'s own `background` Rectangle a `Theme.cardBackgroundColor` fill when a background image is set (was `"transparent"`), so the whole tab row — not just the active tab's pill — sits on a scrim.
- [x] 17.2 First attempt (superseded by 17.4): added a separate `Theme.textSecondaryOnBackgroundColor` property (brightened `textSecondaryColor` by 1.4x) scoped only to text with no card behind it, on the theory that text already on a `cardBackgroundColor`/`insetBackgroundColor` scrim had enough contrast from that scrim alone. Wired into `SettingsPage.qml`'s tab label and `StyledTabButton.qml`'s default `contentItem`.
- [x] 17.3 Live-verified 17.1+17.2 fixed the tab bar. User then pointed at the Beans inventory cards (roaster/origin/tasting-note text, e.g. "Prodigal Coffee", "Brazil, Colombia · Natural") as a counter-example — that text sits on a `cardBackgroundColor`-scrimmed `BagCard`, over a busy photo, and was just as hard to read. Disproves the 17.2 theory: a card's own scrim isn't sufficient contrast for `textSecondaryColor` specifically, at least not on top of a bright/busy region of a photo.
- [x] 17.4 **Reversal, explicitly requested**: moved the brightening from `textSecondaryOnBackgroundColor` into `textSecondaryColor` itself (`qml/Theme.qml`) — supersedes task 15.8's stated reasoning ("rather than special-casing textSecondaryColor... would erase the app's text-hierarchy convention"; in practice, leaving it unbrightened made secondary text illegible in exactly the metadata-heavy contexts — card subtitles, tab labels — where it's used most). `textSecondaryOnBackgroundColor` kept as a plain alias (`: textSecondaryColor`) so the 17.2 call sites don't need to churn back. Now cascades everywhere `Theme.textSecondaryColor` is read — bean cards, recipe cards, settings tabs, everywhere — with zero change when no background image is set.
- [x] 17.5 Rebuilt (`Decenza-Desktop`, zero errors/warnings) and live-verified on the Beans page: roaster/origin text now legible against the same busy photo region that was previously unreadable.
