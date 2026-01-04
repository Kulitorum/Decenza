import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: screensaverTab

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(15)

        // Category selector
        Rectangle {
            Layout.preferredWidth: Theme.scaled(250)
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

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

                // Status row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(20)

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
                            text: (ScreensaverManager.cacheUsedBytes / 1024 / 1024).toFixed(0) + " MB / " +
                                  (ScreensaverManager.maxCacheBytes / 1024 / 1024 / 1024).toFixed(1) + " GB"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(16)
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                // Download progress
                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(6)
                    radius: Theme.scaled(3)
                    color: Qt.darker(Theme.surfaceColor, 1.3)
                    visible: ScreensaverManager.isDownloading

                    Rectangle {
                        width: parent.width * ScreensaverManager.downloadProgress
                        height: parent.height
                        radius: Theme.scaled(3)
                        color: Theme.primaryColor

                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }

                // Toggles row
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(30)

                    RowLayout {
                        spacing: Theme.scaled(10)

                        Tr {
                            key: "settings.screensaver.enabled"
                            fallback: "Enabled"
                            color: Theme.textColor
                            font.pixelSize: Theme.scaled(14)
                        }

                        StyledSwitch {
                            checked: ScreensaverManager.enabled
                            onCheckedChanged: ScreensaverManager.enabled = checked
                        }
                    }

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

                // Action buttons
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(10)

                    AccessibleButton {
                        text: TranslationManager.translate("settings.screensaver.refreshVideos", "Refresh Videos")
                        accessibleName: "Refresh screensaver videos"
                        onClicked: ScreensaverManager.refreshCatalog()
                        enabled: !ScreensaverManager.isRefreshing
                    }

                    AccessibleButton {
                        text: TranslationManager.translate("settings.screensaver.clearCache", "Clear Cache")
                        accessibleName: "Clear video cache"
                        onClicked: ScreensaverManager.clearCache()
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}
