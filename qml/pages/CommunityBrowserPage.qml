// The results-grid delegate reads this file's ids (`communityBrowser`, `resultsGrid`);
// Bound makes them statically resolvable. It declares its one injected role,
// `modelData`, required in the same edit -- without that, Bound stops role injection
// and every community card renders blank at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

Item {
    id: communityBrowser
    // Declarative so it re-evaluates on a language change. This used to be an
    // imperative assignment in onCompleted/onActivated, which ran once and left
    // page titles in the previous language until you navigated away and back.
    readonly property string pageTitle: TranslationManager.translate("community.title", "Community")

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
        refreshResults()
    }

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
                model: [TranslationManager.translate("community.type.all", "All Types"), TranslationManager.translate("community.type.items", "Items"), TranslationManager.translate("community.type.zones", "Zones"), TranslationManager.translate("community.type.layouts", "Layouts"), TranslationManager.translate("community.type.themes", "Themes")]
                onCurrentIndexChanged: {
                    var types = ["", "item", "zone", "layout", "theme"]
                    communityBrowser.filterType = types[currentIndex]
                    communityBrowser.refreshResults()
                }
            }

            // Variable filter
            StyledComboBox {
                id: variableFilter
                Layout.fillWidth: true
                accessibleLabel: TranslationManager.translate("community.filter.variable", "Variable filter")
                model: [
                    TranslationManager.translate("community.variable.any", "Any Variable"),
                    TranslationManager.translate("community.variable.groupTemp", "Group Head Temp"),
                    TranslationManager.translate("community.variable.steamTemp", "Steam Temp"),
                    TranslationManager.translate("community.variable.pressure", "Pressure"),
                    TranslationManager.translate("community.variable.flow", "Flow Rate"),
                    TranslationManager.translate("community.variable.weight", "Weight"),
                    TranslationManager.translate("community.variable.water", "Water Level"),
                    TranslationManager.translate("community.variable.shotTime", "Shot Time"),
                    TranslationManager.translate("community.variable.profile", "Profile"),
                    TranslationManager.translate("community.variable.state", "Machine State"),
                    TranslationManager.translate("community.variable.time", "Time"),
                    TranslationManager.translate("community.variable.date", "Date"),
                    TranslationManager.translate("community.variable.ratio", "Ratio"),
                    TranslationManager.translate("community.variable.dose", "Dose"),
                    TranslationManager.translate("community.variable.targetWeight", "Target Weight")]
                onCurrentIndexChanged: {
                    var vars = ["", "%TEMP%", "%STEAM_TEMP%", "%PRESSURE%",
                                "%FLOW%", "%WEIGHT%", "%WATER%", "%SHOT_TIME%",
                                "%PROFILE%", "%STATE%", "%TIME%", "%DATE%",
                                "%RATIO%", "%DOSE%", "%TARGET_WEIGHT%"]
                    communityBrowser.filterVariable = vars[currentIndex]
                    communityBrowser.refreshResults()
                }
            }

            // Action filter
            StyledComboBox {
                id: actionFilter
                Layout.fillWidth: true
                accessibleLabel: TranslationManager.translate("community.filter.action", "Action filter")
                model: [
                    TranslationManager.translate("community.action.any", "Any Action"),
                    TranslationManager.translate("community.action.settings", "Go to Settings"),
                    TranslationManager.translate("community.action.history", "Go to History"),
                    TranslationManager.translate("community.action.profiles", "Go to Profiles"),
                    TranslationManager.translate("community.action.profileEditor", "Go to Profile Editor"),
                    TranslationManager.translate("community.action.recipes", "Go to Recipes"),
                    TranslationManager.translate("community.action.descaling", "Go to Descaling"),
                    TranslationManager.translate("community.action.ai", "Go to AI"),
                    TranslationManager.translate("community.action.visualizer", "Go to Visualizer"),
                    TranslationManager.translate("community.action.favorites", "Go to Favorites"),
                    TranslationManager.translate("community.action.steam", "Go to Steam"),
                    TranslationManager.translate("community.action.hotWater", "Go to Hot Water"),
                    TranslationManager.translate("community.action.flush", "Go to Flush"),
                    TranslationManager.translate("community.action.beanInfo", "Go to Bean Info"),
                    TranslationManager.translate("community.action.sleep", "Sleep"),
                    TranslationManager.translate("community.action.startEspresso", "Start Espresso"),
                    TranslationManager.translate("community.action.startSteam", "Start Steam"),
                    TranslationManager.translate("community.action.startHotWater", "Start Hot Water"),
                    TranslationManager.translate("community.action.startFlush", "Start Flush"),
                    TranslationManager.translate("community.action.stop", "Stop"),
                    TranslationManager.translate("community.action.tare", "Tare Scale"),
                    TranslationManager.translate("community.action.quit", "Quit App"),
                    TranslationManager.translate("community.action.toggleEspresso", "Toggle Espresso"),
                    TranslationManager.translate("community.action.toggleSteam", "Toggle Steam"),
                    TranslationManager.translate("community.action.toggleHotWater", "Toggle Hot Water"),
                    TranslationManager.translate("community.action.toggleFlush", "Toggle Flush"),
                    TranslationManager.translate("community.action.toggleBeans", "Toggle Beans")]
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
                    communityBrowser.filterAction = actions[currentIndex]
                    communityBrowser.refreshResults()
                }
            }

            // Sort
            StyledComboBox {
                id: sortFilter
                Layout.preferredWidth: Theme.scaled(120)
                accessibleLabel: TranslationManager.translate("community.filter.sort", "Sort order")
                model: [TranslationManager.translate("community.sort.newest", "Newest"), TranslationManager.translate("community.sort.popular", "Most Popular")]
                onCurrentIndexChanged: {
                    var sorts = ["newest", "popular"]
                    communityBrowser.sortBy = sorts[currentIndex]
                    communityBrowser.refreshResults()
                }
            }
        }

        // Loading indicator
        Text {
            visible: LibrarySharing.browsing
            text: TranslationManager.translate("community.status.loading", "Loading...")
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
                id: communityCard
                required property var modelData

                width: resultsGrid.cellWidth - Theme.scaled(8)
                height: resultsGrid.cellHeight - Theme.scaled(8)
                entryData: communityCard.modelData
                displayMode: 0
                isSelected: communityBrowser.selectedEntryId === (communityCard.modelData.id || "")

                onClicked: {
                    var id = communityCard.modelData.id || ""
                    communityBrowser.selectedEntryId =
                        communityBrowser.selectedEntryId === id ? "" : id
                }
                onDoubleClicked: {
                    LibrarySharing.downloadEntry(communityCard.modelData.id)
                }
            }

            // Load more when reaching bottom
            onAtYEndChanged: {
                if (atYEnd && count > 0 && !LibrarySharing.browsing) {
                    var totalPages = Math.ceil(LibrarySharing.totalCommunityResults / 20)
                    if (communityBrowser.currentPage < totalPages) {
                        communityBrowser.currentPage++
                        LibrarySharing.browseCommunity(communityBrowser.filterType, communityBrowser.filterVariable,
                            communityBrowser.filterAction, "", communityBrowser.sortBy, communityBrowser.currentPage)
                    }
                }
            }

            // Empty state
            Text {
                visible: resultsGrid.count === 0 && !LibrarySharing.browsing
                anchors.centerIn: parent
                text: communityBrowser.filterType || communityBrowser.filterVariable || communityBrowser.filterAction
                    ? TranslationManager.translate("community.empty.filtered", "No entries match your filters.")
                    : TranslationManager.translate("community.empty.none", "Community library is empty.\nBe the first to share a widget!")
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
            downloadToast.text = TranslationManager.translate("community.toast.added", "Added to your library!")
            downloadToast.visible = true
            downloadToastTimer.restart()
        }
        function onDownloadAlreadyExists(localEntryId) {
            downloadToast.text = TranslationManager.translate("community.toast.duplicate", "Already in your library")
            downloadToast.visible = true
            downloadToastTimer.restart()
        }
        function onDownloadFailed(error) {
            downloadToast.text = TranslationManager.translate("community.toast.failed", "Download failed: %1").arg(error)
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
        id: communityBottomBar
        title: TranslationManager.translate("community.title", "Community")
        onBackClicked: AppShell.backRequested()

        // Download button
        Rectangle {
            property bool downloadEnabled: communityBrowser.selectedEntryId !== "" && !LibrarySharing.downloading
            width: downloadLabel.implicitWidth + Theme.scaled(32)
            height: Theme.scaled(40)
            radius: Theme.scaled(20)
            color: downloadEnabled ? communityBottomBar.contentColor : Qt.rgba(communityBottomBar.contentColor.r, communityBottomBar.contentColor.g, communityBottomBar.contentColor.b, 0.15)

            Accessible.role: Accessible.Button
            Accessible.name: LibrarySharing.downloading
                             ? TranslationManager.translate("community.downloading", "Downloading")
                             : TranslationManager.translate("community.addToLibrary", "Add to Library")
            Accessible.focusable: true
            Accessible.onPressAction: downloadArea.clicked(null)

            Text {
                id: downloadLabel
                anchors.centerIn: parent
                text: LibrarySharing.downloading
                      ? TranslationManager.translate("community.downloadingEllipsis", "Downloading...")
                      : TranslationManager.translate("community.addToLibrary", "Add to Library")
                color: parent.downloadEnabled ? communityBottomBar.color : Qt.rgba(communityBottomBar.contentColor.r, communityBottomBar.contentColor.g, communityBottomBar.contentColor.b, 0.5)
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.bodyFont.pixelSize
                font.bold: true
                Accessible.ignored: true
            }

            MouseArea {
                id: downloadArea
                anchors.fill: parent
                enabled: parent.downloadEnabled
                onClicked: LibrarySharing.downloadEntry(communityBrowser.selectedEntryId)
            }
        }

        Text {
            visible: LibrarySharing.totalCommunityResults > 0
            text: TranslationManager.translate("community.entryCount", "%1 entries").arg(LibrarySharing.totalCommunityResults)
            color: communityBottomBar.contentColor
            font: Theme.captionFont
        }
    }

    function refreshResults() {
        currentPage = 1
        LibrarySharing.browseCommunity(filterType, filterVariable,
            filterAction, "", sortBy, currentPage)
    }
}
