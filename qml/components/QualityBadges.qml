import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Decenza

// Compact quality status chip(s) for a shot. Sized to its content (shrink-wraps).
// Note: always shows at least one chip when visible — either a flag or "Clean extraction".
// Shows the most important quality indicator: channeling (red), temp unstable (orange),
// grind issue (orange), or clean extraction (green). Multiple flags show multiple chips.
Item {
    id: root

    required property bool channelingDetected
    required property bool temperatureUnstable
    required property bool grindIssueDetected

    signal summaryRequested()

    Layout.fillWidth: true
    implicitWidth: badgeRow.implicitWidth
    implicitHeight: badgeRow.implicitHeight

    Flow {
        id: badgeRow
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
            Accessible.focusable: true

            Row {
                id: channelingRow
                anchors.centerIn: parent
                spacing: Theme.scaled(4)
                Rectangle {
                    width: Theme.scaled(8); height: Theme.scaled(8); radius: Theme.scaled(4)
                    color: Theme.errorColor; anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
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
            Accessible.focusable: true

            Row {
                id: tempRow
                anchors.centerIn: parent
                spacing: Theme.scaled(4)
                Rectangle {
                    width: Theme.scaled(8); height: Theme.scaled(8); radius: Theme.scaled(4)
                    color: Theme.warningColor; anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
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

        // Grind issue badge (orange)
        Rectangle {
            visible: root.grindIssueDetected
            width: grindRow.width + Theme.spacingMedium * 2
            height: Theme.scaled(28)
            radius: Theme.scaled(14)
            color: Qt.rgba(Theme.warningColor.r, Theme.warningColor.g, Theme.warningColor.b, 0.15)
            border.color: Theme.warningColor
            border.width: Theme.scaled(1)

            Accessible.role: Accessible.StaticText
            Accessible.name: grindText.text
            Accessible.focusable: true

            Row {
                id: grindRow
                anchors.centerIn: parent
                spacing: Theme.scaled(4)
                Rectangle {
                    width: Theme.scaled(8); height: Theme.scaled(8); radius: Theme.scaled(4)
                    color: Theme.warningColor; anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
                Tr {
                    id: grindText
                    key: "badges.grindIssue"
                    fallback: "Grind issue"
                    font: Theme.captionFont
                    color: Theme.warningColor
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }
        }

        // Clean extraction badge (green) — only shown when no flags are set
        Rectangle {
            visible: !root.channelingDetected && !root.temperatureUnstable && !root.grindIssueDetected
            width: cleanRow.width + Theme.spacingMedium * 2
            height: Theme.scaled(28)
            radius: Theme.scaled(14)
            color: Qt.rgba(Theme.successColor.r, Theme.successColor.g, Theme.successColor.b, 0.15)
            border.color: Theme.successColor
            border.width: Theme.scaled(1)

            Accessible.role: Accessible.StaticText
            Accessible.name: cleanText.text
            Accessible.focusable: true

            Row {
                id: cleanRow
                anchors.centerIn: parent
                spacing: Theme.scaled(4)
                Rectangle {
                    width: Theme.scaled(8); height: Theme.scaled(8); radius: Theme.scaled(4)
                    color: Theme.successColor; anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
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
