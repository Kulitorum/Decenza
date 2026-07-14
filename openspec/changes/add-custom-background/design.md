## Context

Every top-level `Page` in `qml/pages/` currently sets `background: Rectangle { color: Theme.backgroundColor }` — a flat color driven by `Settings.theme.customThemeColors`. Eight of these pages (Idle, Beans, Recipes, Steam, Hot Water, Flush, Equipment, Settings) are the target for an optional background image, sourced from the screensaver media library that already exists for the `ScreensaverPage`.

The screensaver media library (`ScreensaverVideoManager`, QML singleton `ScreensaverManager`) already distinguishes images from videos via a `MediaType` enum and manages two pools:
- **Personal media** (`m_personalCatalog`, backed by `<cacheDir>/personal/catalog.json`): always fully present on disk — these are files the user uploaded via the web interface.
- **Remote/stock catalog** (`m_catalog`, a `QList<VideoItem>` loaded from a remote manifest): downloaded lazily and rate-limited (`startBackgroundDownload()`) into `m_cacheIndex` (`<video path> -> CachedVideo`), so at any given moment only a subset of catalog items are actually present on disk.

There is no existing UI for "browse screensaver images as thumbnails" — the web upload gallery shows filename + icon only, not real image previews. `LayoutPreview.qml` is the one precedent for a *live, scaled-down reconstruction* of the idle screen (already reused by the Layout settings tab), which this design leans on for the picker's preview.

## Goals / Non-Goals

**Goals:**
- One global background image, applied identically in light and dark mode, across exactly 8 named pages.
- Reuse the screensaver media library as the sole image source — no new upload path, no new storage format.
- Let the user preview the idle screen with a candidate background before committing.
- Zero new network/download behavior: the picker only ever shows what's already on disk.
- On the covered pages, extend the background behind the shared chrome bars (top `StatusBar`, `BottomBar`/`IdlePage`'s own bottom bar) via a semi-transparent scrim, rather than having it visibly stop at each bar's edge.

**Non-Goals:**
- Per-page distinct backgrounds (v1 is one image for all 8 pages).
- Forcing/eager-downloading the full remote catalog to populate the picker.
- New thumbnail assets or catalog manifest changes (thumbnails are just the cached full images, decoded at a capped `sourceSize` for the grid).
- Video backgrounds.
- Any new dim/blur/scrim compositing beyond what's needed for text legibility, beyond the bar scrim itself — this is deliberately left to be evaluated visually during implementation rather than designed upfront.
- A user-facing toggle for the bar-scrim behavior — it's automatic whenever a background image is set, per explicit user direction, to avoid a settings surface for something that should just look right by default.
- Applying the background to any page beyond the 8 named ones (dialing assistant, profile editor, espresso/shot page, etc. are explicitly out of scope for this change).

## Decisions

### 1. Setting lives on `SettingsTheme`, not a new domain or on `ScreensaverVideoManager`
A new `QString backgroundImagePath` `Q_PROPERTY` (persisted key, default empty) is added to `SettingsTheme` (`src/core/settings_theme.h`/`.cpp`), following the existing pattern used by `skin`, `customThemeColors`, `themeMode`. Empty string means "no custom background" (today's flat-color behavior).

**Alternative considered**: storing the path on `ScreensaverVideoManager` instead, since it already owns screensaver media paths. Rejected because the background choice is a **theme/appearance** concern consumed by 8 unrelated pages, not a screensaver-playback concern — `SettingsTheme` is the correct owner per `SETTINGS.md`'s domain-boundary guidance, and `ScreensaverVideoManager` should not need to know it's being used for anything besides the screensaver.

The path is stored as an **absolute filesystem path** (matching what `getPersonalMediaList()`/the new catalog accessor return), not a catalog ID — this keeps the 8 consuming pages simple (`Image { source: "file:///" + path }`) with no lookup indirection, at the cost of the path becoming stale if that specific file is later deleted (see Risks).

### 2. New read-only accessor on `ScreensaverVideoManager` for locally-cached catalog images
Add `QVariantList getCachedCatalogImages() const` (or similar) alongside the existing `getPersonalMediaList()`, returning `{id, type, filename, path (absolute), bytes, author}` per entry — same shape as `getPersonalMediaList()`'s items so the picker can merge both lists uniformly.

**Iterates `m_cacheIndex` directly, not `m_catalog`.** An earlier version of this method filtered `m_catalog` (the currently-selected category's manifest) down to cached `Image` entries — but `m_catalog` is wholesale *replaced* on every category switch (`setSelectedCategoryId()`), while `m_cacheIndex` (the on-disk download record) accumulates across every category ever visited and is never cleared by a category switch. Filtering on `m_catalog` meant images cached under a previously-selected category silently vanished from the picker the moment the user switched categories, even though the files were still on disk — the picker must reflect *everything actually cached*, not just the active category. Since `m_cacheIndex`'s `CachedVideo` struct doesn't carry a `MediaType` (only `ScreensaverVideoManager::getCachePath()`'s file extension does — `.jpg`/`.jpeg`/`.png` for images, `.mp4` for videos), image/video is now discriminated by the cached file's extension instead of the originating `VideoItem`; `author` is left blank for catalog images since it's no longer available once the owning category's catalog has been replaced (acceptable — the picker doesn't surface author info).

This method is purely additive and read-only: it does not touch `startBackgroundDownload()`, the rate limiter, or any download/eviction logic. Catalog image coverage in the picker simply grows over time as the existing background download job progresses, across whichever categories have ever been selected — this is documented behavior, not a bug, and needs no loading/progress UI in the picker.

**Alternative considered**: force a full catalog download when the picker opens, so it shows the complete stock set immediately. Rejected — this reintroduces exactly the bandwidth spike the rate limiter exists to prevent, and adds a new "downloading catalog…" state to design and test for a feature the user explicitly wants scoped small for v1.

### 3. Picker dialog and thumbnail grid follow existing precedents, not new patterns
- Dialog shell: modeled on `CustomEditorPopup.qml` (modal `Dialog`, `Overlay`-anchored, `Theme.surfaceColor`/`Theme.cardRadius` styling).
- Thumbnail grid: modeled on `EmojiPicker.qml`'s `GridView` delegate (selection ring, `Image`, `MouseArea` → `selected(path)` signal), swapping the SVG/emoji source for background-image thumbnails.
- Image loading discipline: follows `BeanThumbnail.qml` — `asynchronous: true`, capped `sourceSize` (grid-cell resolution, never full image resolution), placeholder until `Image.status === Image.Ready`, `fillMode: Image.PreserveAspectCrop`.
- A "None" tile is included in the grid (clears `backgroundImagePath`) so users can remove a background without a separate control.

No new picker pattern is invented; every piece has a direct precedent already in the codebase.

### 4. Preview reuses `LayoutPreview.qml` via a new optional property
`LayoutPreview.qml`'s current background is a flat `Rectangle { anchors.fill: parent; color: Theme.backgroundColor }` (lines 36-39). Add `property string backgroundImageSource: ""`; when non-empty, an `Image` (same fill/crop conventions as above) renders behind the existing zone reconstruction instead of the flat Rectangle. The picker binds this to the *currently highlighted/tapped* thumbnail (not yet saved to Settings), giving a live "what would this look like" preview without a commit step.

This is deliberately the **same component** already embedded in `SettingsLayoutTab.qml:587` for the layout preview, rather than a second bespoke preview renderer — one live-rendering pipeline serves both features. The preview only ever represents the idle screen; it does not attempt to mock up Beans/Recipes/Steam/Hot Water/Flush/Equipment, which have different, simpler layouts. Accepted simplification, not a gap to fill later.

### 5. Application to the 8 target pages via a small shared background component
Rather than duplicating `background: Rectangle { color: (Settings.theme.backgroundImagePath ? ... : Theme.backgroundColor) } / Image { ... }` eight times, add one small reusable component (`qml/components/ThemedPageBackground.qml`) that each of the 8 pages sets as `background: ThemedPageBackground {}`. It encapsulates: show the background image (`PreserveAspectCrop`, filling the page, capped `sourceSize` at device resolution) when `Settings.theme.backgroundImagePath` is non-empty, else the existing flat `Rectangle`. Centralizing this in one component means any future legibility tweak (e.g. a scrim) is a one-file change, not an eight-file change.

**Alternative considered**: inline the conditional in each of the 8 pages directly. Rejected as needless duplication of non-trivial (image + fallback + sourceSize) logic across 8 files for no benefit — this is exactly the kind of shared logic a small component is for, not premature abstraction, since it's used identically by a known, fixed set of 8 call sites today.

### 6. Bars extend through automatically, gated by a single derived `root` property
`StatusBar.qml` (global — instantiated once in `main.qml`, shown above every page in the app, not just the 8 covered ones) and `BottomBar.qml` (shared back-button bar used by ~25 pages) both need to go semi-transparent on the 8 covered pages, but must stay fully opaque everywhere else — an unconditional transparency change to either would visibly break every uncovered page (revealing whatever content is scrolled underneath a bar that's supposed to be solid).

Added one derived property on `main.qml`'s `root`: `currentPageHasThemedBackground`, true when `Settings.theme.backgroundImagePath` is non-empty *and* `pageStack.currentItem.objectName` matches one of the 8 covered pages' `objectName`s. `StatusBar.qml` and `BottomBar.qml` both read this single property to decide `Qt.rgba(barColor.r, g, b, 0.62)` vs. the normal opaque color. `IdlePage.qml`'s own bespoke bottom nav bar (it predates/bypasses the shared `BottomBar` component) reads `Settings.theme.backgroundImagePath` directly instead, since it's already known to only render on `IdlePage`.

**Alternative considered**: have each of the 8 pages set a flag on `root` in `StackView.onActivated`/`onDeactivated` (mirroring the existing `currentPageTitle` pattern). Rejected — matching on `pageStack.currentItem.objectName` is a single computed property with one list to maintain, versus 8 pages each needing correct set/reset wiring (a forgotten `onDeactivated` reset would leave the flag stuck true after navigating away). `currentPageTitle` gets away with the per-page pattern because a stale/wrong value there is only ever a label glitch, not a persistent visual bug.

**Alternative considered**: an explicit user-facing toggle to opt in/out of the bar scrim. Rejected per explicit direction — automatic-when-set keeps this feature setting-free, matching how a wallpaper naturally extends everywhere without a separate switch.

### 6a. Scope expanded from 8 pages to universal coverage, mid-implementation
After the 8-page version shipped and was tested live, direction changed twice in quick succession: first to add Settings (already folded into "8"), then to extend to every page in the app ("lets extend to all pages, we will have to go back and fix things that need transparency"). Rather than keep threading an `_themedBackgroundPageNames` allowlist through `main.qml`, `ThemedPageBackground` was already applied identically everywhere (`background: ThemedPageBackground {}` on all ~30 pages), so `currentPageHasThemedBackground` simplifies to `Settings.theme.backgroundImagePath.length > 0` — no page list at all. This is a strictly simpler end state than the 8-page version, not added complexity: one allowlist that would have needed maintaining forever (every new page added to the app would silently be "uncovered" until someone remembered to add it) is gone.

### 7. Page-level cards get a centralized translucent variant, not per-file opacity tuning
Once every page had a background image, it became visually clear (screenshots of Flush and Equipment) that large opaque `Rectangle { color: Theme.surfaceColor }` content cards were hiding almost all of the wallpaper — the bar scrim alone wasn't enough. Rather than tune an opacity value per call site, `Theme.qml` gained one derived property, `cardBackgroundColor` (`Qt.rgba(surfaceColor.r, g, b, 0.62)` when a background image is set, else opaque `surfaceColor`) — the same 62% used for the bars, so the whole app reads as one consistent translucency language. Every page-level card's `color: Theme.surfaceColor` became `color: Theme.cardBackgroundColor`; dialogs, popups, toasts, and small buttons/pills were deliberately left on plain `Theme.surfaceColor`.

**Classification rule used across the sweep** (~35 files, ~90 call sites): a `Theme.surfaceColor` fill is a **card** (switch it) if it's a `Rectangle`'s own top-level `color:` sized by `Layout.fill*`/`anchors.fill: parent`/an `implicitHeight` binding — i.e., a panel that's part of the page's normal content flow. It's a **dialog** (leave it) if it appears as `background: Rectangle { color: Theme.surfaceColor; ... }` inside a `Dialog`/`Popup` — those already render above a dimmed `Overlay`, so the wallpaper isn't visually "behind" them the way it is behind page content, and translucency there would fight the dim layer for legibility. It's a **toast** (leave it) if it's sized by `text.implicitWidth`/`implicitHeight` with `anchors.bottom: parent.bottom` and a high `z` — a transient floating notification needs reliable contrast against whatever's beneath it at the moment it appears, not scrim-blended into the page. List/grid item delegates (e.g. a favorites-list row, `RecipeDrinkCard`, `EquipmentCard`, `BagCard`) were treated as cards, matching the page-level-card precedent already set for Recipes' archive tile and the Equipment/Beans grid cards.

**Alternative considered**: leave individual `Theme.surfaceColor` opacity choices to whoever touches each file next. Rejected — that produces a visibly inconsistent app (some cards ghost-transparent, others solid, no rule anyone could point to) and there's no reason a "Flush settings card" and a "Steam settings card" should end up different once the underlying question ("is this a page card or a dialog") is the same everywhere.

**Not done in this pass**: `qml/components/` (~43 files, ~59 occurrences) — shared sub-widgets, many nested *inside* the cards already covered above. Whether an inner widget nested inside an already-translucent parent card should also go translucent (risking a double-blend look) or stay solid as a deliberate inner layer is a genuinely different, less clear-cut question than the page-level sweep — deferred pending a look at how the current level of translucency reads in the running app.

### 8. Recessed/inset controls get a second Theme variant, `insetBackgroundColor` — not reused from `cardBackgroundColor`
Live testing surfaced a second, distinct pattern from Decision 7's cards: `Theme.backgroundColor` (not `surfaceColor`) used as the fill for controls meant to "blend into the flat page" rather than stand out — `StyledTextField`/`SuggestionField`'s `fieldColor` default, `StyledSwitch`'s unchecked track, `StyledTabButton`'s active-tab background, and assorted unselected pills (e.g. the AI Provider tab's unconfigured-provider chips, which is what surfaced this). With a flat page, "blend in" meant matching `Theme.backgroundColor` exactly; with a photo background there's no flat color left to match, so these rendered as opaque blocks sitting on top of the wallpaper — the same symptom as Decision 7's cards, but a different root property (`backgroundColor`, not `surfaceColor`) and a different set of call sites.

Added `Theme.insetBackgroundColor` as a parallel property (same 62% scrim formula, applied to `backgroundColor` instead of `surfaceColor`), rather than reusing `cardBackgroundColor` directly — the two need to independently track their own base color if a future theme ever diverges `backgroundColor` and `surfaceColor` further, and "inset control" and "content card" are conceptually different roles even though today's opacity constant happens to match.

**Per-widget treatment differs, unlike the card sweep's single rule**: text fields and switch tracks keep the scrim (`insetBackgroundColor`) — they're interactive controls whose shape/bounds need to stay visible regardless of the photo underneath. The AI Provider chip specifically was changed to `"transparent"` instead, not `insetBackgroundColor` — it's decorative (no content, just a colored pill), so disappearing into the card behind it (which is itself already translucent) reads better than adding a third layer of tinted scrim on top of a scrim.

**Not done in this pass**: ~15 pages inline their own `Theme.backgroundColor`-as-unselected-pill fallback directly (preset pills on Flush/Hot Water, frame-type pills on the profile editors, etc.) rather than going through `StyledTextField`/`StyledSwitch`/`StyledTabButton`. Fixing the shared components cascades to everywhere that didn't override the default, but these per-file inline pills need the same transparent-vs-scrim judgment call individually — deferred pending a look at how the shared-component fixes read live, same reasoning as the `qml/components/` deferral above.

## Risks / Trade-offs

- **[Risk]** Catalog images shown in the picker today may later be evicted from the local cache (if `ScreensaverVideoManager` ever adds LRU eviction) or a personal image may be deleted via "Clear personal media" — leaving `Settings.theme.backgroundImagePath` pointing at a missing file. → **Mitigation**: the `ThemedPageBackground` component checks `Image.status === Image.Error` and falls back to the flat `Theme.backgroundColor` Rectangle, so a stale path degrades gracefully rather than showing a broken image.
- **[Risk]** Picker grid may look sparse immediately after install or after switching screensaver category, since catalog images are only shown once locally cached. → **Mitigation**: this is documented in-app (a short hint in the picker dialog, e.g. "More stock images appear here as they download") and in `docs/CLAUDE_MD/`, so it reads as expected behavior, not a bug report.
- **[Risk]** A background image with a lot of visual busyness could reduce text/control legibility on pages with dense foreground content — resolved for the common case by `Theme.cardBackgroundColor` (Decision 7), but a 62% scrim over a very bright/busy photo could still be marginal on the densest pages (Settings tabs, Recipe/Profile editors). → **Mitigation**: no per-page opacity overrides built (would reintroduce the "inconsistent app" problem Decision 7 exists to avoid); revisit the single 0.62 constant if real usage shows it's wrong, rather than special-casing pages.
- **[Risk]** The card-vs-dialog-vs-toast classification (Decision 7) was applied by a human/agent reading ~90 call sites, not by a structural distinction the type system enforces — a future new card could be added with plain `Theme.surfaceColor` by habit (copy-pasting an older pattern from before this change) and silently stay opaque. → **Mitigation**: none enforced; worth a mention in a contributor-facing convention note if this recurs.
- **[Trade-off]** Storing an absolute path rather than a catalog/media ID is simpler for the consuming pages but means the setting has no "source" metadata (personal vs. stock) — acceptable since nothing currently needs to distinguish them once chosen.

## Migration Plan

No data migration needed — `backgroundImagePath` defaults to empty, which is exactly today's rendering path (flat `Theme.backgroundColor`, opaque `Theme.surfaceColor` cards). No rollback concerns beyond a normal revert; no server-side or schema changes.

## Open Questions

- Whether the `qml/components/` sub-widget sweep (Decision 7's deferred tail — ~43 files) is worth doing, and if so whether nested-inside-a-card widgets should also go translucent or stay solid as an inner layer.
- Whether the single 0.62 opacity constant holds up across all pages/photos, or ends up needing to vary by context (bars vs. cards vs. some pages being busier than others) — deferred to visual testing.
