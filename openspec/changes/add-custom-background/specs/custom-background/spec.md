## ADDED Requirements

### Requirement: Global background image setting
The app SHALL provide a single global setting holding an optional background image path, stored on `Settings.theme`. When empty (the default), every page SHALL render its existing flat `Theme.backgroundColor` background, unchanged from current behavior. When set, the same image SHALL be used regardless of whether the app is in light or dark theme mode.

#### Scenario: Default state is unchanged
- **WHEN** a user has never set a background image
- **THEN** all pages render exactly as they do today (flat `Theme.backgroundColor`)

#### Scenario: Background applies in both theme modes
- **WHEN** a background image is set and the user switches between light and dark theme mode
- **THEN** the same background image continues to render on every page in both modes

### Requirement: Background control in Theme Mode settings card
The Theme Mode card in Settings > Machine SHALL offer a "Background" control that opens the background picker dialog.

#### Scenario: Opening the picker
- **WHEN** the user taps the Background control in the Theme Mode card
- **THEN** the background picker dialog opens

### Requirement: Background picker image source
The background picker SHALL present a grid of image thumbnails drawn from two sources: all personal (web-uploaded) screensaver images, and stock/catalog screensaver images that are already downloaded and cached locally on the device, across every screensaver category the device has ever downloaded — not just the currently selected category. The picker SHALL NOT trigger a download of catalog images that are not yet cached, and SHALL NOT show video items from either source.

#### Scenario: Personal uploads always available
- **WHEN** the user has uploaded personal images via the screensaver web upload page
- **THEN** all of them appear as thumbnails in the background picker

#### Scenario: Only cached catalog images appear
- **WHEN** the stock/catalog screensaver library contains images that have not yet been downloaded to the device by the background download process
- **THEN** those images do not appear in the picker until they have been downloaded

#### Scenario: Cached images persist across category switches
- **WHEN** the user previously selected screensaver category A (downloading some images) and has since switched to category B
- **THEN** the picker still shows the images cached under category A, in addition to any cached under category B

#### Scenario: Videos are excluded
- **WHEN** the screensaver media library contains video items (personal or catalog)
- **THEN** none of them appear as selectable thumbnails in the background picker

### Requirement: Live idle-screen preview while picking
The background picker SHALL show a live, scaled-down preview of the idle screen (its actual configured zones and widgets) composited over the currently highlighted/selected thumbnail, updating as the user taps different thumbnails, before any choice is saved.

#### Scenario: Preview updates on selection
- **WHEN** the user taps a thumbnail in the picker
- **THEN** the preview immediately shows the idle screen's real zone layout with that image as the background

#### Scenario: Preview reflects the user's actual idle layout
- **WHEN** the user has customized their idle-screen zones (e.g. via the Layout settings tab)
- **THEN** the picker's preview shows those same customized zones, not a generic mockup

### Requirement: Clearing the background
The background picker SHALL offer a way to clear the background selection, restoring the flat `Theme.backgroundColor` everywhere.

#### Scenario: User clears a previously set background
- **WHEN** the user selects "None" (or equivalent) in the picker and confirms
- **THEN** `Settings.theme`'s background image path is cleared and every page reverts to the flat background color

### Requirement: Background applies to every page
When a background image is set, it SHALL render on every page in the app — there is no allowlist of covered vs. uncovered pages.

#### Scenario: Background shown on any page
- **WHEN** a background image is set
- **THEN** navigating to any page in the app (idle, brew pages, settings, editors, history, dialing assistant, etc.) shows that image as the page background

### Requirement: Background extends behind the shared chrome bars
Whenever a background image is active, the top status bar and the bottom bar (whether the shared `BottomBar` component or `IdlePage`'s own bottom nav bar) SHALL render as a semi-transparent scrim (retaining their normal theme color, at reduced opacity) instead of fully opaque, so the background image extends behind them rather than visibly stopping at each bar's edge. With no background image set, both bars SHALL remain fully opaque as today. This behavior is automatic — there is no separate setting to enable or disable it.

#### Scenario: Bars go semi-transparent when a background is set
- **WHEN** a background image is set and the user is on any page
- **THEN** the top status bar and the bottom bar render as a semi-transparent scrim, with the background image visible behind them

#### Scenario: Bars stay opaque with no background set
- **WHEN** no background image is set
- **THEN** the top status bar and bottom bars on all pages render fully opaque exactly as before this feature

### Requirement: Page-level content cards extend the background through translucency
Whenever a background image is active, page-level content cards (the `Rectangle { color: Theme.surfaceColor }` panel convention used throughout the app) SHALL render via `Theme.cardBackgroundColor`, a semi-transparent variant of the card's normal color, so the background image remains visible behind card content rather than being hidden by large opaque panels. Dialog and popup backgrounds, toast/transient notifications, and small buttons SHALL remain fully opaque regardless of whether a background image is set, since they don't sit on top of page content the same way (dialogs render above a dimmed `Overlay`; toasts and buttons need reliable contrast at all times). With no background image set, all cards SHALL remain fully opaque as today.

#### Scenario: Page cards go semi-transparent when a background is set
- **WHEN** a background image is set
- **THEN** page-level content cards (e.g. the Flush "Settings frame" card, an Equipment card, a Settings tab card) render as a semi-transparent scrim of their normal color

#### Scenario: Dialogs and toasts stay opaque
- **WHEN** a background image is set
- **THEN** modal dialog/popup backgrounds and toast notifications continue to render fully opaque, unaffected by the setting

#### Scenario: Cards stay opaque with no background set
- **WHEN** no background image is set
- **THEN** all page-level cards render fully opaque exactly as before this feature

### Requirement: Graceful fallback for a missing background file
If the path stored for the background image no longer resolves to a readable file (e.g. the source personal media was deleted), pages SHALL fall back to the flat `Theme.backgroundColor` rather than showing a broken image.

#### Scenario: Selected personal image is later deleted
- **WHEN** a background image sourced from personal media is deleted via "Clear personal media" (or similar) after being set as the background
- **THEN** pages render the flat `Theme.backgroundColor` instead of a broken image
