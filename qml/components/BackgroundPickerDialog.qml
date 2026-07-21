import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "layout"

// Lets the user pick the app background for the idle screen and the other pages
// ThemedPageBackground covers. Two sections: the built-in colour and pattern presets,
// then images from the screensaver media library (personal uploads plus already-cached
// stock images, never videos).
//
// Presets come first because they always exist. With image caching off and no personal
// uploads this dialog used to open on a single "None" tile plus an apology, which reads
// as a broken feature; now there is always something to choose, and the caching
// explanation belongs to the section it actually describes.
//
// One selection covers both sections — a preset and an image are mutually exclusive, and
// the highlighted candidate only updates the live preview. Nothing is saved until
// "Apply". Modal shell modeled on CustomEditorPopup.qml.
Dialog {
    id: popup

    // Currently highlighted candidate — previewed live but not yet saved. Exactly one of
    // these is non-empty, mirroring the mutual exclusion the setting enforces; both empty
    // is the "None" tile.
    property string candidatePreset: ""
    property string candidatePath: ""
    // Independent of the colour: any pattern over any colour. Ignored while an image is
    // the candidate, where a pattern would fight the photo.
    property string candidatePattern: ""
    // "" | "basic" | "advanced" — the last shot's chart. A THIRD candidate rather than a
    // flavour of preset or image, mirroring the three-way source the setting now records.
    property string candidateShot: ""
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
        candidatePreset = Settings.theme.backgroundPreset
        candidatePath = Settings.theme.backgroundImagePath
        candidateShot = Settings.theme.backgroundSource === "shot"
            ? (Settings.theme.backgroundShotAdvanced ? "advanced" : "basic") : ""
        candidatePattern = Settings.theme.backgroundPattern
        _images = buildImageList()
    }

    readonly property bool _isNoneSelected: candidatePreset.length === 0
        && candidatePath.length === 0 && candidateShot.length === 0

    // A pattern cannot be drawn over a chart — texture on top of data is noise on top of
    // information. The row is DISABLED rather than silently ignored: a control that does
    // nothing when you press it is a bug report waiting to happen. The stored value is left
    // alone, so returning to a colour restores the pattern the user had.
    readonly property bool _patternsApply: candidateShot.length === 0 && candidatePath.length === 0

    // The colour value behind a given preset id, for deriving a readable tile label.
    function _presetColour(id) {
        var list = Settings.theme.backgroundPresets
        for (var i = 0; i < list.length; i++) {
            if (list[i].id === id)
                return list[i].value
        }
        return Theme.backgroundColor
    }

    function selectPreset(id) {
        candidatePreset = id
        candidatePath = ""
        candidateShot = ""
    }

    function selectImage(path) {
        candidatePath = path
        candidatePreset = ""
        candidateShot = ""
    }

    function selectShot(which) {
        candidateShot = which
        candidatePreset = ""
        candidatePath = ""
    }

    // Personal uploads + already-cached stock images only — never triggers a
    // download. Catalog coverage grows over time as the existing rate-limited
    // background download progresses (see hint text below).
    function buildImageList() {
        var result = []

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
        // Order matters only in that each setter clears the other; setting the empty one
        // second would undo the first, so write exactly the one that is chosen.
        if (candidateShot.length > 0)
            Settings.theme.selectShotChartBackground(candidateShot === "advanced")
        else if (candidatePreset.length > 0)
            Settings.theme.backgroundPreset = candidatePreset
        else if (candidatePath.length > 0)
            Settings.theme.backgroundImagePath = candidatePath
        else
            Settings.theme.clearBackground()
        Settings.theme.backgroundPattern = candidatePattern
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

            // Preview on the left, pattern tiles filling the space beside it. The preview is
            // only ~220 wide, so a full-width row for it alone left most of the dialog's
            // widest band empty while the pattern tiles sat below the fold.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(12)

                // Live idle-screen preview — same component the Layout settings tab uses,
                // showing the highlighted (not yet saved) candidate and rendered by the same
                // BackgroundSurface the real page background uses, so the preview cannot
                // drift from the result.
                Rectangle {
                    Layout.alignment: Qt.AlignTop
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
                        backgroundPresetSource: popup.candidatePreset
                        backgroundPatternSource: popup.candidatePattern
                        backgroundShotSource: popup.candidateShot.length > 0
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.scaled(6)

                    // --- Patterns -------------------------------------------------
                    // A second axis rather than a property of each colour, so every colour
                    // can carry every texture without a grid of near-identical tiles.

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("backgroundPicker.section.patterns", "Pattern")
                              + (popup._patternsApply ? "" : " — "
                                 + TranslationManager.translate("backgroundPicker.patternsNotAvailable",
                                       "not used with a picture background"))
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    Flow {
                        enabled: popup._patternsApply
                        opacity: popup._patternsApply ? 1.0 : 0.35
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)

                        Repeater {
                            // "None" first, then the catalogue.
                            model: [{ id: "", nameKey: "backgroundPicker.none", nameFallback: "None" }]
                                   .concat(Settings.theme.backgroundPatterns)

                            Rectangle {
                                id: patternTile
                                required property var modelData

                                readonly property bool isSelected: popup.candidatePattern === modelData.id
                                // Previewed on the colour that is actually selected, so the
                                // tile shows the real combination rather than an abstract swatch.
                                readonly property color _base: popup.candidatePreset.length > 0
                                    ? popup._presetColour(popup.candidatePreset)
                                    : Theme.backgroundColor

                                width: Theme.scaled(96)
                                height: Theme.scaled(64)
                                radius: Theme.scaled(8)
                                color: "transparent"
                                border.color: isSelected ? Theme.primaryColor : Theme.borderColor
                                border.width: isSelected ? 2 : 1
                                clip: true

                                BackgroundSurface {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    presetId: popup.candidatePreset
                                    patternId: patternTile.modelData.id
                                    imagePath: ""
                                    // Every input stated, including the ones that are off.
                                    // BackgroundSurface defaults each source from the LIVE
                                    // setting, so a tile that stays silent about the shot
                                    // chart inherits it — and every colour and pattern in
                                    // the chooser drew the last shot.
                                    shotChart: false
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: Theme.scaled(4)
                                    text: TranslationManager.translate(patternTile.modelData.nameKey,
                                                                       patternTile.modelData.nameFallback)
                                    color: Theme.contrastColorFor(patternTile._base)
                                    font: Theme.captionFont
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    Accessible.ignored: true
                                }

                                SelectedTick { visible: patternTile.isSelected }

                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    accessibleName: TranslationManager.translate(patternTile.modelData.nameKey,
                                                                                 patternTile.modelData.nameFallback)
                                        + (patternTile.isSelected
                                            ? ", " + TranslationManager.translate("accessibility.selected", "selected")
                                            : "")
                                    accessibleItem: patternTile
                                    onAccessibleClicked: popup.candidatePattern = patternTile.modelData.id
                                }
                            }
                }
            }
                }
            }

            ScrollView {
                id: scroller
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: scroller.availableWidth
                    spacing: Theme.scaled(6)

                    // --- Shot ----------------------------------------------------
                    // The last shot's chart. Two entries rather than one that follows the
                    // review page's Advanced toggle: that toggle is used to inspect a single
                    // shot, and it must not repaint the whole app as a side effect.

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("backgroundPicker.section.shot", "Last shot")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)

                        Repeater {
                            model: [
                                { which: "basic",
                                  label: TranslationManager.translate("backgroundPicker.shot.basic", "Last Shot") },
                                { which: "advanced",
                                  label: TranslationManager.translate("backgroundPicker.shot.advanced", "Last Shot (Advanced)") }
                            ]

                            Rectangle {
                                id: shotTile
                                required property var modelData

                                readonly property bool isSelected: popup.candidateShot === modelData.which

                                width: Theme.scaled(106)
                                height: Theme.scaled(86)
                                radius: Theme.scaled(8)
                                color: "transparent"
                                border.color: isSelected ? Theme.primaryColor : Theme.borderColor
                                border.width: isSelected ? 2 : 1
                                clip: true

                                // The real renderer, so a tile is a true miniature of the
                                // page — the same already-rendered image, scaled.
                                BackgroundSurface {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    presetId: ""
                                    patternId: ""
                                    imagePath: ""
                                    shotChart: true
                                }

                                // Says WHY the tile is blank rather than leaving the user to
                                // wonder whether the feature is broken.
                                Text {
                                    anchors.centerIn: parent
                                    width: parent.width - Theme.scaled(12)
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    visible: !LastShotChartSource.hasShot && LastShotChartSource.ready
                                    text: TranslationManager.translate("backgroundPicker.shot.noShot",
                                              "No shots yet — this fills in after your first shot")
                                    color: Theme.textSecondaryColor
                                    font: Theme.captionFont
                                    Accessible.ignored: true
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: Theme.scaled(4)
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    text: shotTile.modelData.label
                                    color: Theme.contrastColorFor(Theme.backgroundColor)
                                    font: Theme.captionFont
                                    Accessible.ignored: true
                                }

                                SelectedTick { visible: shotTile.isSelected }

                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    accessibleName: shotTile.modelData.label
                                        + (shotTile.isSelected
                                            ? ", " + TranslationManager.translate("accessibility.selected", "selected")
                                            : "")
                                    accessibleItem: shotTile
                                    onAccessibleClicked: popup.selectShot(shotTile.modelData.which)
                                }
                            }
                        }
                    }

                    // --- Colours -------------------------------------------------

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("backgroundPicker.section.presets", "Colours")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)

                        // "None" — clears both preset and image.
                        Rectangle {
                            width: Theme.scaled(106)
                            height: Theme.scaled(86)
                            radius: Theme.scaled(8)
                            color: Theme.backgroundColor
                            border.color: popup._isNoneSelected ? Theme.primaryColor : Theme.borderColor
                            border.width: popup._isNoneSelected ? 2 : 1
                            clip: true

                            Text {
                                anchors.centerIn: parent
                                text: TranslationManager.translate("backgroundPicker.none", "None")
                                color: Theme.textSecondaryColor
                                font: Theme.captionFont
                                Accessible.ignored: true
                            }

                            SelectedTick { visible: popup._isNoneSelected }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate("backgroundPicker.none", "None")
                                    + (popup._isNoneSelected
                                        ? ", " + TranslationManager.translate("accessibility.selected", "selected")
                                        : "")
                                accessibleItem: parent
                                onAccessibleClicked: popup.selectPreset("")
                            }
                        }

                        Repeater {
                            model: Settings.theme.backgroundPresets

                            Rectangle {
                                id: presetTile
                                required property var modelData

                                readonly property bool isSelected: popup.candidatePreset === modelData.id

                                width: Theme.scaled(106)
                                height: Theme.scaled(86)
                                radius: Theme.scaled(8)
                                color: "transparent"
                                border.color: isSelected ? Theme.primaryColor : Theme.borderColor
                                border.width: isSelected ? 2 : 1
                                clip: true

                                // The real renderer, so a tile is a true miniature of the page.
                                BackgroundSurface {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    presetId: presetTile.modelData.id
                                    patternId: popup.candidatePattern
                                    imagePath: ""
                                    shotChart: false
                                }

                                // Unlike the photo tiles, a preset has a real name to show.
                                Text {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: Theme.scaled(4)
                                    text: TranslationManager.translate(presetTile.modelData.nameKey,
                                                                       presetTile.modelData.nameFallback)
                                    // Derived from THIS tile's colour, not from Theme.textColor.
                                    // The grid shows every background at once, so the
                                    // one global text colour is wrong on most of them — it left
                                    // every light tile captioned in white on near-white.
                                    color: presetTile.modelData.textOn
                                    font: Theme.captionFont
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    Accessible.ignored: true
                                }

                                SelectedTick { visible: presetTile.isSelected }

                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    accessibleName: TranslationManager.translate(presetTile.modelData.nameKey,
                                                                                 presetTile.modelData.nameFallback)
                                        + (presetTile.isSelected
                                            ? ", " + TranslationManager.translate("accessibility.selected", "selected")
                                            : "")
                                    accessibleItem: presetTile
                                    onAccessibleClicked: popup.selectPreset(presetTile.modelData.id)
                                }
                            }
                        }
                    }

                    // --- Images ---------------------------------------------------

                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.scaled(6)
                        text: TranslationManager.translate("backgroundPicker.section.images", "Images")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("backgroundPicker.hint",
                            "More stock images appear here over time as they finish downloading in the background.")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.Wrap
                    }

                    // No images at all — say why and what to do about it, rather than
                    // leaving an unexplained empty section. Stock images can never appear
                    // while caching is off, since the background download that populates
                    // the local cache never runs (see ScreensaverManager.cacheEnabled).
                    RowLayout {
                        Layout.fillWidth: true
                        visible: popup._images.length === 0 && !ScreensaverManager.cacheEnabled
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

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.scaled(6)

                        Repeater {
                            model: popup._images

                            Rectangle {
                                id: imageTile
                                required property var modelData
                                required property int index

                                readonly property bool isSelected: popup.candidatePath === modelData.path

                                width: Theme.scaled(106)
                                height: Theme.scaled(86)
                                radius: Theme.scaled(8)
                                color: "transparent"
                                border.color: isSelected ? Theme.primaryColor : Theme.borderColor
                                border.width: isSelected ? 2 : 1
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    visible: status === Image.Ready
                                    source: "file:///" + imageTile.modelData.path
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    sourceSize.width: Theme.scaled(106)
                                    sourceSize.height: Theme.scaled(86)
                                    Accessible.ignored: true
                                }

                                SelectedTick { visible: imageTile.isSelected }

                                AccessibleMouseArea {
                                    anchors.fill: parent
                                    // Photos carry no usable label (author is blank for stock
                                    // images, filenames are cache-generated) — position-in-list
                                    // is the only thing that lets a screen reader tell otherwise-
                                    // identical "Background image" tiles apart. Presets above do
                                    // have names, and use them.
                                    accessibleName: TranslationManager.translate(
                                            "backgroundPicker.accessible.thumbnail", "Background image %1 of %2")
                                            .arg(imageTile.index + 1).arg(popup._images.length)
                                        + (imageTile.isSelected
                                            ? ", " + TranslationManager.translate("accessibility.selected", "selected")
                                            : "")
                                    accessibleItem: imageTile
                                    onAccessibleClicked: popup.selectImage(imageTile.modelData.path)
                                }
                            }
                        }
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

    // The selected-tile badge, identical on every tile in both sections.
    component SelectedTick: Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.scaled(4)
        width: Theme.scaled(18)
        height: Theme.scaled(18)
        radius: width / 2
        color: Theme.primaryColor
        Accessible.ignored: true

        ColoredIcon {
            anchors.centerIn: parent
            source: "qrc:/icons/tick.svg"
            iconWidth: Theme.scaled(12)
            iconHeight: Theme.scaled(12)
            iconColor: Theme.primaryContrastColor
        }
    }
}
