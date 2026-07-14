import QtQuick
import QtQuick.Window
import Decenza

// Shared `background:` for pages that support a custom background image
// (Settings.theme.backgroundImagePath) — used app-wide; see
// openspec/changes/add-custom-background/design.md Decision 6a for how
// coverage expanded from an initial 8-page set to universal. Shows the
// image when set, falling back to the flat Theme.backgroundColor a page
// always used before — including when the stored path no longer resolves
// to a readable file, so a stale setting degrades gracefully instead of
// showing a broken image. (deletePersonalMedia()/clearPersonalMedia()/
// clearCache() in ScreensaverVideoManager also proactively clear the
// setting when its backing file is the one being deleted, since this
// component's own Image.Error fallback isn't visible to the ~70 other
// call sites that just check backgroundImagePath.length > 0.)
Item {
    id: root

    readonly property bool _hasImage: Settings.theme.backgroundImagePath.length > 0

    Rectangle {
        anchors.fill: parent
        color: Theme.backgroundColor
        // Stay visible until the image is actually decoded (not just "not yet
        // errored") — asynchronous: true below means there's a real window where
        // status is Loading, and painting nothing during that window flashes
        // transparent/window-color instead of holding the flat fallback.
        visible: !root._hasImage || bgImage.status !== Image.Ready
    }

    Image {
        id: bgImage
        anchors.fill: parent
        visible: root._hasImage && status === Image.Ready
        source: root._hasImage ? "file:///" + Settings.theme.backgroundImagePath : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.width: Screen.width
        sourceSize.height: Screen.height
        Accessible.ignored: true
    }
}
