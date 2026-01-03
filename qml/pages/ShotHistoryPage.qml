import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: shotHistoryPage
    objectName: "shotHistoryPage"
    background: Rectangle { color: Theme.backgroundColor }

    property var selectedShots: []
    property int maxSelections: 3

    Component.onCompleted: {
        root.currentPageTitle = "Shot History"
        refreshFilterOptions()
        loadShots()
    }

    StackView.onActivated: {
        root.currentPageTitle = "Shot History"
        refreshFilterOptions()
        loadShots()
    }

    function loadShots() {
        var filter = buildFilter()
        var shots = MainController.shotHistory.getShotsFiltered(filter, 0, 100)
        shotListModel.clear()
        for (var i = 0; i < shots.length; i++) {
            shotListModel.append(shots[i])
        }
    }

    // Get filter values from the model arrays directly (more reliable than currentText)
    property var profileOptions: []
    property var beanOptions: []

    function refreshFilterOptions() {
        var profiles = MainController.shotHistory.getDistinctProfiles()
        var beans = MainController.shotHistory.getDistinctBeanBrands()
        profileOptions = [TranslationManager.translate("shothistory.allprofiles", "All Profiles")].concat(profiles)
        beanOptions = [TranslationManager.translate("shothistory.allbeans", "All Beans")].concat(beans)
    }

    function buildFilter() {
        var filter = {}
        if (profileFilter.currentIndex > 0 && profileFilter.currentIndex < profileOptions.length) {
            filter.profileName = profileOptions[profileFilter.currentIndex]
        }
        if (beanFilter.currentIndex > 0 && beanFilter.currentIndex < beanOptions.length) {
            filter.beanBrand = beanOptions[beanFilter.currentIndex]
        }
        if (searchField.text.length > 0) {
            filter.searchText = searchField.text
        }
        return filter
    }

    function toggleSelection(shotId) {
        var idx = selectedShots.indexOf(shotId)
        if (idx >= 0) {
            selectedShots.splice(idx, 1)
        } else if (selectedShots.length < maxSelections) {
            selectedShots.push(shotId)
        }
        selectedShots = selectedShots.slice()  // Trigger binding update
    }

    function isSelected(shotId) {
        return selectedShots.indexOf(shotId) >= 0
    }

    function clearSelection() {
        selectedShots = []
    }

    function openComparison() {
        MainController.shotComparison.clearAll()
        for (var i = 0; i < selectedShots.length; i++) {
            MainController.shotComparison.addShot(selectedShots[i])
        }
        pageStack.push(Qt.resolvedUrl("ShotComparisonPage.qml"))
    }

    ListModel {
        id: shotListModel
    }

    // Filter bar
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        spacing: Theme.spacingMedium

        // Header row with selection count and compare button
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium
            visible: selectedShots.length > 0

            Text {
                text: selectedShots.length + " " + TranslationManager.translate("shothistory.selected", "selected")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
                Layout.fillWidth: true
            }

            Button {
                text: TranslationManager.translate("shothistory.clear", "Clear")
                onClicked: clearSelection()

                background: Rectangle {
                    color: "transparent"
                    radius: Theme.buttonRadius
                    border.color: Theme.borderColor
                    border.width: 1
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.labelFont
                    color: Theme.textColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                text: TranslationManager.translate("shothistory.compare", "Compare")
                enabled: selectedShots.length >= 2
                onClicked: openComparison()

                background: Rectangle {
                    color: parent.enabled ? Theme.primaryColor : Theme.buttonDisabled
                    radius: Theme.buttonRadius
                }
                contentItem: Text {
                    text: parent.text
                    font: Theme.labelFont
                    color: parent.enabled ? "white" : Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // Filter row
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            ComboBox {
                id: profileFilter
                Layout.preferredWidth: Theme.scaled(150)
                model: profileOptions
                onCurrentIndexChanged: if (shotHistoryPage.visible) loadShots()

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.buttonRadius
                    border.color: Theme.borderColor
                    border.width: 1
                }
                contentItem: Text {
                    text: profileFilter.displayText
                    font: Theme.labelFont
                    color: Theme.textColor
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacingSmall
                }
            }

            ComboBox {
                id: beanFilter
                Layout.preferredWidth: Theme.scaled(150)
                model: beanOptions
                onCurrentIndexChanged: if (shotHistoryPage.visible) loadShots()

                background: Rectangle {
                    color: Theme.surfaceColor
                    radius: Theme.buttonRadius
                    border.color: Theme.borderColor
                    border.width: 1
                }
                contentItem: Text {
                    text: beanFilter.displayText
                    font: Theme.labelFont
                    color: Theme.textColor
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacingSmall
                }
            }

            StyledTextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: TranslationManager.translate("shothistory.searchplaceholder", "Search notes...")
                onTextChanged: searchTimer.restart()
            }

            Timer {
                id: searchTimer
                interval: 300
                onTriggered: loadShots()
            }
        }

        // Shot count
        Text {
            text: shotListModel.count + " " + TranslationManager.translate("shothistory.shots", "shots") +
                  (MainController.shotHistory.totalShots > shotListModel.count ?
                  " (" + TranslationManager.translate("shothistory.of", "of") + " " + MainController.shotHistory.totalShots + ")" : "")
            font: Theme.captionFont
            color: Theme.textSecondaryColor
        }

        // Shot list
        ListView {
            id: shotListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSmall
            model: shotListModel

            delegate: Rectangle {
                id: shotDelegate
                width: shotListView.width
                height: Theme.scaled(90)
                radius: Theme.cardRadius
                color: isSelected(model.id) ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                border.color: isSelected(model.id) ? Theme.primaryColor : "transparent"
                border.width: isSelected(model.id) ? 2 : 0

                // Store enjoyment for child components to access
                property int shotEnjoyment: model.enjoyment || 0

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    // Selection checkbox
                    CheckBox {
                        checked: isSelected(model.id)
                        enabled: checked || selectedShots.length < maxSelections
                        onClicked: toggleSelection(model.id)

                        indicator: Rectangle {
                            implicitWidth: Theme.scaled(24)
                            implicitHeight: Theme.scaled(24)
                            radius: 4
                            color: parent.checked ? Theme.primaryColor : "transparent"
                            border.color: parent.checked ? Theme.primaryColor : Theme.borderColor
                            border.width: 2

                            Text {
                                anchors.centerIn: parent
                                text: "\u2713"
                                font.pixelSize: Theme.scaled(16)
                                color: Theme.textColor
                                visible: parent.parent.checked
                            }
                        }
                    }

                    // Shot info
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Text {
                                text: model.dateTime || ""
                                font: Theme.subtitleFont
                                color: Theme.textColor
                            }

                            Text {
                                text: model.profileName || ""
                                font: Theme.labelFont
                                color: Theme.primaryColor
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            // Rating stars
                            Row {
                                spacing: 2
                                Repeater {
                                    model: 5
                                    Text {
                                        text: "\u2605"
                                        font.pixelSize: Theme.scaled(14)
                                        color: index < Math.round(shotDelegate.shotEnjoyment / 20)
                                               ? Theme.warningColor : Theme.borderColor
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMedium

                            Text {
                                text: (model.beanBrand || "") + (model.beanType ? " " + model.beanType : "")
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }

                        RowLayout {
                            spacing: Theme.spacingLarge

                            Text {
                                text: (model.doseWeight || 0).toFixed(1) + "g \u2192 " + (model.finalWeight || 0).toFixed(1) + "g"
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: (model.duration || 0).toFixed(1) + "s"
                                font: Theme.labelFont
                                color: Theme.textSecondaryColor
                            }

                            Text {
                                text: model.hasVisualizerUpload ? "\u2601" : ""
                                font.pixelSize: Theme.scaled(16)
                                color: Theme.successColor
                                visible: model.hasVisualizerUpload
                            }
                        }
                    }

                    // Detail arrow
                    Rectangle {
                        width: Theme.scaled(40)
                        height: Theme.scaled(40)
                        radius: Theme.scaled(20)
                        color: Theme.primaryColor

                        Text {
                            anchors.centerIn: parent
                            text: ">"
                            font.pixelSize: Theme.scaled(20)
                            font.bold: true
                            color: "white"
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                pageStack.push(Qt.resolvedUrl("ShotDetailPage.qml"), { shotId: model.id })
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onClicked: toggleSelection(model.id)
                    onPressAndHold: {
                        pageStack.push(Qt.resolvedUrl("ShotDetailPage.qml"), { shotId: model.id })
                    }
                }
            }

            // Empty state
            Tr {
                anchors.centerIn: parent
                key: "shothistory.noshots"
                fallback: "No shots found"
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
                visible: shotListModel.count === 0
            }
        }
    }

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("shothistory.title", "Shot History")
        rightText: MainController.shotHistory.totalShots + " " + TranslationManager.translate("shothistory.shots", "shots")
        onBackClicked: root.goBack()
    }
}
