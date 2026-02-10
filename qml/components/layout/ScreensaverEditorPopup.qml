import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import ".."

Popup {
    id: popup

    property string itemId: ""
    property string zoneName: ""
    property string itemType: ""
    property real clockScale: 1.0  // 0.0 = small (fit width), 1.0 = large (fit height)

    signal saved()

    function openForItem(id, zone, props) {
        itemId = id
        zoneName = zone
        itemType = props.type || ""
        // Migrate from old fitMode string to numeric scale
        if (typeof props.clockScale === "number") {
            clockScale = props.clockScale
        } else if (props.fitMode === "width") {
            clockScale = 0.0
        } else {
            clockScale = 1.0
        }
        open()
    }

    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.spacingMedium

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(Theme.scaled(320), parent.width - Theme.spacingSmall * 2)
    height: content.implicitHeight + padding * 2

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    function save() {
        Settings.setItemProperty(itemId, "clockScale", clockScale)
        saved()
        close()
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: Theme.spacingMedium

        // Title
        Text {
            text: {
                switch (popup.itemType) {
                    case "screensaverFlipClock": return "Flip Clock Settings"
                    case "screensaverPipes": return "3D Pipes Settings"
                    case "screensaverAttractor": return "Attractors Settings"
                    case "screensaverShotMap": return "Shot Map Settings"
                    default: return "Screensaver Settings"
                }
            }
            font.family: Theme.titleFont.family
            font.pixelSize: Theme.titleFont.pixelSize
            font.bold: true
            color: Theme.textColor
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Separator
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderColor
        }

        // Size slider (only for flip clock)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            visible: popup.itemType === "screensaverFlipClock"

            Text {
                text: "Size"
                font: Theme.labelFont
                color: Theme.textSecondaryColor
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Text {
                    text: "Small"
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                }

                Slider {
                    id: sizeSlider
                    Layout.fillWidth: true
                    from: 0.0
                    to: 1.0
                    stepSize: 0.05
                    value: popup.clockScale
                    onMoved: popup.clockScale = value
                }

                Text {
                    text: "Large"
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                }
            }
        }

        // No settings message for non-flip-clock screensavers
        Text {
            visible: popup.itemType !== "screensaverFlipClock"
            text: "No additional settings for this screensaver."
            font: Theme.bodyFont
            color: Theme.textSecondaryColor
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Item { Layout.fillWidth: true }

            Rectangle {
                width: Theme.scaled(80)
                height: Theme.scaled(36)
                radius: Theme.cardRadius
                color: Theme.surfaceColor
                border.color: Theme.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "Cancel"
                    font: Theme.bodyFont
                    color: Theme.textColor
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: popup.close()
                }
            }

            Rectangle {
                width: Theme.scaled(80)
                height: Theme.scaled(36)
                radius: Theme.cardRadius
                color: Theme.primaryColor
                visible: popup.itemType === "screensaverFlipClock"

                Text {
                    anchors.centerIn: parent
                    text: "Save"
                    font: Theme.bodyFont
                    color: "white"
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: popup.save()
                }
            }
        }
    }
}
