pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Service-free package detail dialog shared by native and browser hosts.
SharedDecenzaDialog {
    id: root
    required property EquipmentDialogStyle equipmentStyle
    required property EquipmentDialogText strings
    property var packageData: ({})
    property var puckPrepRows: []

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(equipmentStyle.dialogStyle.scaled(460),
                    parent ? parent.width * 0.95 : equipmentStyle.dialogStyle.scaled(460))
    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    padding: 0
    dialogStyle: equipmentStyle.dialogStyle

    readonly property string pkgName: (packageData && packageData.name && String(packageData.name).length > 0)
        ? String(packageData.name)
        : [(packageData && packageData.grinderBrand) || "", (packageData && packageData.grinderModel) || ""]
              .filter(function(value) { return value.length > 0 }).join(" ")
    readonly property string puckPrepSummary: {
        var canonical = String(packageData && packageData.puckPrepCanonical || "").split(",")
        var labels = []
        var rows = root.puckPrepRows
        for (let i = 0; i < rows.length; ++i)
            if (canonical.indexOf(rows[i].key) >= 0) labels.push(rows[i].label)
        return labels.join(" · ")
    }

    background: Rectangle {
        color: root.equipmentStyle.dialogStyle.surfaceColor
        radius: root.equipmentStyle.cardRadius
        border.color: root.equipmentStyle.dialogStyle.borderColor
        border.width: 1
    }

    contentItem: Item {
        implicitHeight: infoColumn.implicitHeight + 2 * root.equipmentStyle.spacingMedium
        ColumnLayout {
            id: infoColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: root.equipmentStyle.spacingMedium
            spacing: root.equipmentStyle.spacingSmall

            Text {
                Layout.fillWidth: true
                text: root.strings.infoTitle
                font: root.equipmentStyle.titleFont
                color: root.equipmentStyle.dialogStyle.textColor
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: root.pkgName.length > 0
                text: root.pkgName
                textFormat: Text.PlainText
                font: root.equipmentStyle.dialogStyle.subtitleFont
                color: root.equipmentStyle.dialogStyle.textColor
            }
            InfoRow {
                label: root.strings.infoGrinder
                value: [(root.packageData && root.packageData.grinderBrand) || "",
                        (root.packageData && root.packageData.grinderModel) || ""]
                    .filter(function(value) { return value.length > 0 }).join(" ")
            }
            InfoRow {
                label: root.strings.infoBurrs
                value: (root.packageData && root.packageData.grinderBurrs) || ""
            }
            InfoRow {
                label: root.strings.infoLastGrind
                value: {
                    var grind = (root.packageData && root.packageData.lastGrindSetting) || ""
                    var showRpm = root.packageData && root.packageData.rpmCapable && root.packageData.lastRpm > 0
                    return showRpm ? (grind.length > 0 ? grind + "  ·  " + root.packageData.lastRpm + " rpm"
                                                       : root.packageData.lastRpm + " rpm")
                                   : grind
                }
            }
            InfoRow {
                label: root.strings.infoBasket
                value: [(root.packageData && root.packageData.basketBrand) || "",
                        (root.packageData && root.packageData.basketModel) || ""]
                    .filter(function(value) { return value.length > 0 }).join(" ")
            }
            InfoRow {
                label: root.strings.infoBasketDetails
                value: (root.packageData && root.packageData.basketSummary) || ""
            }
            InfoRow {
                label: root.strings.infoPuckPrep
                value: root.puckPrepSummary
            }
            SharedEquipmentDialogButton {
                equipmentStyle: root.equipmentStyle
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: root.equipmentStyle.spacingSmall
                text: root.strings.close
                accessibleName: text
                onClicked: root.close()
            }
        }
    }

    component InfoRow: RowLayout {
        id: row
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: root.equipmentStyle.spacingMedium
        visible: value.length > 0
        Text {
            text: row.label + ":"
            textFormat: Text.PlainText
            font: root.equipmentStyle.labelFont
            color: root.equipmentStyle.dialogStyle.textSecondaryColor
            Layout.minimumWidth: root.equipmentStyle.dialogStyle.scaled(130)
            Accessible.ignored: true
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: row.value
            textFormat: Text.PlainText
            font: root.equipmentStyle.dialogStyle.bodyFont
            color: root.equipmentStyle.dialogStyle.textColor
            Accessible.role: Accessible.StaticText
            Accessible.name: row.label + ": " + row.value
        }
    }
}
