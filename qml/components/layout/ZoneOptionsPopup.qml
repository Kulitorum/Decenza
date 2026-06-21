import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Per-zone options (composable-brew-bar): distribution, alignment, style preset,
// and a one-tap "populate from preset". Opened from the layout editor by the
// zone options button / long-press. Writes via Settings.network.setZoneOption,
// so the live layout re-renders immediately.
Dialog {
    id: popup

    property string zoneName: ""
    property string zoneLabel: ""

    // Local mirrors of the current option values (read on open).
    property string distribution: "packed"
    property string alignment: "center"
    property string zoneStyle: "standard"

    function openForZone(name, label) {
        popup.zoneName = name
        popup.zoneLabel = label
        popup.distribution = Settings.network.getZoneOption(name, "distribution", "packed")
        popup.alignment = Settings.network.getZoneOption(name, "alignment", "center")
        popup.zoneStyle = Settings.network.getZoneOption(name, "style", "standard")
        popup.open()
    }

    function setOption(key, value) {
        Settings.network.setZoneOption(popup.zoneName, key, value)
    }

    // Fill the zone with the built-in "Brew bar" arrangement (the PR #1364 view).
    function populateBrewBar() {
        var items = [
            { type: "profileName",      id: "lmb_profile" },
            { type: "scaleWeight",      id: "lmb_scale", dataMode: "contextAware" },
            { type: "ratioQuickSelect", id: "lmb_ratio" },
            { type: "doseWeight",       id: "lmb_dose" },
            { type: "milkWeight",       id: "lmb_milk" }
        ]
        Settings.network.setZoneItems(popup.zoneName, items)
        setOption("distribution", "equalWidth"); popup.distribution = "equalWidth"
        setOption("style", "accentBar");        popup.zoneStyle = "accentBar"
    }

    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(Theme.scaled(480), parent.width - Theme.spacingLarge * 2)
    padding: Theme.spacingMedium

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    // A labelled row of mutually-exclusive choices.
    component OptionRow: ColumnLayout {
        id: optRow
        property string title
        property string current
        property var choices   // [{value, label}]
        signal picked(string value)
        Layout.fillWidth: true
        spacing: Theme.scaled(4)
        Text {
            text: optRow.title
            color: Theme.textSecondaryColor
            font: Theme.labelFont
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Repeater {
                model: optRow.choices
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool sel: optRow.current === modelData.value
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(40)
                    radius: Theme.buttonRadius
                    color: sel ? Theme.primaryColor : "transparent"
                    border.color: sel ? Theme.primaryColor : Theme.borderColor
                    border.width: 1
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.label
                    Accessible.focusable: true
                    Accessible.onPressAction: choiceMa.clicked(null)
                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: parent.sel ? Theme.primaryContrastColor : Theme.textColor
                        font: Theme.labelFont
                    }
                    MouseArea {
                        id: choiceMa
                        anchors.fill: parent
                        onClicked: optRow.picked(modelData.value)
                    }
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium

        Text {
            text: TranslationManager.translate("layoutEditor.zoneOptionsTitle", "%1 options").arg(popup.zoneLabel)
            color: Theme.textColor
            font.pixelSize: Theme.scaled(20)
            font.bold: true
        }

        OptionRow {
            title: TranslationManager.translate("layoutEditor.zoneDistribution", "Distribution")
            current: popup.distribution
            choices: [
                { value: "packed",     label: TranslationManager.translate("layoutEditor.distPacked", "Packed") },
                { value: "equalWidth", label: TranslationManager.translate("layoutEditor.distEqual", "Equal width") },
                { value: "spaced",     label: TranslationManager.translate("layoutEditor.distSpaced", "Spaced") }
            ]
            onPicked: function(v) { popup.distribution = v; popup.setOption("distribution", v) }
        }

        OptionRow {
            title: TranslationManager.translate("layoutEditor.zoneAlignment", "Alignment")
            current: popup.alignment
            choices: [
                { value: "left",   label: TranslationManager.translate("layoutEditor.alignLeft", "Left") },
                { value: "center", label: TranslationManager.translate("layoutEditor.alignCenter", "Center") },
                { value: "right",  label: TranslationManager.translate("layoutEditor.alignRight", "Right") }
            ]
            onPicked: function(v) { popup.alignment = v; popup.setOption("alignment", v) }
        }

        OptionRow {
            title: TranslationManager.translate("layoutEditor.zoneStyle", "Style")
            current: popup.zoneStyle
            choices: [
                { value: "standard",  label: TranslationManager.translate("layoutEditor.styleStandard", "Standard") },
                { value: "surface",   label: TranslationManager.translate("layoutEditor.styleSurface", "Surface") },
                { value: "accentBar", label: TranslationManager.translate("layoutEditor.styleAccent", "Accent bar") }
            ]
            onPicked: function(v) { popup.zoneStyle = v; popup.setOption("style", v) }
        }

        // Populate from a built-in preset.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(4)
            Text {
                text: TranslationManager.translate("layoutEditor.zonePopulate", "Populate")
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(44)
                radius: Theme.buttonRadius
                color: brewMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("layoutEditor.populateBrewBar", "Fill with Brew bar")
                Accessible.focusable: true
                Accessible.onPressAction: brewMa.clicked(null)
                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("layoutEditor.populateBrewBar", "Fill with Brew bar")
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                }
                MouseArea { id: brewMa; anchors.fill: parent; onClicked: { popup.populateBrewBar(); popup.close() } }
            }

            // Clear all widgets from this zone.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(44)
                radius: Theme.buttonRadius
                color: clearMa.pressed ? Qt.rgba(Theme.errorColor.r, Theme.errorColor.g, Theme.errorColor.b, 0.15) : "transparent"
                border.color: Theme.errorColor
                border.width: 1
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("layoutEditor.clearZone", "Clear zone")
                Accessible.focusable: true
                Accessible.onPressAction: clearMa.clicked(null)
                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("layoutEditor.clearZone", "Clear zone")
                    color: Theme.errorColor
                    font: Theme.bodyFont
                }
                MouseArea { id: clearMa; anchors.fill: parent; onClicked: { Settings.network.setZoneItems(popup.zoneName, []); popup.close() } }
            }
        }
    }
}
