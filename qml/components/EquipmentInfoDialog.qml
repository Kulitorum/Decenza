import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Read-only details of a single equipment package (add-equipment-packages).
// Opened from the info button on any equipment line (Brew Settings, Shot Review,
// Edit Bag). Call openFor(packageId) — it resolves the package async and shows
// its name, grinder identity, RPM capability, and last dial.
Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(460), parent ? parent.width * 0.95 : Theme.scaled(460))
    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    padding: 0

    property int packageId: -1
    property var pkg: ({})

    function openFor(id) {
        packageId = id
        pkg = ({})
        if (id > 0 && MainController.equipmentStorage)
            MainController.equipmentStorage.requestPackage(id)
        open()
    }

    Connections {
        target: MainController.equipmentStorage
        function onPackageReady(pid, p) {
            if (pid !== root.packageId) return
            root.pkg = p || ({})
        }
    }

    readonly property string pkgName: (pkg && pkg.name && String(pkg.name).length > 0)
        ? String(pkg.name)
        : [(pkg && pkg.grinderBrand) || "", (pkg && pkg.grinderModel) || ""]
              .filter(function(s) { return s.length > 0 }).join(" ")

    // Puck-prep set flags as a "WDT • Shaker" summary (add-puckprep-equipment),
    // empty when the package has no puck prep.
    readonly property string puckPrepSummary: {
        var _ = TranslationManager.translationVersion
        if (!pkg) return ""
        var labels = []
        if (pkg.puckPrep_wdt) labels.push(TranslationManager.translate("equipment.dialog.puckWdt", "WDT"))
        if (pkg.puckPrep_shaker) labels.push(TranslationManager.translate("equipment.dialog.puckShaker", "Shaker"))
        if (pkg.puckPrep_puckScreen) labels.push(TranslationManager.translate("equipment.dialog.puckScreen", "Puck screen"))
        if (pkg.puckPrep_paperFilter) labels.push(TranslationManager.translate("equipment.dialog.puckPaper", "Bottom paper filter"))
        if (pkg.puckPrep_rdt) labels.push(TranslationManager.translate("equipment.dialog.puckRdt", "RDT (spritz)"))
        return labels.join(" • ")
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: Item {
        implicitHeight: infoColumn.implicitHeight + 2 * Theme.spacingMedium

        ColumnLayout {
            id: infoColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Tr {
                Layout.fillWidth: true
                key: "equipment.info.title"
                fallback: "Equipment"
                font: Theme.titleFont
                color: Theme.textColor
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: root.pkgName.length > 0
                text: root.pkgName
                font: Theme.subtitleFont
                color: Theme.textColor
            }

            InfoRow {
                label: TranslationManager.translate("equipment.info.grinder", "Grinder")
                value: [(root.pkg && root.pkg.grinderBrand) || "", (root.pkg && root.pkg.grinderModel) || ""]
                           .filter(function(s) { return s.length > 0 }).join(" ")
            }
            InfoRow {
                label: TranslationManager.translate("equipment.info.burrs", "Burrs")
                value: (root.pkg && root.pkg.grinderBurrs) || ""
            }
            // Last dial — grind setting, plus the last RPM appended only when the
            // grinder is rpm-adjustable (saves a row vs. a separate RPM line).
            InfoRow {
                label: TranslationManager.translate("equipment.info.lastGrind", "Last grind")
                value: {
                    var g = (root.pkg && root.pkg.lastGrindSetting) || ""
                    var showRpm = root.pkg && root.pkg.rpmCapable && root.pkg.lastRpm > 0
                    return showRpm ? (g.length > 0 ? g + "  •  " + root.pkg.lastRpm + " rpm"
                                                   : root.pkg.lastRpm + " rpm")
                                   : g
                }
            }
            // Basket last — separate equipment, so it follows the grinder identity +
            // its dial. Both rows self-hide when the package has no basket; the
            // details row also hides for a custom basket with no resolved specs.
            InfoRow {
                label: TranslationManager.translate("equipment.info.basket", "Basket")
                value: [(root.pkg && root.pkg.basketBrand) || "", (root.pkg && root.pkg.basketModel) || ""]
                           .filter(function(s) { return s.length > 0 }).join(" ")
            }
            InfoRow {
                label: TranslationManager.translate("equipment.info.basketDetails", "Basket details")
                value: (root.pkg && root.pkg.basketSummary) || ""
            }
            // Puck prep — the set flags. The derived `distribution` rollup is an
            // AI-only signal (advisor + MCP) and is deliberately NOT shown here.
            InfoRow {
                label: TranslationManager.translate("equipment.info.puckPrep", "Puck prep")
                value: root.puckPrepSummary
            }

            AccessibleButton {
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: Theme.spacingSmall
                text: TranslationManager.translate("common.button.close", "Close")
                accessibleName: text
                onClicked: root.close()
            }
        }
    }

    // Label/value row; hides itself when the value is empty.
    component InfoRow: RowLayout {
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: Theme.spacingMedium
        visible: value.length > 0

        Text {
            text: label + ":"
            font: Theme.labelFont
            color: Theme.textSecondaryColor
            Layout.preferredWidth: Theme.scaled(130)
            Accessible.ignored: true
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: value
            font: Theme.bodyFont
            color: Theme.textColor
            Accessible.role: Accessible.StaticText
            Accessible.name: label + ": " + value
        }
    }
}
