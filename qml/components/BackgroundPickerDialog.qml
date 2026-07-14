import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "."
import "layout"

// Lets the user pick a background image (from the screensaver media library —
// personal uploads plus already-cached stock images, never videos) for the
// idle screen and the other pages ThemedPageBackground covers. Tapping a
// thumbnail only updates the live preview; nothing is saved until "Choose".
// Modal shell modeled on CustomEditorPopup.qml; thumbnail grid modeled on
// EmojiPicker.qml's delegate.
Dialog {
    id: popup

    // Currently highlighted thumbnail — previewed live but not yet saved.
    // "" means the "None" tile (clears the background).
    property string candidatePath: ""
    property var _images: []

    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: Theme.spacingSmall

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Theme.spacingSmall
    width: parent.width - Theme.spacingSmall * 2
    height: parent.height * 0.85

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    onAboutToShow: {
        candidatePath = Settings.theme.backgroundImagePath
        _images = buildImageList()
    }

    // Personal uploads + already-cached stock images only — never triggers a
    // download. Catalog coverage grows over time as the existing rate-limited
    // background download progresses (see hint text below).
    function buildImageList() {
        var result = [{ path: "", key: "none" }]

        var personal = ScreensaverManager.getPersonalMediaList()
        for (var i = 0; i < personal.length; i++) {
            if (personal[i].type === "image")
                result.push({ path: personal[i].path, key: "p" + personal[i].id })
        }

        var cached = ScreensaverManager.getCachedCatalogImages()
        for (var j = 0; j < cached.length; j++) {
            result.push({ path: cached[j].path, key: "c" + cached[j].id })
        }

        return result
    }

    function chooseAndClose() {
        Settings.theme.backgroundImagePath = candidatePath
        popup.close()
    }

    contentItem: Item {
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.scaled(4)
            spacing: Theme.scaled(6)

            Text {
                text: TranslationManager.translate("backgroundPicker.title", "Background")
                color: Theme.textColor
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(18)
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: TranslationManager.translate("backgroundPicker.hint",
                    "More stock images appear here over time as they finish downloading in the background.")
                color: Theme.textSecondaryColor
                font: Theme.captionFont
                wrapMode: Text.Wrap
            }

            // Only the "None" tile is in the list — tell the user why, and
            // what to do about it, rather than leaving an unexplained empty
            // grid. Stock images can never appear here while caching is off,
            // since the background download that populates the local cache
            // never runs (see ScreensaverManager.cacheEnabled).
            RowLayout {
                Layout.fillWidth: true
                visible: popup._images.length <= 1 && !ScreensaverManager.cacheEnabled
                spacing: Theme.scaled(8)

                ColoredIcon {
                    source: "qrc:/icons/warning.svg"
                    iconWidth: Theme.scaled(18)
                    iconHeight: Theme.scaled(18)
                    iconColor: Theme.warningColor
                    Layout.alignment: Qt.AlignTop
                }

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("backgroundPicker.warning.cachingDisabled",
                        "Screensaver image caching is turned off, so no stock images have downloaded. Turn on caching in Settings → Screensaver to start downloading images automatically, or upload your own via the web interface.")
                    color: Theme.warningColor
                    font: Theme.captionFont
                    wrapMode: Text.Wrap
                }
            }

            // Live idle-screen preview — same component the Layout settings
            // tab uses, now showing the highlighted (not yet saved) candidate.
            // Deliberately small: this dialog's main job is the thumbnail
            // grid below, which needs most of the vertical space so it
            // doesn't force scrolling to see a second row of images on
            // shorter screens.
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Theme.scaled(220)
                Layout.preferredHeight: Theme.scaled(138)
                color: Theme.backgroundColor
                radius: Theme.cardRadius
                border.color: Theme.borderColor
                border.width: 1
                clip: true

                LayoutPreview {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(4)
                    backgroundImageSource: popup.candidatePath
                }
            }

            GridView {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                cellWidth: Theme.scaled(112)
                cellHeight: Theme.scaled(92)
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {
                    policy: grid.contentHeight > grid.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                }

                model: popup._images

                delegate: Rectangle {
                    id: tile
                    required property var modelData

                    readonly property bool isNone: modelData.path.length === 0
                    readonly property bool isSelected: popup.candidatePath === modelData.path

                    width: grid.cellWidth - Theme.scaled(6)
                    height: grid.cellHeight - Theme.scaled(6)
                    radius: Theme.scaled(8)
                    color: isNone ? Theme.backgroundColor : "transparent"
                    border.color: isSelected ? Theme.primaryColor : Theme.borderColor
                    border.width: isSelected ? 2 : 1
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        visible: !tile.isNone && status === Image.Ready
                        source: tile.isNone ? "" : "file:///" + tile.modelData.path
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: grid.cellWidth
                        sourceSize.height: grid.cellHeight
                        Accessible.ignored: true
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: tile.isNone
                        text: TranslationManager.translate("backgroundPicker.none", "None")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        Accessible.ignored: true
                    }

                    Rectangle {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.scaled(4)
                        width: Theme.scaled(18)
                        height: Theme.scaled(18)
                        radius: width / 2
                        color: Theme.primaryColor
                        visible: tile.isSelected
                        Accessible.ignored: true

                        ColoredIcon {
                            anchors.centerIn: parent
                            source: "qrc:/icons/tick.svg"
                            iconWidth: Theme.scaled(12)
                            iconHeight: Theme.scaled(12)
                            iconColor: Theme.primaryContrastColor
                        }
                    }

                    AccessibleMouseArea {
                        anchors.fill: parent
                        accessibleName: tile.isNone
                            ? TranslationManager.translate("backgroundPicker.none", "None")
                            : TranslationManager.translate("backgroundPicker.accessible.thumbnail", "Background image")
                        accessibleItem: tile
                        onAccessibleClicked: popup.candidatePath = tile.modelData.path
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.button.cancel", "Cancel")
                    onClicked: popup.close()
                }

                AccessibleButton {
                    primary: true
                    text: TranslationManager.translate("common.button.apply", "Apply")
                    accessibleName: TranslationManager.translate("backgroundPicker.accessible.choose", "Apply background")
                    onClicked: popup.chooseAndClose()
                }
            }
        }
    }
}
