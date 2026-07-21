import QtQuick

// Shared `background:` for pages that support a custom background — a built-in colour
// preset (Settings.theme.backgroundPreset) or an image from the screensaver media
// library (Settings.theme.backgroundImagePath) — used app-wide; see
// openspec/changes/add-custom-background/design.md Decision 6a for how coverage expanded
// from an initial 8-page set to universal.
//
// All the drawing lives in BackgroundSurface, which the background chooser's tiles and
// preview also use, so what the chooser shows is what a page actually renders. That
// includes the flat Theme.backgroundColor fallback a page always used before — held
// while an image decodes, and kept when a stored path no longer resolves to a readable
// file, so a stale setting degrades gracefully instead of showing a broken image.
// (deletePersonalMedia()/clearPersonalMedia()/clearCache() in ScreensaverVideoManager
// also proactively clear the setting when its backing file is the one being deleted,
// since this component's own Image.Error fallback isn't visible to the many other call
// sites that read Theme.hasBackgroundImage.)
BackgroundSurface {
}
