import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "."

// Brief shot quality summary dialog — no AI, computed from curve data.
// Shows only noteworthy observations + a verdict.
// Analysis logic lives in C++ (ShotAnalysis) — this is a thin display layer.
Dialog {
    id: analysisDialog

    property var shotData: ({})

    title: TranslationManager.translate("shotAnalysis.title", "Shot Summary")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(Overlay.overlay ? Overlay.overlay.width * 0.9 : Theme.scaled(450), Theme.scaled(450))
    padding: 0

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    header: null
    footer: null

    // Analysis computed in C++ via ShotHistoryStorage.generateShotSummary()
    property var analysisLines: analysisDialog.visible
        ? MainController.shotHistory.generateShotSummary(shotData)
        : []

    contentItem: ColumnLayout {
        spacing: Theme.spacingSmall

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingMedium
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            text: analysisDialog.title
            color: Theme.textColor
            font: Theme.subtitleFont
            Accessible.ignored: true
        }

        // Analysis lines
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Repeater {
                model: analysisDialog.analysisLines

                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData.text
                    Accessible.focusable: true

                    // Colored dot for non-verdict lines
                    Rectangle {
                        visible: modelData.type !== "verdict"
                        width: Theme.scaled(6)
                        height: Theme.scaled(6)
                        radius: Theme.scaled(3)
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: Theme.scaled(6)
                        Accessible.ignored: true
                        color: {
                            if (modelData.type === "good") return Theme.successColor
                            if (modelData.type === "caution") return Theme.warningColor
                            if (modelData.type === "warning") return Theme.errorColor
                            return Theme.textSecondaryColor
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.text
                        font: modelData.type === "verdict" ? Theme.subtitleFont : Theme.bodyFont
                        color: modelData.type === "verdict" ? Theme.textColor : Theme.textSecondaryColor
                        wrapMode: Text.Wrap
                        topPadding: modelData.type === "verdict" ? Theme.spacingSmall : 0
                        Accessible.ignored: true
                    }
                }
            }
        }

        // Close button
        AccessibleButton {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMedium
            text: TranslationManager.translate("common.button.ok", "OK")
            accessibleName: TranslationManager.translate("common.accessibility.dismissDialog", "Dismiss dialog")
            onClicked: analysisDialog.close()
        }
    }
}
