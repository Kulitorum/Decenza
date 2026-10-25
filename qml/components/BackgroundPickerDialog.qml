// The four tile Repeater delegates read this file's `popup` id; Bound makes it
// statically resolvable. All four already declare every injected role they use
// (`modelData`, and `index` on the image grid) required, so Bound cannot break role
// injection here.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

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
DecenzaDialog {
    id: popup

    // Currently highlighted candidate — previewed live but not yet saved. Exactly one of
    // these is non-empty, mirroring the mutual exclusion the setting enforces; both empty
    // is the "None" tile.
    property string candidatePreset: ""
    property string candidateImageId: ""
    // Independent of the colour: any pattern over any colour. Ignored while an image is
    // the candidate, where a pattern would fight the photo.
    property string candidatePattern: ""
    // "" | "basic" | "advanced" — the last shot's chart. A THIRD candidate rather than a
    // flavour of preset or image, mirroring the three-way source the setting now records.
    property string candidateShot: ""
    property var _images: []
    property string pendingRequestId: ""
    property var baselineRevision: 0
    property var _queuedImageTiles: []
    property var _activeImageTiles: []
    property bool _acceptingImageLoads: false

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
        baselineRevision = ThemeManagementService.revision
        _acceptingImageLoads = true
        candidatePreset = ThemeManagementService.backgroundPreset
        candidateImageId = ThemeManagementService.backgroundImageId
        candidateShot = ThemeManagementService.backgroundSource === "shot"
            ? (ThemeManagementService.backgroundShotAdvanced ? "advanced" : "basic") : ""
        candidatePattern = ThemeManagementService.backgroundPattern
        _images = buildImageList()
        Qt.callLater(pumpImageLoads)
        // The singleton only loads while the shot background is SELECTED, so for anyone
        // opening this chooser to consider it, ready stays false and both shot tiles were
        // blank rectangles with a blank caption. Ask for the load explicitly; it does not
        // render anything (the renderer's Loader stays inactive until Apply).
        LastShotChartSource.ensureLoaded()
    }
    onAboutToHide: {
        _acceptingImageLoads = false
        cancelImageLoads()
        _images = []
    }

    readonly property bool _isNoneSelected: candidatePreset.length === 0
        && candidateImageId.length === 0 && candidateShot.length === 0

    // A pattern cannot be drawn over a chart — texture on top of data is noise on top of
    // information. The row is DISABLED rather than silently ignored: a control that does
    // nothing when you press it is a bug report waiting to happen. The stored value is left
    // alone, so returning to a colour restores the pattern the user had.
    readonly property bool _patternsApply: candidateShot.length === 0 && candidateImageId.length === 0

    // The colour value behind a given preset id, for deriving a readable tile label.
    function _presetColour(id) {
        var list = ThemeManagementService.backgroundPresets
        for (let i = 0; i < list.length; i++) {
            if (list[i].id === id)
                return list[i].value
        }
        return Theme.backgroundColor
    }

    function selectPreset(id) {
        candidatePreset = id
        candidateImageId = ""
        candidateShot = ""
    }

    function selectImage(imageId) {
        candidateImageId = imageId
        candidatePreset = ""
        candidateShot = ""
    }

    function selectShot(which) {
        candidateShot = which
        candidatePreset = ""
        candidateImageId = ""
    }

    // The host projects only cached, picker-safe image IDs and labels.
    function buildImageList() {
        return ThemeManagementService.backgroundImages
    }

    // The preview endpoint intentionally caps host-side image work at two jobs.
    // Keep each client to one request at a time; the second slot remains available
    // to another client or the live preview. GridView bounds how many tiles exist.
    function enqueueImageLoad(tile) {
        if (!_acceptingImageLoads || !tile || !tile.imageId || tile.imageLoadStarted
                || _queuedImageTiles.indexOf(tile) >= 0
                || _activeImageTiles.indexOf(tile) >= 0)
            return
        _queuedImageTiles.push(tile)
        pumpImageLoads()
    }

    function pumpImageLoads() {
        while (_acceptingImageLoads && _activeImageTiles.length < 1
                && _queuedImageTiles.length > 0) {
            const tile = _queuedImageTiles.shift()
            if (!tile || !tile.imageId || tile.imageLoadStarted)
                continue
            _activeImageTiles.push(tile)
            tile.imageLoadStarted = true
        }
    }

    function finishImageLoad(tile, keepLoaded) {
        const index = _activeImageTiles.indexOf(tile)
        if (index < 0)
            return
        _activeImageTiles.splice(index, 1)
        if (!keepLoaded)
            tile.imageLoadStarted = false
        pumpImageLoads()
    }

    function cancelImageLoad(tile) {
        let index = _queuedImageTiles.indexOf(tile)
        if (index >= 0)
            _queuedImageTiles.splice(index, 1)
        index = _activeImageTiles.indexOf(tile)
        if (index >= 0)
            _activeImageTiles.splice(index, 1)
        if (tile)
            tile.resetImageLoad()
        pumpImageLoads()
    }

    function cancelImageLoads() {
        const queued = _queuedImageTiles.slice()
        const active = _activeImageTiles.slice()
        _queuedImageTiles = []
        _activeImageTiles = []
        for (const tile of queued)
            if (tile) tile.resetImageLoad()
        for (const tile of active)
            if (tile) tile.resetImageLoad()
    }

    function chooseAndClose() {
        if (!ThemeManagementService.ready || pendingRequestId.length > 0) return
        let source = "none"
        if (candidateShot.length > 0) source = "shot"
        else if (candidatePreset.length > 0) source = "colour"
        else if (candidateImageId.length > 0) source = "image"
        const id = ThemeManagementService.newRequestId()
        pendingRequestId = id
        ThemeManagementService.executeSetting("setBackgroundSelection", {
            source: source,
            presetId: candidatePreset,
            imageId: candidateImageId,
            shotAdvanced: candidateShot === "advanced",
            patternId: candidatePattern
        }, baselineRevision, id)
    }

    Connections {
        target: ThemeManagementService
        function onOperationCompleted(requestId, operation, success, revision, result, reason) {
            if (requestId !== popup.pendingRequestId) return
            popup.pendingRequestId = ""
            if (!success) {
                if (reason === "conflict") popup.baselineRevision = revision
                backgroundError.show(reason === "conflict"
                    ? TranslationManager.translate("settings.themes.conflict", "Themes changed while this action was open. Try again.")
                    : reason)
            } else popup.close()
        }
    }

    StatusToast { id: backgroundError; error: true }

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
                        backgroundImageSource: ScreensaverService.backgroundImageUrl(popup.candidateImageId)
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
                                   .concat(ThemeManagementService.backgroundPatterns)

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
                                    imageSource: ""
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

                                // Only ONE render exists at a time — the one for the entry
                                // currently applied — so a tile may only show it when it is
                                // that entry. Binding both tiles to it made "Last Shot" and
                                // "Last Shot (Advanced)" identical pictures, which is worse
                                // than showing none: the wrong one claims to be a preview of
                                // a curve set it is not.
                                readonly property bool showsLiveRender:
                                    LastShotChartSource.imageSource.length > 0
                                    && ThemeManagementService.backgroundSource === "shot"
                                    && ThemeManagementService.backgroundShotAdvanced
                                       === (shotTile.modelData.which === "advanced")

                                BackgroundSurface {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    presetId: ""
                                    patternId: ""
                                    imageSource: ""
                                    shotChart: shotTile.showsLiveRender
                                }

                                // Says WHY a tile is blank rather than leaving the user to
                                // wonder whether the feature is broken — separately for "you
                                // have no shots" and "this variant has not been drawn yet",
                                // because they call for different actions.
                                Text {
                                    anchors.centerIn: parent
                                    width: parent.width - Theme.scaled(12)
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    visible: !shotTile.showsLiveRender
                                    text: !LastShotChartSource.ready
                                        ? ""
                                        : (!LastShotChartSource.hasShot
                                            ? TranslationManager.translate("backgroundPicker.shot.noShot",
                                                  "No shots yet — this fills in after your first shot")
                                            : TranslationManager.translate("backgroundPicker.shot.notDrawn",
                                                  "Apply to see your last shot here"))
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
                            model: ThemeManagementService.backgroundPresets

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
                                    imageSource: ""
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
                    // the local cache never runs (see ThemeManagementService.backgroundImageCacheEnabled).
                    RowLayout {
                        Layout.fillWidth: true
                        visible: popup._images.length === 0 && !ThemeManagementService.backgroundImageCacheEnabled
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
                            text: TranslationManager.translate("backgroundPicker.warning.cachingDisabledSharedUpload",
                                "Screensaver image caching is turned off, so no stock images have downloaded. In Settings → Screensaver, turn on caching for stock images or choose Upload Media to add your own.")
                            color: Theme.warningColor
                            font: Theme.captionFont
                            wrapMode: Text.Wrap
                        }
                    }

                    GridView {
                        id: imageGrid
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, Theme.scaled(300))
                        Layout.minimumHeight: Math.min(contentHeight, Theme.scaled(90))
                        visible: popup._images.length > 0
                        clip: true
                        interactive: contentHeight > height
                        boundsBehavior: Flickable.StopAtBounds
                        cellWidth: Theme.scaled(112)
                        cellHeight: Theme.scaled(92)
                        cacheBuffer: Theme.scaled(92)
                        model: popup._images
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            id: imageTile
                            required property var modelData
                            required property int index
                            readonly property string imageId: modelData.id
                            property bool imageLoadStarted: false
                            property int retryCount: 0

                            width: imageGrid.cellWidth - Theme.scaled(6)
                            height: imageGrid.cellHeight - Theme.scaled(6)
                            radius: Theme.scaled(8)
                            color: "transparent"
                            border.color: isSelected ? Theme.primaryColor : Theme.borderColor
                            border.width: isSelected ? 2 : 1
                            clip: true
                            readonly property bool isSelected: popup.candidateImageId === imageId

                            function resetImageLoad() {
                                retryTimer.stop()
                                imageLoadStarted = false
                                retryCount = 0
                            }

                            Component.onCompleted: popup.enqueueImageLoad(imageTile)
                            Component.onDestruction: popup.cancelImageLoad(imageTile)

                            Image {
                                id: thumbnail
                                anchors.fill: parent
                                anchors.margins: 1
                                visible: status === Image.Ready
                                source: imageTile.imageLoadStarted
                                    ? ScreensaverService.backgroundImageUrl(imageTile.imageId) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                sourceSize.width: Theme.scaled(106)
                                sourceSize.height: Theme.scaled(86)
                                Accessible.ignored: true
                                onStatusChanged: {
                                    if (status === Image.Ready) {
                                        retryTimer.stop()
                                        popup.finishImageLoad(imageTile, true)
                                    } else if (status === Image.Error && imageTile.imageLoadStarted) {
                                        popup.finishImageLoad(imageTile, false)
                                        if (imageTile.retryCount < 3) {
                                            imageTile.retryCount++
                                            retryTimer.interval = 250 * (1 << (imageTile.retryCount - 1))
                                            retryTimer.restart()
                                        }
                                    }
                                }
                            }

                            Timer {
                                id: retryTimer
                                repeat: false
                                onTriggered: popup.enqueueImageLoad(imageTile)
                            }

                            SelectedTick { visible: imageTile.isSelected }

                            AccessibleMouseArea {
                                anchors.fill: parent
                                accessibleName: TranslationManager.translate(
                                        "backgroundPicker.accessible.thumbnail", "Background image %1 of %2")
                                        .arg(imageTile.index + 1).arg(popup._images.length)
                                    + (imageTile.isSelected
                                        ? ", " + TranslationManager.translate("accessibility.selected", "selected")
                                        : "")
                                accessibleItem: imageTile
                                onAccessibleClicked: popup.selectImage(imageTile.imageId)
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
                    enabled: ThemeManagementService.ready && popup.pendingRequestId.length === 0
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
