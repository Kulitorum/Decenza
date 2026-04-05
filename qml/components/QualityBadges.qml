import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Decenza

// Single-line quality status chip(s) shown below the graph legend.
// Shows the most important quality indicator: channeling (red), temp unstable (orange),
// or clean extraction (green). If both flags are set, shows both red + orange.
Item {
    id: root

    required property bool channelingDetected
    required property bool temperatureUnstable

    signal summaryRequested()

    Layout.fillWidth: true
    implicitHeight: badgeRow.height

    Flow {
        id: badgeRow
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.spacingSmall

        // Channeling badge (red)
        Rectangle {
            visible: root.channelingDetected
            width: channelingRow.width + Theme.spacingMedium * 2
            height: Theme.scaled(28)
            radius: Theme.scaled(14)
            color: Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.15)
            border.color: Theme.errorColor
            border.width: Theme.scaled(1)

            Accessible.role: Accessible.StaticText
            Accessible.name: channelingText.text

            Row {
                id: channelingRow
                anchors.centerIn: parent
                spacing: Theme.scaled(4)
                Rectangle {
                    width: Theme.scaled(8); height: Theme.scaled(8); radius: Theme.scaled(4)
                    color: Theme.errorColor; anchors.verticalCenter: parent.verticalCenter
                }
                Tr {
                    id: channelingText
                    key: "badges.channeling"
                    fallback: "Channeling detected"
                    font: Theme.captionFont
                    color: Theme.errorColor
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }
        }

        // Temperature unstable badge (orange/warning)
        Rectangle {
            visible: root.temperatureUnstable
            width: tempRow.width + Theme.spacingMedium * 2
            height: Theme.scaled(28)
            radius: Theme.scaled(14)
            color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.15)
            border.color: Theme.warningColor
            border.width: Theme.scaled(1)

            Accessible.role: Accessible.StaticText
            Accessible.name: tempText.text

            Row {
                id: tempRow
                anchors.centerIn: parent
                spacing: Theme.scaled(4)
                Rectangle {
                    width: Theme.scaled(8); height: Theme.scaled(8); radius: Theme.scaled(4)
                    color: Theme.warningColor; anchors.verticalCenter: parent.verticalCenter
                }
                Tr {
                    id: tempText
                    key: "badges.tempUnstable"
                    fallback: "Temp unstable"
                    font: Theme.captionFont
                    color: Theme.warningColor
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }
        }

        // Clean extraction badge (green) — only shown when neither flag is set
        Rectangle {
            visible: !root.channelingDetected && !root.temperatureUnstable
            width: cleanRow.width + Theme.spacingMedium * 2
            height: Theme.scaled(28)
            radius: Theme.scaled(14)
            color: Qt.rgba(Theme.successColor.r, Theme.successColor.g, Theme.successColor.b, 0.15)
            border.color: Theme.successColor
            border.width: Theme.scaled(1)

            Accessible.role: Accessible.StaticText
            Accessible.name: cleanText.text

            Row {
                id: cleanRow
                anchors.centerIn: parent
                spacing: Theme.scaled(4)
                Rectangle {
                    width: Theme.scaled(8); height: Theme.scaled(8); radius: Theme.scaled(4)
                    color: Theme.successColor; anchors.verticalCenter: parent.verticalCenter
                }
                Tr {
                    id: cleanText
                    key: "badges.clean"
                    fallback: "Clean extraction"
                    font: Theme.captionFont
                    color: Theme.successColor
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }
        }

        // "Shot Summary" button
        Rectangle {
            width: summaryRow.width + Theme.spacingMedium * 2
            height: Theme.scaled(28)
            radius: Theme.scaled(14)
            color: Theme.surfaceColor
            border.color: Theme.borderColor
            border.width: Theme.scaled(1)

            Accessible.role: Accessible.Button
            Accessible.name: summaryLabel.text
            Accessible.focusable: true
            Accessible.onPressAction: summaryArea.clicked(null)

            Row {
                id: summaryRow
                anchors.centerIn: parent
                spacing: Theme.scaled(4)
                Image {
                    source: "qrc:/icons/Graph.svg"
                    sourceSize.width: Theme.scaled(12)
                    sourceSize.height: Theme.scaled(12)
                    anchors.verticalCenter: parent.verticalCenter
                    layer.enabled: true
                    layer.smooth: true
                    layer.effect: MultiEffect {
                        colorization: 1.0
                        colorizationColor: Theme.textSecondaryColor
                    }
                }
                Text {
                    id: summaryLabel
                    text: TranslationManager.translate("badges.shotSummary", "Shot Summary")
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                id: summaryArea
                anchors.fill: parent
                onClicked: root.summaryRequested()
            }
        }
    }
}
