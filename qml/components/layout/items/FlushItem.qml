import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
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
            presetPopup.visible ? presetPopup.close() : presetPopup.open()
        } else if (root.idlePage) {
            root.idlePage.activePresetFunction =
                (root.idlePage.activePresetFunction === "flush") ? "" : "flush"
        }
    }

    function goToFlush() {
        if (typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/FlushPage.qml"))
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
                source: "qrc:/icons/flush.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                opacity: DE1Device.guiEnabled ? 1.0 : 0.5
                Accessible.ignored: true
            }
            Tr {
                key: "idle.button.flush"
                fallback: "Flush"
                font: Theme.bodyFont
                color: DE1Device.guiEnabled ? Theme.textColor : Theme.textSecondaryColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            enabled: DE1Device.guiEnabled
            supportLongPress: true
            supportDoubleClick: true
            accessibleName: TranslationManager.translate("idle.button.flush", "Flush")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: root.goToFlush()
            onAccessibleLongPressed: root.goToFlush()
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
            translationKey: "idle.button.flush"
            translationFallback: "Flush"
            iconSource: "qrc:/icons/flush.svg"
            enabled: DE1Device.guiEnabled
            supportDoubleClick: true
            onClicked: root.togglePresets()
            onPressAndHold: root.goToFlush()
            onDoubleClicked: root.goToFlush()

            Accessible.description: TranslationManager.translate("idle.accessible.flush.description", "Flush the group head. Long-press to configure.")
        }
    }

    // --- PRESET POPUP ---
    Popup {
        id: presetPopup
        modal: false
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

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
            maxWidth: Theme.scaled(600)
            presets: Settings.flushPresets
            selectedIndex: Settings.selectedFlushPreset

            onPresetSelected: function(index) {
                var wasAlreadySelected = (index === Settings.selectedFlushPreset)
                Settings.selectedFlushPreset = index
                var preset = Settings.getFlushPreset(index)
                if (preset) {
                    Settings.flushFlow = preset.flow
                    Settings.flushSeconds = preset.seconds
                }
                MainController.applyFlushSettings()

                if (wasAlreadySelected) {
                    if (MachineState.isReady) {
                        DE1Device.startFlush()
                    } else {
                        console.log("Cannot start flush - machine not ready, phase:", MachineState.phase)
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                            AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                    }
                }
                presetPopup.close()
            }
        }
    }
}
