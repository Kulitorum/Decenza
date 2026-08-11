// The value-row delegate declares its injected model role required, so Bound
// cannot break role injection here and this file's ids resolve statically.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Value picker opened from the temperatureQuickSelect layout widget. Lists the
// brew temperatures the widget generated (current ±5 steps, clamped to the brew
// range) and applies the tapped value by emitting its Celsius value. A plain
// VALUE picker — not a preset editor; the value-picker sibling of
// GrindPickerDialog (a plain tap-a-row list rather than Tumbler wheels, no shared
// code).
//
// The list (`rows`) is computed by the item; each row is
// { value: <Celsius double>, label: <display string>, isCurrent: bool },
// ordered cool -> hot (ascending Celsius). The emitted value is CELSIUS (the
// internal/stored unit); labels are already in the user's display unit.
//
// Roots at DecenzaDialog (the shared dialog base every dialog in the app uses),
// not QtQuick.Controls Dialog: the app base keeps AOT compilation and the app's
// dialog theming (dim, enter/exit transitions). See DecenzaDialog.qml.
DecenzaDialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(360), parent ? parent.width * 0.95 : Theme.scaled(360))
    height: Math.min(contentCol.implicitHeight + Theme.scaled(40),
                     parent ? parent.height * 0.92 : Theme.scaled(640))
    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    padding: 0

    // [{ value: double (Celsius), label: string, isCurrent: bool }] — cool -> hot.
    property var rows: []

    // Emits the picked temperature in CELSIUS (the internal/stored unit).
    signal valuePicked(double value)

    background: Rectangle {
        color: Theme.dialogBackgroundColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: Flickable {
        contentWidth: width
        contentHeight: contentCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: contentCol
            width: parent.width
            spacing: Theme.spacingSmall

            // --- Header ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingLarge
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                spacing: Theme.scaled(2)
                Text {
                    text: TranslationManager.translate("temp.picker.title", "Brew Temperature")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(24); font.bold: true
                }
                Text {
                    text: TranslationManager.translate("temp.picker.subtitle", "Tap a value to set the brew temperature")
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                }
            }

            // --- Cooler end label (top: rows ascend, so lowest = coolest is first). ---
            Text {
                visible: root.rows.length > 2
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                horizontalAlignment: Text.AlignHCenter
                text: TranslationManager.translate("temp.picker.cooler", "Cooler").toUpperCase()
                color: Theme.textSecondaryColor
                font: Theme.captionFont
            }

            // --- Value rows ---
            Repeater {
                model: root.rows
                delegate: Rectangle {
                    id: rowRect
                    required property var modelData
                    readonly property bool isCurrent: modelData.isCurrent === true
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingLarge
                    Layout.rightMargin: Theme.spacingLarge
                    Layout.preferredHeight: Theme.scaled(48)
                    radius: Theme.buttonRadius
                    color: rowMa.pressed ? Qt.darker(Theme.backgroundColor, 1.1) : Theme.backgroundColor
                    border.width: isCurrent ? 2 : 1
                    border.color: isCurrent ? Theme.primaryColor : Theme.borderColor

                    Accessible.role: Accessible.Button
                    Accessible.name: String(modelData.label)
                                     + (isCurrent ? ", " + TranslationManager.translate("temp.picker.current", "current") : "")
                    Accessible.focusable: true
                    Accessible.onPressAction: rowMa.clicked(null)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingSmall
                        Text {
                            text: String(rowRect.modelData.label)
                            color: rowRect.isCurrent ? Theme.primaryColor : Theme.textColor
                            font.pixelSize: Theme.scaled(20)
                            font.bold: rowRect.isCurrent
                            Accessible.ignored: true   // the delegate Rectangle carries Accessible.name
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: rowRect.isCurrent
                            text: TranslationManager.translate("temp.picker.current", "current").toUpperCase()
                            color: Theme.primaryColor
                            font: Theme.captionFont
                            Accessible.ignored: true   // the delegate Rectangle carries Accessible.name
                        }
                    }

                    MouseArea {
                        id: rowMa
                        anchors.fill: parent
                        onClicked: {
                            root.valuePicked(rowRect.modelData.value)
                            root.close()
                        }
                    }
                }
            }

            // --- Warmer end label (bottom: highest = hottest). ---
            Text {
                visible: root.rows.length > 2
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                horizontalAlignment: Text.AlignHCenter
                text: TranslationManager.translate("temp.picker.warmer", "Warmer").toUpperCase()
                color: Theme.textSecondaryColor
                font: Theme.captionFont
            }

            // --- Close ---
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                Layout.topMargin: Theme.spacingSmall
                Layout.bottomMargin: Theme.spacingLarge
                Layout.preferredHeight: Theme.scaled(48)
                radius: Theme.buttonRadius
                color: closeMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.close", "Close")
                Accessible.focusable: true
                Accessible.onPressAction: closeMa.clicked(null)
                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.close", "Close")
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                    Accessible.ignored: true   // the Close Rectangle carries Accessible.name
                }
                MouseArea { id: closeMa; anchors.fill: parent; onClicked: root.close() }
            }
        }
    }
}
