## Why

Idle and several other everyday pages (Beans, Recipes, Steam, Hot Water, Flush, Equipment) always render a flat `Theme.backgroundColor` rectangle. [Issue #168](https://github.com/Kulitorum/Decenza/issues/168) asks for a wallpaper feature so users can personalize these screens the way they already can personalize the screensaver. Rather than build a separate upload/storage path, this reuses the existing screensaver media library (personal uploads + cached stock catalog images) as the source, since that machinery — image validation, resizing, local caching — already exists and is exposed as a QML singleton.

## What Changes

- Add a single global `Settings.theme` property holding an optional background image path. Empty means today's behavior (flat `Theme.backgroundColor`). The same image applies in both light and dark theme mode — no separate light/dark choice.
- Add a "Background" control to the existing Theme Mode card in Machine settings (`SettingsMachineTab.qml`) that opens a new picker dialog.
- Add `BackgroundPickerDialog.qml`: a modal grid-of-thumbnails picker sourced from all personal (web-uploaded) screensaver images plus stock/catalog screensaver images that are **already cached locally** (no forced download of the remote catalog just to populate the grid — coverage grows over time via the existing rate-limited background download). Videos never appear; images only.
- Add a live preview inside the picker: extend `LayoutPreview.qml` (the same component the Layout settings tab already uses to preview the idle screen) with an optional background-image property, so the user sees their real idle-screen zones composited over the candidate wallpaper before committing. Preview covers the idle screen only.
- Apply the chosen background (when set) to **every page in the app**. Each page currently uses the identical `background: Rectangle { color: Theme.backgroundColor }` one-liner, which becomes a conditional: background image when set, otherwise the existing flat color. Scope grew from an initial 8-page allowlist (Idle, Beans, Recipes, Steam, Hot Water, Flush, Equipment, Settings) to universal coverage per explicit direction partway through implementation — see design.md Decision 6a.
- Extend the background through the app's shared chrome bars — the top `StatusBar` (global, shown on every page) and the shared `BottomBar` (back-button bar used by ~25 pages) — everywhere a background image is set: each goes semi-transparent (a 62%-opacity scrim in its own theme color, keeping existing icon/text contrast) instead of fully opaque, so the wallpaper isn't cut off at the bar edges. `IdlePage`'s own bespoke bottom nav bar (it doesn't use the shared `BottomBar`) gets the same treatment. Automatic whenever a background image is set — no separate toggle.
- Extend the same translucency treatment to **page-level content cards** (the `Rectangle { color: Theme.surfaceColor; radius: Theme.cardRadius }` convention used throughout the app for card-style panels) via a new centralized `Theme.cardBackgroundColor`, so cards don't hide most of the wallpaper behind large opaque panels. Scoped to page-level cards only — dialog/popup backgrounds, toasts, and small buttons/pills intentionally stay fully opaque (dialogs already sit above a dim `Overlay` and don't need the wallpaper showing through; toasts and buttons need reliable contrast regardless of the photo underneath).

## Capabilities

### New Capabilities
- `custom-background`: the global background-image setting, the picker dialog (source list, thumbnails, live idle preview), its universal page coverage, the through-bar scrim treatment, and the page-level card translucency treatment.

### Modified Capabilities
(none — this does not change the documented requirements of `settings-ui`, `screensaver-overlay`, or `idle-default-layout`; it adds a new, independent control and reads existing screensaver media without altering their behavior)

## Impact

- **New setting**: `src/core/settings_theme.h` / `.cpp` — one new `Q_PROPERTY` following the existing pattern (alongside `skin`, `customThemeColors`, `themeMode`).
- **New QML**: `qml/components/ThemedPageBackground.qml`, `qml/components/BackgroundPickerDialog.qml` (both added to `CMakeLists.txt`'s `qt_add_qml_module` file list).
- **New Theme property**: `Theme.cardBackgroundColor` (`qml/Theme.qml`) — semi-transparent `surfaceColor` variant for page-level cards, active whenever a background image is set.
- **Modified QML**: `SettingsMachineTab.qml` (themeMode card gains a row), `LayoutPreview.qml` (new optional background-image property), every page's `background:` declaration (`background: ThemedPageBackground {}`), `StatusBar.qml` and `BottomBar.qml` (scrim when a background image is active), `main.qml` (new `currentPageHasThemedBackground` derived property), and page-level content cards across ~35 files (settings tabs + other pages) switched from `Theme.surfaceColor` to `Theme.cardBackgroundColor`. Dialog/popup backgrounds, toasts, and small buttons in those same files were deliberately left untouched.
- **Reused, unmodified**: `ScreensaverVideoManager` (`ScreensaverManager` singleton) — `getPersonalMediaList()` for personal uploads; a new read-only accessor for locally-cached catalog images is needed (see design.md), but no changes to download/caching behavior.
- **No backend/server changes**: the web upload flow, catalog manifest format, and screensaver playback are untouched.
- **Not yet covered**: ~43 files under `qml/components/` (shared sub-widgets, many nested inside cards already covered above) still use opaque `Theme.surfaceColor` — deliberately deferred pending a look at the current translucency level in the running app (see tasks.md).
