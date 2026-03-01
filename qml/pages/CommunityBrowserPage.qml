import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"
import "../components/library"

Item {
    id: communityBrowser
    objectName: "communityBrowserPage"

    // Filter state
    property string filterType: ""
    property string filterVariable: ""
    property string filterAction: ""
    property string sortBy: "newest"
    property int currentPage: 1

    // Selection
    property string selectedEntryId: ""

    Component.onCompleted: {
        root.currentPageTitle = "Community"
        refreshResults()
    }
    StackView.onActivated: root.currentPageTitle = "Community"

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.leftMargin: Theme.scaled(12)
        anchors.rightMargin: Theme.scaled(12)
        anchors.bottomMargin: Theme.bottomBarHeight
        spacing: Theme.spacingMedium

        // Filter bar
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)

            // Type filter
            StyledComboBox {
                id: typeFilter
                Layout.preferredWidth: Theme.scaled(120)
                accessibleLabel: TranslationManager.translate("community.filter.type", "Type filter")
                model: ["All Types", "Items", "Zones", "Layouts", "Themes"]
                onCurrentIndexChanged: {
                    var types = ["", "item", "zone", "layout", "theme"]
                    filterType = types[currentIndex]
                    refreshResults()
                }
            }

            // Variable filter
            StyledComboBox {
                id: variableFilter
                Layout.fillWidth: true
                accessibleLabel: TranslationManager.translate("community.filter.variable", "Variable filter")
                model: ["Any Variable", "Group Head Temp", "Steam Temp", "Pressure",
                        "Flow Rate", "Weight", "Water Level", "Shot Time",
                        "Profile", "Machine State", "Time", "Date",
                        "Ratio", "Dose", "Target Weight"]
                onCurrentIndexChanged: {
                    var vars = ["", "%TEMP%", "%STEAM_TEMP%", "%PRESSURE%",
                                "%FLOW%", "%WEIGHT%", "%WATER%", "%SHOT_TIME%",
                                "%PROFILE%", "%STATE%", "%TIME%", "%DATE%",
                                "%RATIO%", "%DOSE%", "%TARGET_WEIGHT%"]
                    filterVariable = vars[currentIndex]
                    refreshResults()
                }
            }

            // Action filter
            StyledComboBox {
                id: actionFilter
                Layout.fillWidth: true
                accessibleLabel: TranslationManager.translate("community.filter.action", "Action filter")
                model: ["Any Action",
                        "Go to Settings", "Go to History", "Go to Profiles",
                        "Go to Profile Editor", "Go to Recipes", "Go to Descaling",
                        "Go to AI", "Go to Visualizer", "Go to Favorites",
                        "Go to Steam", "Go to Hot Water", "Go to Flush",
                        "Go to Bean Info",
                        "Sleep", "Start Espresso", "Start Steam",
                        "Start Hot Water", "Start Flush", "Stop",
                        "Tare Scale", "Quit App",
                        "Toggle Espresso", "Toggle Steam",
                        "Toggle Hot Water", "Toggle Flush", "Toggle Beans"]
                onCurrentIndexChanged: {
                    var actions = ["",
                        "navigate:settings", "navigate:history", "navigate:profiles",
                        "navigate:profileEditor", "navigate:recipes", "navigate:descaling",
                        "navigate:ai", "navigate:visualizer", "navigate:autofavorites",
                        "navigate:steam", "navigate:hotwater", "navigate:flush",
                        "navigate:beaninfo",
                        "command:sleep", "command:startEspresso", "command:startSteam",
                        "command:startHotWater", "command:startFlush", "command:idle",
                        "command:tare", "command:quit",
                        "togglePreset:espresso", "togglePreset:steam",
                        "togglePreset:hotwater", "togglePreset:flush", "togglePreset:beans"]
                    filterAction = actions[currentIndex]
                    refreshResults()
                }
            }

            // Sort
            StyledComboBox {
                id: sortFilter
                Layout.preferredWidth: Theme.scaled(120)
                accessibleLabel: TranslationManager.translate("community.filter.sort", "Sort order")
                model: ["Newest", "Most Popular"]
                onCurrentIndexChanged: {
                    var sorts = ["newest", "popular"]
                    sortBy = sorts[currentIndex]
                    refreshResults()
                }
            }
        }

        // Loading indicator
        Text {
            visible: LibrarySharing.browsing
            text: "Loading..."
            color: Theme.primaryColor
            font: Theme.bodyFont
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Results grid
        GridView {
            id: resultsGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: Theme.scaled(300)
            cellHeight: Theme.scaled(200)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            model: LibrarySharing.communityEntries

            delegate: LibraryItemCard {
                width: resultsGrid.cellWidth - Theme.scaled(8)
                height: resultsGrid.cellHeight - Theme.scaled(8)
                entryData: modelData
                displayMode: 0
                isSelected: communityBrowser.selectedEntryId === (modelData.id || "")

                onClicked: {
                    var id = modelData.id || ""
                    communityBrowser.selectedEntryId =
                        communityBrowser.selectedEntryId === id ? "" : id
                }
                onDoubleClicked: {
                    LibrarySharing.downloadEntry(modelData.id)
                }
            }

            // Load more when reaching bottom
            onAtYEndChanged: {
                if (atYEnd && count > 0 && !LibrarySharing.browsing) {
                    var totalPages = Math.ceil(LibrarySharing.totalCommunityResults / 20)
                    if (currentPage < totalPages) {
                        currentPage++
                        LibrarySharing.browseCommunity(filterType, filterVariable,
                            filterAction, "", sortBy, currentPage)
                    }
                }
            }

            // Empty state
            Text {
                visible: resultsGrid.count === 0 && !LibrarySharing.browsing
                anchors.centerIn: parent
                text: filterType || filterVariable || filterAction
                    ? "No entries match your filters."
                    : "Community library is empty.\nBe the first to share a widget!"
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // Download feedback
    Connections {
        target: LibrarySharing
        function onDownloadComplete(localEntryId) {
            downloadToast.text = "Added to your library!"
            downloadToast.visible = true
            downloadToastTimer.restart()
        }
        function onDownloadAlreadyExists(localEntryId) {
            downloadToast.text = "Already in your library"
            downloadToast.visible = true
            downloadToastTimer.restart()
        }
        function onDownloadFailed(error) {
            downloadToast.text = "Download failed: " + error
            downloadToast.visible = true
            downloadToastTimer.restart()
        }
    }

    // Simple toast notification
    Rectangle {
        id: downloadToast
        visible: false
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.bottomBarHeight + Theme.scaled(12)
        anchors.horizontalCenter: parent.horizontalCenter
        width: toastText.implicitWidth + Theme.scaled(32)
        height: Theme.scaled(40)
        radius: Theme.scaled(20)
        color: Theme.surfaceColor
        border.color: Theme.borderColor
        border.width: 1

        property alias text: toastText.text

        Text {
            id: toastText
            anchors.centerIn: parent
            color: Theme.textColor
            font: Theme.bodyFont
        }
    }

    Timer {
        id: downloadToastTimer
        interval: 3000
        onTriggered: downloadToast.visible = false
    }

    // Bottom navigation bar
    BottomBar {
        title: "Community"
        onBackClicked: pageStack.pop()

        // Download button
        Rectangle {
            property bool downloadEnabled: selectedEntryId !== "" && !LibrarySharing.downloading
            width: downloadLabel.implicitWidth + Theme.scaled(32)
            height: Theme.scaled(40)
            radius: Theme.scaled(20)
            color: downloadEnabled ? "white" : Qt.rgba(1, 1, 1, 0.15)

            Accessible.role: Accessible.Button
            Accessible.name: LibrarySharing.downloading ? "Downloading" : "Add to Library"
            Accessible.focusable: true
            Accessible.onPressAction: downloadArea.clicked(null)

            Text {
                id: downloadLabel
                anchors.centerIn: parent
                text: LibrarySharing.downloading ? "Downloading..." : "Add to Library"
                color: parent.downloadEnabled ? Theme.primaryColor : Qt.rgba(1, 1, 1, 0.5)
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.bodyFont.pixelSize
                font.bold: true
                Accessible.ignored: true
            }

            MouseArea {
                id: downloadArea
                anchors.fill: parent
                enabled: parent.downloadEnabled
                onClicked: LibrarySharing.downloadEntry(selectedEntryId)
            }
        }

        Text {
            visible: LibrarySharing.totalCommunityResults > 0
            text: LibrarySharing.totalCommunityResults + " entries"
            color: "white"
            font: Theme.captionFont
        }
    }

    function refreshResults() {
        currentPage = 1
        LibrarySharing.browseCommunity(filterType, filterVariable,
            filterAction, "", sortBy, currentPage)
    }
}
