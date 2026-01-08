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
                ListView {
                    id: categoryList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: ScreensaverManager.categories
                    spacing: Theme.scaled(2)
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: ItemDelegate {
                        width: ListView.view.width
                        height: Theme.scaled(36)
                        highlighted: modelData.id === ScreensaverManager.selectedCategoryId

                        background: Rectangle {
                            color: parent.highlighted ? Theme.primaryColor :
                                   parent.hovered ? Qt.darker(Theme.backgroundColor, 1.2) : Theme.backgroundColor
                            radius: Theme.scaled(6)
                        }

                        contentItem: Text {
                            text: modelData.name
                            color: parent.highlighted ? "white" : Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                            font.bold: parent.highlighted
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.scaled(10)
                        }

                        onClicked: {
                            ScreensaverManager.selectedCategoryId = modelData.id
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
                            key: "settings.screensaver.flipclock24Hour"
                            fallback: "24-hour format"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ScreensaverManager.flipClockUse24Hour
                            onCheckedChanged: ScreensaverManager.flipClockUse24Hour = checked
                        }
                    }

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
                            onCheckedChanged: ScreensaverManager.attractorShowClock = checked
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // Shot Map settings (shotmap mode only)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(30)
                    visible: ScreensaverManager.screensaverType === "shotmap"

                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.shotmap.style"
                            fallback: "Map Style"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledComboBox {
                            id: shotMapStyleCombo
                            width: Theme.scaled(150)
                            model: [
                                TranslationManager.translate("settings.screensaver.shotmap.dark", "Dark"),
                                TranslationManager.translate("settings.screensaver.shotmap.globe", "Globe"),
                                TranslationManager.translate("settings.screensaver.shotmap.bright", "Bright")
                            ]
                            currentIndex: ScreensaverManager.shotMapStyle === "globe" ? 1 :
                                          ScreensaverManager.shotMapStyle === "bright" ? 2 : 0
                            onActivated: {
                                var styles = ["dark", "globe", "bright"]
                                ScreensaverManager.shotMapStyle = styles[currentIndex]
                            }
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
                            onCheckedChanged: ScreensaverManager.showDateOnPersonal = checked
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
                        onClicked: ScreensaverManager.clearPersonalMedia()
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}
