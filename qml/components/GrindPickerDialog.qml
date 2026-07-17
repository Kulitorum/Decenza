import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Value picker opened from the grindQuickSelect layout widget, styled as native
// picker wheels (Qt Tumbler). A variable-RPM grinder's dial-in has two halves,
// so it shows up to two wheels SIDE BY SIDE — Grind (always) and RPM (when the
// grinder is RPM-capable) — under a shared selection band.
//
// Each wheel opens centred on the grinder's current value. Because a two-axis
// dial-in needs both a grind AND an RPM chosen, nothing is applied until the
// user presses Done: that reads whatever each wheel has settled on and applies
// both halves at once. Dismissing by Escape / tap-outside cancels (no change).
//
// Rows are ordered fine -> coarse (smallest value / lowest RPM first), so the
// top of the wheel is the finest setting.
Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    // Narrow for a single Grind wheel; wider when the RPM wheel is present.
    width: Math.min(root.rpmRows.length > 0 ? Theme.scaled(420) : Theme.scaled(280),
                    parent ? parent.width * 0.95 : Theme.scaled(280))
    height: Math.min(Theme.scaled(480), parent ? parent.height * 0.92 : Theme.scaled(480))
    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    padding: 0

    // [{ value: string, isCurrent: bool }] — fine -> coarse order, per wheel.
    property var grindRows: []
    property var rpmRows: []
    // Reserved for API compatibility with the widget (the wheel makes the
    // fine/coarse direction self-evident, so no separate labels are drawn).
    property bool finerHint: false

    signal grindPicked(string value)
    signal rpmPicked(string value)

    readonly property bool _hasRpm: rpmRows.length > 0

    // Index of the current value within a rows array (-1 if none is current).
    function _currentIndex(rows) {
        for (var i = 0; i < rows.length; i++)
            if (rows[i].isCurrent === true)
                return i
        return -1
    }

    // Centre each wheel on its current value once the dialog is laid out.
    onOpened: Qt.callLater(function() {
        var gi = root._currentIndex(root.grindRows)
        if (gi >= 0) grindTumbler.currentIndex = gi
        var ri = root._currentIndex(root.rpmRows)
        if (ri >= 0) rpmTumbler.currentIndex = ri
    })

    // Apply BOTH halves from whatever the wheels have settled on, then close.
    // The only commit path — a single-axis grinder still confirms via Done.
    function _applyAndClose() {
        if (root.grindRows.length > 0 && grindTumbler.currentIndex >= 0
                && grindTumbler.currentIndex < root.grindRows.length)
            root.grindPicked(String(root.grindRows[grindTumbler.currentIndex].value))
        if (root._hasRpm && rpmTumbler.currentIndex >= 0
                && rpmTumbler.currentIndex < root.rpmRows.length)
            root.rpmPicked(String(root.rpmRows[rpmTumbler.currentIndex].value))
        root.close()
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    // Shared wheel-row delegate: the centred item (displacement ~0) is bold and
    // accent-coloured; the rest fade with distance from the selection band.
    Component {
        id: wheelDelegate
        Item {
            required property var modelData
            required property int index
            readonly property real _dist: Math.abs(Tumbler.displacement)
            readonly property bool _centered: _dist < 0.5
            Text {
                anchors.centerIn: parent
                text: String(modelData.value)
                color: _centered ? Theme.primaryColor : Theme.textColor
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.bodyFont.pixelSize
                font.bold: _centered
                opacity: 1.0 - Math.min(0.72, _dist * 0.36)
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium

        // --- Fixed header ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingLarge
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            spacing: Theme.scaled(2)
            Text {
                text: TranslationManager.translate("grind.picker.title", "Grind Setting")
                color: Theme.textColor
                font: Theme.titleFont
            }
            Text {
                text: TranslationManager.translate("grind.picker.subtitle", "Spin to choose, then Done")
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
        }

        // --- Column labels (one per wheel, aligned above them) ---
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            spacing: Theme.spacingMedium
            visible: root.grindRows.length > 0 || root._hasRpm
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: TranslationManager.translate("grind.quickSelect.label", "Grind").toUpperCase()
                color: Theme.textColor
                font: Theme.subtitleFont
            }
            Text {
                visible: root._hasRpm
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: TranslationManager.translate("grind.quickSelect.rpmLabel", "RPM").toUpperCase()
                color: Theme.textColor
                font: Theme.subtitleFont
            }
        }

        // --- Body: side-by-side wheels under a shared selection band ---
        Item {
            id: wheelArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            visible: root.grindRows.length > 0 || root._hasRpm

            // Selection band: one item tall, centred on the wheels' middle row.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height / grindTumbler.visibleItemCount
                radius: Theme.buttonRadius
                color: Theme.primaryColor
                opacity: 0.14
            }

            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingMedium

                Tumbler {
                    id: grindTumbler
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.grindRows
                    visibleItemCount: 5
                    wrap: false
                    delegate: wheelDelegate
                    Accessible.role: Accessible.Slider
                    Accessible.name: TranslationManager.translate("grind.quickSelect.label", "Grind")
                }

                Tumbler {
                    id: rpmTumbler
                    visible: root._hasRpm
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: root.rpmRows
                    visibleItemCount: 5
                    wrap: false
                    delegate: wheelDelegate
                    Accessible.role: Accessible.Slider
                    Accessible.name: TranslationManager.translate("grind.quickSelect.rpmLabel", "RPM")
                }
            }
        }

        // --- Empty state: no value could be generated ---
        Text {
            visible: root.grindRows.length === 0 && !root._hasRpm
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            text: TranslationManager.translate("grind.picker.empty",
                "Set a grind value in Brew Settings first, then this list fills in around it.")
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
        }

        // --- Fixed footer: Cancel dismisses, Done applies both wheels ---
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            Layout.bottomMargin: Theme.spacingLarge
            spacing: Theme.spacingMedium

            // Cancel — secondary style; closes with no change.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(48)
                radius: Theme.buttonRadius
                color: cancelMa.pressed ? Qt.darker(Theme.surfaceColor, 1.1) : "transparent"
                border.width: 1
                border.color: Theme.borderColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.cancel", "Cancel")
                Accessible.focusable: true
                Accessible.onPressAction: cancelMa.clicked(null)
                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    color: Theme.textColor
                    font: Theme.bodyFont
                }
                MouseArea { id: cancelMa; anchors.fill: parent; onClicked: root.close() }
            }

            // Done — primary style; applies both wheels then closes.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(48)
                radius: Theme.buttonRadius
                color: doneMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.done", "Done")
                Accessible.focusable: true
                Accessible.onPressAction: doneMa.clicked(null)
                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.done", "Done")
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                }
                MouseArea { id: doneMa; anchors.fill: parent; onClicked: root._applyAndClose() }
            }
        }
    }
}
