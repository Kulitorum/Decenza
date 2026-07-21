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
    id: pageBackground

    // Set by pages that draw a graph of their own. The last-shot chart is the one background
    // that competes with page CONTENT rather than merely sitting behind it: a live shot drawn
    // over the previous shot's chart is two sets of curves in the same colours at different
    // scales, and neither is readable. Those pages fall back to the plain background.
    //
    // Opt-IN per page rather than a list of page names here, so a new graph page is a one-line
    // change at the page — and so the failure mode is a page that forgot to say so, which is
    // visible, rather than a name that drifted, which is not.
    //
    // Only the shot chart is suppressed. A colour, a pattern or a photo sits behind a graph
    // perfectly well, and turning those off would take away a choice for no reason.
    property bool suppressShotChart: false
    shotChart: !suppressShotChart && Settings.theme.backgroundSource === "shot"

    // The PAGE background is the one surface whose size the shot chart should be rendered
    // at. The chooser's tiles and preview are also BackgroundSurfaces, and they are small —
    // if they reported their size the wallpaper would be re-rendered at thumbnail
    // resolution every time the chooser opened.
    onWidthChanged: _reportSize()
    onHeightChanged: _reportSize()
    Component.onCompleted: _reportSize()

    function _reportSize() {
        if (width > 0 && height > 0) {
            LastShotChartSource.targetWidth = width
            LastShotChartSource.targetHeight = height
        }
    }
}
