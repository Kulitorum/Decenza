import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Value picker opened from the grindQuickSelect layout widget. A variable-RPM
// grinder's dial-in has two halves, so this shows up to two columns SIDE BY
// SIDE — Grind (always) and RPM (when the grinder is RPM-capable). Picking a row
// applies only that half.
//
// Layout: a fixed title header at the top and a fixed Close button at the
// bottom; between them, each column's section header + Finer label are pinned
// above its list and the Coarser label pinned below, so ONLY the value list
// scrolls. Each list opens centred on its current value. Rows are ordered
// fine -> coarse (smallest value / lowest RPM first).
Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    // Narrow for a single Grind column; wider when the RPM column is present.
    width: Math.min(root.rpmRows.length > 0 ? Theme.scaled(460) : Theme.scaled(320),
                    parent ? parent.width * 0.95 : Theme.scaled(320))
    // Bounded so the dialog always fits the screen — the lists scroll inside.
    height: Math.min(Theme.scaled(560), parent ? parent.height * 0.92 : Theme.scaled(560))
    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    padding: 0

    // [{ value: string, isCurrent: bool }] — fine -> coarse order, per column.
    property var grindRows: []
    property var rpmRows: []
    // Whether to show the grind column's finer/coarser end annotations.
    property bool finerHint: false

    signal grindPicked(string value)
    signal rpmPicked(string value)

    // Centre each list on its current value once the dialog is laid out.
    onOpened: Qt.callLater(function() {
        grindColumn.centerCurrent()
        if (rpmColumn.visible)
            rpmColumn.centerCurrent()
    })

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    // One scrollable value column: a pinned header + Finer slot, the scrolling
    // list, and a pinned Coarser slot. The end slots reserve their height even
    // when hidden (the RPM column) so the two columns' lists stay aligned.
    component PickerColumn: ColumnLayout {
        id: col
        property string sectionLabel: ""
        property var rows: []
        property bool showEnds: false
        property string kind: "grind"    // "grind" | "rpm"
        spacing: Theme.spacingSmall

        function centerCurrent() {
            for (var i = 0; i < rows.length; i++) {
                if (rows[i].isCurrent === true) {
                    list.positionViewAtIndex(i, ListView.Center)
                    return
                }
            }
            list.positionViewAtBeginning()
        }

        // --- Pinned: section header ---
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: col.sectionLabel.toUpperCase()
            color: Theme.textColor
            font: Theme.subtitleFont
        }

        // --- Pinned: Finer label (reserves height in both columns to align) ---
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: finerText.implicitHeight
            Text {
                id: finerText
                anchors.centerIn: parent
                visible: col.showEnds && col.rows.length > 2
                text: TranslationManager.translate("grind.picker.finer", "Finer").toUpperCase()
                color: Theme.textSecondaryColor
                font: Theme.captionFont
            }
        }

        // --- Scrolling: the value list ---
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: col.rows
            spacing: Theme.spacingSmall
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: rowRect
                required property var modelData
                readonly property bool isCurrent: modelData.isCurrent === true
                width: ListView.view.width
                height: Theme.scaled(48)
                radius: Theme.buttonRadius
                color: rowMa.pressed ? Qt.darker(Theme.backgroundColor, 1.1) : Theme.backgroundColor
                border.width: isCurrent ? 2 : 1
                border.color: isCurrent ? Theme.primaryColor : Theme.borderColor

                Accessible.role: Accessible.Button
                Accessible.name: String(modelData.value)
                                 + (isCurrent ? ", " + TranslationManager.translate("grind.picker.current", "current") : "")
                Accessible.focusable: true
                Accessible.onPressAction: rowMa.clicked(null)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    spacing: Theme.spacingSmall
                    Text {
                        text: String(rowRect.modelData.value)
                        color: rowRect.isCurrent ? Theme.primaryColor : Theme.textColor
                        font.family: Theme.bodyFont.family
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: rowRect.isCurrent
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        visible: rowRect.isCurrent
                        text: TranslationManager.translate("grind.picker.current", "current").toUpperCase()
                        color: Theme.primaryColor
                        font: Theme.captionFont
                    }
                }

                MouseArea {
                    id: rowMa
                    anchors.fill: parent
                    onClicked: {
                        if (col.kind === "rpm")
                            root.rpmPicked(String(rowRect.modelData.value))
                        else
                            root.grindPicked(String(rowRect.modelData.value))
                        root.close()
                    }
                }
            }
        }

        // --- Pinned: Coarser label ---
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: coarserText.implicitHeight
            Text {
                id: coarserText
                anchors.centerIn: parent
                visible: col.showEnds && col.rows.length > 2
                text: TranslationManager.translate("grind.picker.coarser", "Coarser").toUpperCase()
                color: Theme.textSecondaryColor
                font: Theme.captionFont
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
                text: TranslationManager.translate("grind.picker.subtitle", "Tap a value to set the grinder")
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
        }

        // --- Body: side-by-side scrolling columns ---
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            spacing: Theme.spacingMedium
            visible: root.grindRows.length > 0 || root.rpmRows.length > 0

            PickerColumn {
                id: grindColumn
                Layout.fillWidth: true
                Layout.fillHeight: true
                sectionLabel: TranslationManager.translate("grind.quickSelect.label", "Grind")
                rows: root.grindRows
                showEnds: root.finerHint
                kind: "grind"
            }

            PickerColumn {
                id: rpmColumn
                visible: root.rpmRows.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                sectionLabel: TranslationManager.translate("grind.quickSelect.rpmLabel", "RPM")
                rows: root.rpmRows
                showEnds: false
                kind: "rpm"
            }
        }

        // --- Empty state: no value could be generated ---
        Text {
            visible: root.grindRows.length === 0 && root.rpmRows.length === 0
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

        // --- Fixed footer: Close ---
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
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
            }
            MouseArea { id: closeMa; anchors.fill: parent; onClicked: root.close() }
        }
    }
}
