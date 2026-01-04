import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: shotDetailPage
    objectName: "shotDetailPage"
    background: Rectangle { color: Theme.backgroundColor }

    property int shotId: 0
    property var shotData: ({})

    Component.onCompleted: {
        root.currentPageTitle = "Shot Detail"
        loadShot()
    }

    function loadShot() {
        if (shotId > 0) {
            shotData = MainController.shotHistory.getShot(shotId)
        }
    }

    function formatRatio() {
        if (shotData.doseWeight > 0) {
            return "1:" + (shotData.finalWeight / shotData.doseWeight).toFixed(1)
        }
        return "-"
    }


    ScrollView {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.topMargin: Theme.pageTopMargin
        anchors.leftMargin: Theme.standardMargin
        anchors.rightMargin: Theme.standardMargin
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Theme.spacingMedium

            // Header
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(2)

                Text {
                    text: shotData.profileName || "Shot Detail"
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Text {
                    text: shotData.dateTime || ""
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                }
            }

            // Graph
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(250)
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                HistoryShotGraph {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    pressureData: shotData.pressure || []
                    flowData: shotData.flow || []
                    temperatureData: shotData.temperature || []
                    weightData: shotData.weight || []
                    phaseMarkers: shotData.phases || []
                    maxTime: shotData.duration || 60
                }
            }

            // Metrics row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingLarge

                // Duration
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.duration"
                        fallback: "Duration"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Text {
                        text: (shotData.duration || 0).toFixed(1) + "s"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }
                }

                // Dose
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.dose"
                        fallback: "Dose"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Text {
                        text: (shotData.doseWeight || 0).toFixed(1) + "g"
                        font: Theme.subtitleFont
                        color: Theme.dyeDoseColor
                    }
                }

                // Output
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.output"
                        fallback: "Output"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Text {
                        text: (shotData.finalWeight || 0).toFixed(1) + "g"
                        font: Theme.subtitleFont
                        color: Theme.dyeOutputColor
                    }
                }

                // Ratio
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.ratio"
                        fallback: "Ratio"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Text {
                        text: formatRatio()
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }
                }

                // Rating
                ColumnLayout {
                    spacing: Theme.scaled(2)
                    Tr {
                        key: "shotdetail.rating"
                        fallback: "Rating"
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                    Text {
                        text: (shotData.enjoyment || 0) > 0 ? shotData.enjoyment + "%" : "-"
                        font: Theme.subtitleFont
                        color: Theme.warningColor
                    }
                }
            }

            // Bean info
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: beanColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: beanColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.beaninfo"
                        fallback: "Bean Info"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: Theme.spacingLarge
                        rowSpacing: Theme.spacingSmall
                        Layout.fillWidth: true

                        Tr { key: "shotdetail.brand"; fallback: "Brand:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.beanBrand || "-"; font: Theme.labelFont; color: Theme.textColor }

                        Tr { key: "shotdetail.type"; fallback: "Type:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.beanType || "-"; font: Theme.labelFont; color: Theme.textColor }

                        Tr { key: "shotdetail.roastdate"; fallback: "Roast Date:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.roastDate || "-"; font: Theme.labelFont; color: Theme.textColor }

                        Tr { key: "shotdetail.roastlevel"; fallback: "Roast Level:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.roastLevel || "-"; font: Theme.labelFont; color: Theme.textColor }
                    }
                }
            }

            // Grinder info
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: grinderColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: grinderColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.grinder"
                        fallback: "Grinder"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: Theme.spacingLarge
                        rowSpacing: Theme.spacingSmall
                        Layout.fillWidth: true

                        Tr { key: "shotdetail.model"; fallback: "Model:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.grinderModel || "-"; font: Theme.labelFont; color: Theme.textColor }

                        Tr { key: "shotdetail.setting"; fallback: "Setting:"; font: Theme.labelFont; color: Theme.textSecondaryColor }
                        Text { text: shotData.grinderSetting || "-"; font: Theme.labelFont; color: Theme.textColor }
                    }
                }
            }

            // Analysis
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: analysisColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: shotData.drinkTds > 0 || shotData.drinkEy > 0

                ColumnLayout {
                    id: analysisColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.analysis"
                        fallback: "Analysis"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    RowLayout {
                        spacing: Theme.spacingLarge

                        ColumnLayout {
                            spacing: Theme.scaled(2)
                            Tr { key: "shotdetail.tds"; fallback: "TDS"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                            Text { text: (shotData.drinkTds || 0).toFixed(2) + "%"; font: Theme.bodyFont; color: Theme.dyeTdsColor }
                        }

                        ColumnLayout {
                            spacing: Theme.scaled(2)
                            Tr { key: "shotdetail.ey"; fallback: "EY"; font: Theme.captionFont; color: Theme.textSecondaryColor }
                            Text { text: (shotData.drinkEy || 0).toFixed(1) + "%"; font: Theme.bodyFont; color: Theme.dyeEyColor }
                        }
                    }
                }
            }

            // Notes
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: notesColumn.height + Theme.spacingLarge
                color: Theme.surfaceColor
                radius: Theme.cardRadius

                ColumnLayout {
                    id: notesColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    Tr {
                        key: "shotdetail.notes"
                        fallback: "Notes"
                        font: Theme.subtitleFont
                        color: Theme.textColor
                    }

                    Text {
                        text: shotData.espressoNotes || "-"
                        font: Theme.bodyFont
                        color: Theme.textColor
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Actions
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                Button {
                    text: TranslationManager.translate("shotdetail.viewdebuglog", "View Debug Log")
                    Layout.fillWidth: true
                    onClicked: debugLogDialog.open()

                    background: Rectangle {
                        color: "transparent"
                        radius: Theme.buttonRadius
                        border.color: Theme.borderColor
                        border.width: Theme.scaled(1)
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
                    text: TranslationManager.translate("shotdetail.deleteshot", "Delete Shot")
                    Layout.fillWidth: true
                    onClicked: deleteConfirmDialog.open()

                    background: Rectangle {
                        color: "transparent"
                        radius: Theme.buttonRadius
                        border.color: Theme.errorColor
                        border.width: Theme.scaled(1)
                    }
                    contentItem: Text {
                        text: parent.text
                        font: Theme.labelFont
                        color: Theme.errorColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Visualizer status
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(50)
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                visible: shotData.visualizerId

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium

                    Tr {
                        key: "shotdetail.uploadedtovisualizer"
                        fallback: "\u2601 Uploaded to Visualizer"
                        font: Theme.labelFont
                        color: Theme.successColor
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: shotData.visualizerId || ""
                        font: Theme.captionFont
                        color: Theme.textSecondaryColor
                    }
                }
            }

            // Bottom spacer
            Item { Layout.preferredHeight: Theme.spacingLarge }
        }
    }

    // Debug log dialog
    Dialog {
        id: debugLogDialog
        title: TranslationManager.translate("shotdetail.debuglog", "Debug Log")
        anchors.centerIn: parent
        width: parent.width * 0.9
        height: parent.height * 0.8
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
        }

        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth

            TextArea {
                text: shotData.debugLog || TranslationManager.translate("shotdetail.nodebuglog", "No debug log available")
                font.family: "monospace"
                font.pixelSize: Theme.scaled(12)
                color: Theme.textColor
                readOnly: true
                wrapMode: Text.Wrap
                background: Rectangle { color: "transparent" }
            }
        }

        standardButtons: Dialog.Close
    }

    // Delete confirmation dialog
    Dialog {
        id: deleteConfirmDialog
        title: TranslationManager.translate("shotdetail.deleteconfirmtitle", "Delete Shot?")
        anchors.centerIn: parent
        modal: true

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
        }

        Tr {
            key: "shotdetail.deleteconfirmmessage"
            fallback: "This will permanently delete this shot from history."
            font: Theme.bodyFont
            color: Theme.textColor
            wrapMode: Text.Wrap
        }

        standardButtons: Dialog.Cancel | Dialog.Ok

        onAccepted: {
            MainController.shotHistory.deleteShot(shotId)
            pageStack.pop()
        }
    }

    // Bottom bar
    BottomBar {
        id: bottomBar
        title: TranslationManager.translate("shotdetail.title", "Shot Detail")
        rightText: shotData.profileName || ""
        onBackClicked: root.goBack()
    }
}
