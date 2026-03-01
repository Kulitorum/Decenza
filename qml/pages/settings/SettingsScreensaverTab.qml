import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: screensaverTab

    // Track pending screensaver type change for dialog flow
    property string pendingScreensaverType: ""

    // Dialog to offer clearing video cache when switching away from videos
    Dialog {
        id: clearCacheDialog
        modal: true
        anchors.centerIn: parent
        width: Theme.scaled(400)
        padding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.screensaver.clearCacheTitle", "Clear Video Cache?")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(15)

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.screensaver.clearCacheMessage",
                        "You have %1 MB of cached videos. Would you like to delete them to free up space?").arg(
                        (ScreensaverManager.cacheUsedBytes / 1024 / 1024).toFixed(0))
                    color: Theme.textColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: TranslationManager.translate("settings.screensaver.clearCacheWarning",
                        "Note: If you switch back to video screensaver later, videos will re-download slowly (one every 3 minutes) to conserve bandwidth. Images will download normally.")
                    color: Theme.warningColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.screensaver.keepCache", "Keep Videos")
                        accessibleName: "Keep cached videos"
                        onClicked: {
                            // Apply type change without clearing cache
                            ScreensaverManager.screensaverType = screensaverTab.pendingScreensaverType
                            clearCacheDialog.close()
                        }
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.screensaver.clearCache", "Delete Videos")
                        accessibleName: "Delete cached videos"
                        onClicked: {
                            // Clear cache with rate limiting, then apply type change
                            ScreensaverManager.clearCacheWithRateLimit()
                            ScreensaverManager.screensaverType = screensaverTab.pendingScreensaverType
                            clearCacheDialog.close()
                        }
                        background: Rectangle {
                            implicitHeight: Theme.scaled(36)
                            implicitWidth: Theme.scaled(120)
                            radius: Theme.scaled(6)
                            color: parent.down ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                        }
                        contentItem: Text {
                            text: parent.text
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        onRejected: {
            // User cancelled - revert combobox to current type
            typeComboBox.currentIndex = ScreensaverManager.screensaverType === "videos" ? 1 :
                                        ScreensaverManager.screensaverType === "pipes" ? 2 :
                                        ScreensaverManager.screensaverType === "flipclock" ? 3 :
                                        ScreensaverManager.screensaverType === "attractor" ? 4 :
                                      ScreensaverManager.screensaverType === "shotmap" ? 5 : 0
        }
    }

    // Dialog to confirm clearing personal media
    Dialog {
        id: clearPersonalMediaDialog
        modal: true
        anchors.centerIn: parent
        width: Theme.scaled(400)
        padding: 0

        property bool isDeleting: false
        property int itemCount: ScreensaverManager.personalMediaCount

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(20)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("settings.screensaver.clearPersonalTitle", "Clear Personal Media?")
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.scaled(20)
                spacing: Theme.scaled(15)

                Text {
                    Layout.fillWidth: true
                    visible: !clearPersonalMediaDialog.isDeleting
                    text: TranslationManager.translate("settings.screensaver.clearPersonalMessage",
                        "This will delete all %1 personal media files. This action cannot be undone.").arg(clearPersonalMediaDialog.itemCount)
                    color: Theme.textColor
                    font: Theme.bodyFont
                    wrapMode: Text.WordWrap
                }

                // Deleting state
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: clearPersonalMediaDialog.isDeleting
                    spacing: Theme.scaled(10)

                    Text {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("settings.screensaver.deletingPersonal", "Deleting personal media...")
                        color: Theme.textColor
                        font: Theme.bodyFont
                        horizontalAlignment: Text.AlignHCenter
                    }

                    // Progress bar (indeterminate)
                    Rectangle {
                        Layout.fillWidth: true
                        height: Theme.scaled(6)
                        radius: Theme.scaled(3)
                        color: Qt.darker(Theme.surfaceColor, 1.3)

                        Rectangle {
                            id: progressIndicator
                            width: parent.width * 0.3
                            height: parent.height
                            radius: Theme.scaled(3)
                            color: Theme.primaryColor

                            SequentialAnimation on x {
                                loops: Animation.Infinite
                                running: clearPersonalMediaDialog.isDeleting
                                NumberAnimation {
                                    from: 0
                                    to: progressIndicator.parent.width - progressIndicator.width
                                    duration: 800
                                    easing.type: Easing.InOutQuad
                                }
                                NumberAnimation {
                                    from: progressIndicator.parent.width - progressIndicator.width
                                    to: 0
                                    duration: 800
                                    easing.type: Easing.InOutQuad
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)
                    visible: !clearPersonalMediaDialog.isDeleting

                    Item { Layout.fillWidth: true }

                    AccessibleButton {
                        text: TranslationManager.translate("common.cancel", "Cancel")
                        accessibleName: "Cancel"
                        onClicked: clearPersonalMediaDialog.close()
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.screensaver.deleteAll", "Delete All")
                        accessibleName: "Delete all personal media"
                        onClicked: {
                            clearPersonalMediaDialog.isDeleting = true
                            // Use a timer to allow the UI to update before blocking deletion
                            deleteTimer.start()
                        }
                        background: Rectangle {
                            implicitHeight: Theme.scaled(36)
                            implicitWidth: Theme.scaled(120)
                            radius: Theme.scaled(6)
                            color: parent.down ? Qt.darker(Theme.errorColor, 1.2) : Theme.errorColor
                        }
                        contentItem: Text {
                            text: parent.text
                            font.family: Theme.bodyFont.family
                            font.pixelSize: Theme.scaled(14)
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        Timer {
            id: deleteTimer
            interval: 100  // Brief delay to show progress animation
            onTriggered: {
                ScreensaverManager.clearPersonalMedia()
                clearPersonalMediaDialog.isDeleting = false
                clearPersonalMediaDialog.close()
            }
        }

        onClosed: {
            isDeleting = false
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Category selector (videos mode only)
        Rectangle {
            Layout.preferredWidth: Theme.scaled(250)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            visible: ScreensaverManager.screensaverType === "videos"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(10)

                Tr {
                    key: "settings.screensaver.videoCategory"
                    fallback: "Video Category"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                Tr {
                    Layout.fillWidth: true
                    key: "settings.screensaver.videoCategoryDesc"
                    fallback: "Choose a theme for screensaver videos"
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(12)
                    wrapMode: Text.WordWrap
                }

                Item { height: 10 }

                // Category list
                // Use local model copy to avoid delegate crash during rapid updates
                ListView {
                    id: categoryList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: categoryModelCopy
                    spacing: Theme.scaled(2)
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    // Local copy of categories - updated via timer to avoid delegate model crash
                    property var categoryModelCopy: []

                    Timer {
                        id: categoryUpdateTimer
                        interval: 50
                        onTriggered: categoryList.categoryModelCopy = ScreensaverManager.categories
                    }

                    Connections {
                        target: ScreensaverManager
                        function onCategoriesChanged() {
                            categoryUpdateTimer.restart()
                        }
                    }

                    Component.onCompleted: {
                        categoryModelCopy = ScreensaverManager.categories
                    }

                    delegate: ItemDelegate {
                        width: ListView.view ? ListView.view.width : 0
                        height: Theme.scaled(36)
                        highlighted: modelData && modelData.id === ScreensaverManager.selectedCategoryId

                        background: Rectangle {
                            color: parent.highlighted ? Theme.primaryColor :
                                   parent.hovered ? Qt.darker(Theme.backgroundColor, 1.2) : Theme.backgroundColor
                            radius: Theme.scaled(6)
                        }

                        contentItem: Text {
                            text: modelData ? modelData.name : ""
                            color: parent.highlighted ? "white" : Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: parent.highlighted
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.scaled(10)
                        }

                        onClicked: {
                            if (modelData) {
                                ScreensaverManager.selectedCategoryId = modelData.id
                            }
                        }
                    }

                    Tr {
                        anchors.centerIn: parent
                        key: ScreensaverManager.isFetchingCategories ? "settings.screensaver.loading" : "settings.screensaver.noCategories"
                        fallback: ScreensaverManager.isFetchingCategories ? "Loading..." : "No categories"
                        visible: parent.count === 0
                        color: Theme.textSecondaryColor
                    }
                }

                AccessibleButton {
                    text: TranslationManager.translate("settings.screensaver.refreshCategories", "Refresh Categories")
                    accessibleName: "Refresh screensaver categories"
                    Layout.fillWidth: true
                    enabled: !ScreensaverManager.isFetchingCategories
                    onClicked: ScreensaverManager.refreshCategories()
                }
            }
        }

        // Screensaver settings
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(15)
                spacing: Theme.scaled(15)

                Tr {
                    key: "settings.screensaver.settings"
                    fallback: "Screensaver Settings"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(16)
                    font.bold: true
                }

                // Screensaver type selector
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(15)

                    Tr {
                        key: "settings.screensaver.type"
                        fallback: "Type"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(14)
                    }

                    StyledComboBox {
                        id: typeComboBox
                        Layout.preferredWidth: Theme.scaled(220)
                        accessibleLabel: TranslationManager.translate("settings.screensaver.type", "Screensaver type")
                        model: [
                            TranslationManager.translate("settings.screensaver.type.disabled", "Turn Screen Off"),
                            TranslationManager.translate("settings.screensaver.type.videos", "Videos & Images"),
                            TranslationManager.translate("settings.screensaver.type.pipes", "3D Pipes"),
                            TranslationManager.translate("settings.screensaver.type.flipclock", "Flip Clock"),
                            TranslationManager.translate("settings.screensaver.type.attractor", "Strange Attractors"),
                            TranslationManager.translate("settings.screensaver.type.shotmap", "Shot Map")
                        ]
                        currentIndex: ScreensaverManager.screensaverType === "videos" ? 1 :
                                      ScreensaverManager.screensaverType === "pipes" ? 2 :
                                      ScreensaverManager.screensaverType === "flipclock" ? 3 :
                                      ScreensaverManager.screensaverType === "attractor" ? 4 :
                                      ScreensaverManager.screensaverType === "shotmap" ? 5 : 0
                        onActivated: {
                            var types = ["disabled", "videos", "pipes", "flipclock", "attractor", "shotmap"]
                            var newType = types[currentIndex]

                            // If switching away from videos and we have cached videos, offer to clear
                            if (ScreensaverManager.screensaverType === "videos" &&
                                newType !== "videos" &&
                                ScreensaverManager.cacheUsedBytes > 0) {
                                screensaverTab.pendingScreensaverType = newType
                                clearCacheDialog.open()
                            } else {
                                ScreensaverManager.screensaverType = newType
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // Pipes settings (pipes mode only)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(30)
                    visible: ScreensaverManager.screensaverType === "pipes"

                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.pipesSpeed"
                            fallback: "Speed"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        ValueInput {
                            id: pipesSpeedInput
                            value: ScreensaverManager.pipesSpeed
                            suffix: "x"
                            from: 0.1
                            to: 2.0
                            stepSize: 0.1
                            decimals: 1
                            accessibleName: TranslationManager.translate("settings.screensaver.pipesSpeedAccessible", "Pipes animation speed")
                            onValueModified: function(newValue) { ScreensaverManager.pipesSpeed = newValue }
                        }
                    }

                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.cameraSpeed"
                            fallback: "Camera rotation"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        ValueInput {
                            id: cameraSpeedInput
                            value: ScreensaverManager.pipesCameraSpeed
                            suffix: "s"
                            from: 10
                            to: 300
                            stepSize: 1
                            decimals: 0
                            accessibleName: TranslationManager.translate("settings.screensaver.cameraSpeedAccessible", "Camera rotation speed")
                            onValueModified: function(newValue) { ScreensaverManager.pipesCameraSpeed = newValue }
                        }
                    }

                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.showClock"
                            fallback: "Show Clock"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ScreensaverManager.pipesShowClock
                            accessibleName: TranslationManager.translate("settings.screensaver.showClock", "Show Clock")
                            onCheckedChanged: ScreensaverManager.pipesShowClock = checked
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // Flip Clock settings (flipclock mode only)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(30)
                    visible: ScreensaverManager.screensaverType === "flipclock"

                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.flipclock3D"
                            fallback: "3D perspective"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ScreensaverManager.flipClockUse3D
                            accessibleName: TranslationManager.translate("settings.screensaver.flipclock3D", "3D perspective")
                            onCheckedChanged: ScreensaverManager.flipClockUse3D = checked
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // Strange Attractor settings (attractor mode only)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(30)
                    visible: ScreensaverManager.screensaverType === "attractor"

                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.showClock"
                            fallback: "Show Clock"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ScreensaverManager.attractorShowClock
                            accessibleName: TranslationManager.translate("settings.screensaver.showClock", "Show Clock")
                            onCheckedChanged: ScreensaverManager.attractorShowClock = checked
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // Shot Map settings (shotmap mode only)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(20)
                    visible: ScreensaverManager.screensaverType === "shotmap"

                    RowLayout {
                        spacing: Theme.scaled(8)
                        visible: Settings.hasQuick3D  // Globe requires Quick3D; hide selector without it

                        Tr {
                            key: "settings.screensaver.shotmap.shape"
                            fallback: "Shape"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledComboBox {
                            id: shotMapShapeCombo
                            width: Theme.scaled(100)
                            accessibleLabel: TranslationManager.translate("settings.screensaver.shotmap.shape", "Shape")
                            model: [
                                TranslationManager.translate("settings.screensaver.shotmap.flat", "Flat"),
                                TranslationManager.translate("settings.screensaver.shotmap.globe", "Globe")
                            ]
                            currentIndex: ScreensaverManager.shotMapShape === "globe" ? 1 : 0
                            onActivated: {
                                var shapes = ["flat", "globe"]
                                ScreensaverManager.shotMapShape = shapes[currentIndex]
                            }
                        }
                    }

                    RowLayout {
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "settings.screensaver.shotmap.texture"
                            fallback: "Map"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledComboBox {
                            id: shotMapTextureCombo
                            width: Theme.scaled(110)
                            accessibleLabel: TranslationManager.translate("settings.screensaver.shotmap.texture", "Texture")
                            model: [
                                TranslationManager.translate("settings.screensaver.shotmap.dark", "Dark"),
                                TranslationManager.translate("settings.screensaver.shotmap.bright", "Bright"),
                                TranslationManager.translate("settings.screensaver.shotmap.satellite", "Satellite")
                            ]
                            currentIndex: ScreensaverManager.shotMapTexture === "bright" ? 1 :
                                          ScreensaverManager.shotMapTexture === "satellite" ? 2 : 0
                            onActivated: {
                                var textures = ["dark", "bright", "satellite"]
                                ScreensaverManager.shotMapTexture = textures[currentIndex]
                            }
                        }
                    }

                    RowLayout {
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "settings.screensaver.showClock"
                            fallback: "Clock"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ScreensaverManager.shotMapShowClock
                            accessibleName: TranslationManager.translate("settings.screensaver.showClock", "Clock")
                            onToggled: ScreensaverManager.shotMapShowClock = checked
                        }
                    }

                    RowLayout {
                        spacing: Theme.scaled(8)

                        Tr {
                            key: "settings.screensaver.shotmap.profiles"
                            fallback: "Profiles"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ScreensaverManager.shotMapShowProfiles
                            accessibleName: TranslationManager.translate("settings.screensaver.shotmap.profiles", "Profiles")
                            onToggled: ScreensaverManager.shotMapShowProfiles = checked
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // Status row (only visible for videos mode)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(20)
                    visible: ScreensaverManager.screensaverType === "videos"

                    ColumnLayout {
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.screensaver.currentCategory"
                            fallback: "Current Category"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Text {
                            text: ScreensaverManager.selectedCategoryName
                            color: Theme.primaryColor
                            font.pixelSize: Theme.scaled(16)
                            font.bold: true
                        }
                    }

                    ColumnLayout {
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.screensaver.videos"
                            fallback: "Videos"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Text {
                            text: ScreensaverManager.itemCount + (ScreensaverManager.isDownloading ? " (" + TranslationManager.translate("settings.screensaver.downloading", "downloading...") + ")" : "")
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                        }
                    }

                    ColumnLayout {
                        spacing: Theme.scaled(4)

                        Tr {
                            key: "settings.screensaver.cacheUsage"
                            fallback: "Cache Usage"
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Text {
                            text: (ScreensaverManager.cacheUsedBytes / 1024 / 1024).toFixed(0) + " MB"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                        }
                    }

                    // Rate limit indicator
                    ColumnLayout {
                        spacing: Theme.scaled(4)
                        visible: ScreensaverManager.isRateLimited

                        Text {
                            text: TranslationManager.translate("settings.screensaver.rateLimited", "Slow Download")
                            color: Theme.textSecondaryColor
                            font.pixelSize: Theme.scaled(12)
                        }

                        Text {
                            text: ScreensaverManager.rateLimitMinutesRemaining > 0
                                ? TranslationManager.translate("settings.screensaver.nextIn", "Next in %1 min").arg(ScreensaverManager.rateLimitMinutesRemaining)
                                : TranslationManager.translate("settings.screensaver.ready", "Ready")
                            color: Theme.warningColor
                            font.pixelSize: Theme.scaled(16)
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // Download progress (videos mode only)
                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(6)
                    radius: Theme.scaled(3)
                    color: Qt.darker(Theme.surfaceColor, 1.3)
                    visible: ScreensaverManager.screensaverType === "videos" && ScreensaverManager.isDownloading

                    Rectangle {
                        width: parent.width * ScreensaverManager.downloadProgress
                        height: parent.height
                        radius: Theme.scaled(3)
                        color: Theme.primaryColor

                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }

                // Toggles row (videos mode only)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(30)
                    visible: ScreensaverManager.screensaverType === "videos"

                    // Cache toggle
                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.cacheVideos"
                            fallback: "Cache Videos"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ScreensaverManager.cacheEnabled
                            accessibleName: TranslationManager.translate("settings.screensaver.cacheVideos", "Cache Videos")
                            onCheckedChanged: ScreensaverManager.cacheEnabled = checked
                        }
                    }

                    // Show clock toggle
                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.showClock"
                            fallback: "Show Clock"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ScreensaverManager.videosShowClock
                            accessibleName: TranslationManager.translate("settings.screensaver.showClock", "Show Clock")
                            onCheckedChanged: ScreensaverManager.videosShowClock = checked
                        }
                    }

                    // Show date toggle - only visible for Personal category
                    RowLayout {
                        spacing: Theme.scaled(10)
                        visible: ScreensaverManager.isPersonalCategory

                        Tr {
                            key: "settings.screensaver.showDate"
                            fallback: "Show Date"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ScreensaverManager.showDateOnPersonal
                            accessibleName: TranslationManager.translate("settings.screensaver.showDate", "Show Date")
                            onCheckedChanged: ScreensaverManager.showDateOnPersonal = checked
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // Screen dimming settings (all modes except disabled)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(30)
                    visible: ScreensaverManager.screensaverType !== "disabled"

                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.dimAfter"
                            fallback: "Dim after"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        ValueInput {
                            value: ScreensaverManager.dimDelayMinutes
                            suffix: " min"
                            from: 0
                            to: 45
                            stepSize: 5
                            decimals: 0
                            displayText: value === 0 ? TranslationManager.translate("settings.screensaver.immediately", "Immediately") : ""
                            accessibleName: TranslationManager.translate("settings.screensaver.dimAfterAccessible", "Dim screen after delay in minutes")
                            onValueModified: function(newValue) { ScreensaverManager.dimDelayMinutes = newValue }
                        }
                    }

                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.dimAmount"
                            fallback: "Dim amount"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        ValueInput {
                            value: ScreensaverManager.dimPercent
                            suffix: "%"
                            from: 0
                            to: 100
                            stepSize: 5
                            decimals: 0
                            displayText: value === 0 ? TranslationManager.translate("settings.screensaver.off", "Off") : ""
                            accessibleName: TranslationManager.translate("settings.screensaver.dimAmountAccessible", "Screen dim amount percentage")
                            onValueModified: function(newValue) { ScreensaverManager.dimPercent = newValue }
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                Item { Layout.fillHeight: true }

                // Action buttons (videos mode only)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)
                    visible: ScreensaverManager.screensaverType === "videos"

                    AccessibleButton {
                        visible: !ScreensaverManager.isPersonalCategory
                        text: TranslationManager.translate("settings.screensaver.refreshVideos", "Refresh Videos")
                        accessibleName: "Refresh screensaver videos"
                        onClicked: ScreensaverManager.refreshCatalog()
                        enabled: !ScreensaverManager.isRefreshing
                    }

                    AccessibleButton {
                        visible: ScreensaverManager.isPersonalCategory
                        text: TranslationManager.translate("settings.screensaver.clearPersonal", "Clear Personal Media")
                        accessibleName: "Clear personal media"
                        onClicked: clearPersonalMediaDialog.open()
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}
