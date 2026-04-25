import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    property var idlePage: {
        var p = root.parent
        while (p) {
            if (p.objectName === "idlePage") return p
            p = p.parent
        }
        return null
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    function togglePresets() {
        if (root.isCompact) {
            if (Settings.dye.idleBeanPresets.length === 0) {
                goToBeanInfo()
            } else {
                presetPopup.visible ? presetPopup.close() : presetPopup.open()
            }
        } else if (Settings.dye.idleBeanPresets.length === 0) {
            goToBeanInfo()
        } else if (root.idlePage) {
            root.idlePage.activePresetFunction =
                (root.idlePage.activePresetFunction === "beans") ? "" : "beans"
        }
    }

    function goToBeanInfo() {
        if (typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/BeanInfoPage.qml"))
        }
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        RowLayout {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                source: "qrc:/icons/coffeebeans.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: Theme.textColor
                }
            }
            Tr {
                key: "idle.button.beaninfo"
                fallback: "Beans"
                font: Theme.bodyFont
                color: Theme.textColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            supportLongPress: true
            supportDoubleClick: true
            accessibleName: TranslationManager.translate("idle.button.beaninfo", "Beans")
            accessibleDescription: TranslationManager.translate("idle.accessible.beaninfo.hint", "Tap to toggle presets. Double-tap or long-press for bean info.")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: root.goToBeanInfo()
            onAccessibleLongPressed: root.goToBeanInfo()
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: Theme.scaled(150)
        implicitHeight: Theme.scaled(120)

        ActionButton {
            anchors.fill: parent
            translationKey: "idle.button.beaninfo"
            translationFallback: "Beans"
            iconSource: "qrc:/icons/coffeebeans.svg"
            iconSize: Theme.scaled(43)
            backgroundColor: Settings.dye.selectedBeanPreset === -1 ? Theme.highlightColor : Theme.primaryColor
            supportDoubleClick: true
            onClicked: root.togglePresets()
            onPressAndHold: root.goToBeanInfo()
            onDoubleClicked: root.goToBeanInfo()

            Accessible.description: TranslationManager.translate("idle.accessible.beaninfo.description", "Set up bean and grinder info for your shots. Double-tap or long-press for bean info.")
        }
    }

    // --- PRESET POPUP ---
    Popup {
        id: presetPopup
        modal: false
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

        // Full-mode beans path runs IdlePage.onActivePresetFunctionChanged which announces
        // the preset list to TalkBack. The compact-mode popup bypasses that path, so
        // announce here directly to keep feature parity for screen-reader users.
        onOpened: {
            if (typeof AccessibilityManager === "undefined" || !AccessibilityManager.enabled) return
            var presets = Settings.dye.idleBeanPresets
            if (presets.length === 0) return
            var names = []
            var selectedName = ""
            for (var i = 0; i < presets.length; ++i) {
                names.push(presets[i].name)
                if (presets[i].originalIndex === Settings.dye.selectedBeanPreset) selectedName = presets[i].name
            }
            var announcement = presets.length + " " + TranslationManager.translate("idle.accessible.presets", "presets") + ": " + names.join(", ")
            if (selectedName !== "") {
                announcement += ". " + selectedName + " " + TranslationManager.translate("idle.accessible.isSelected", "is selected")
            }
            AccessibilityManager.announce(announcement)
        }

        width: {
            var win = root.Window.window
            var w = Theme.scaled(600) + 2 * padding
            return win ? Math.min(w, win.width) : w
        }

        y: {
            var _v = visible // Force re-evaluation when popup opens (mapToItem is not reactive)
            var win = root.Window.window
            if (win) {
                var globalY = root.mapToItem(null, 0, 0).y
                var spaceBelow = win.height - globalY - root.height - Theme.spacingSmall
                var spaceAbove = globalY - Theme.spacingSmall
                if (height > spaceBelow && spaceAbove > spaceBelow)
                    return -height - Theme.spacingSmall
            }
            return parent.height + Theme.spacingSmall
        }

        x: {
            var _v = visible // Force re-evaluation when popup opens (mapToItem is not reactive)
            var win = root.Window.window
            if (win) {
                var globalX = root.mapToItem(null, 0, 0).x
                var centered = -width / 2 + parent.width / 2
                if (globalX + centered + width > win.width)
                    centered = win.width - globalX - width
                if (globalX + centered < 0)
                    centered = -globalX
                return centered
            }
            return -width / 2 + parent.width / 2
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: PresetPillRow {
            id: beansPillRow
            maxWidth: Theme.scaled(600)
            presets: Settings.dye.idleBeanPresets
            modified: Settings.dye.beansModified
            // selectedIndex refers to position within the filtered list
            selectedIndex: {
                var list = Settings.dye.idleBeanPresets
                for (var i = 0; i < list.length; ++i) {
                    if (list[i].originalIndex === Settings.dye.selectedBeanPreset) return i
                }
                return -1
            }

            onPresetSelected: function(index) {
                var row = beansPillRow.presets[index]
                if (!row) return
                var originalIndex = row.originalIndex !== undefined ? row.originalIndex : index
                Settings.dye.selectedBeanPreset = originalIndex
                Settings.dye.applyBeanPreset(originalIndex)
                presetPopup.close()
            }
        }
    }
}
