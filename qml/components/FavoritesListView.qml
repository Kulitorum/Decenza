import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQml.Models
import Decenza

// Reusable vertical reorderable list with live drag-and-drop.
// Used by Profile favorites and Bean presets — edits here improve both.
//
// Drag UX:
//  - Grab the drag handle at the left of any row.
//  - Hovering over another row performs a live swap; other rows slide out of the way
//    via ListView.moveDisplaced animation.
//  - Edge autoscroll triggers near the top/bottom of the list.
//  - On release, emits rowMoved(startIndex, endIndex) once; consumer persists to its
//    own store (Settings) and the resulting NOTIFY rebuilds the DelegateModel to match.
//
// Consumer API:
//   model: QVariantList of row objects
//   selectedIndex: external index of the selected row (or -1)
//   displayTextFn(row, index), accessibleNameFn(row, index), deleteAccessibleNameFn(row, index)
//   removeConfirmFn(row, index): return false to cancel delete
//   trailingActionDelegate: Component whose loaded root sees `parent.row`, `parent.rowIndex`, `parent.selected`
//   showDeleteButton: toggles the X button column
//   rowAccessibleDescription: TalkBack/VoiceOver hint describing the long-press/double-tap action
//
// Signals: rowSelected(int), rowLongPressed(int), rowMoved(int from, int to), rowDeleted(int)
Item {
    id: root

    property var model: []
    property int selectedIndex: -1
    property Component trailingActionDelegate: null
    property bool showDeleteButton: true

    property var displayTextFn: function(row, index) { return row && row.name ? row.name : "" }
    property var accessibleNameFn: function(row, index) { return row && row.name ? row.name : "" }
    property var deleteAccessibleNameFn: function(row, index) {
        return TranslationManager.translate("favorites.accessible.remove", "Remove") + " " +
               (row && row.name ? row.name : "")
    }
    property var removeConfirmFn: function(row, index) { return true }

    // TalkBack/VoiceOver hint for the row's secondary (long-press/double-tap) action.
    // Consumers should override with context-specific text (e.g. "rename preset", "edit profile").
    property string rowAccessibleDescription: TranslationManager.translate(
        "favorites.accessible.secondary_hint",
        "Double-tap or long-press for more options.")

    signal rowSelected(int index)
    signal rowLongPressed(int index)
    signal rowMoved(int from, int to)
    signal rowDeleted(int index)

    readonly property int rowHeight: Theme.scaled(60)
    readonly property int rowSpacing: Theme.scaled(8)

    implicitHeight: {
        var n = root.model ? root.model.length : 0
        if (n <= 0) return 0
        return n * rowHeight + (n - 1) * rowSpacing
    }

    property bool _dragging: false

    DelegateModel {
        id: visualModel
        model: root.model

        delegate: Item {
            id: rowDelegate
            width: listView.width
            height: root.rowHeight

            // itemsIndex reflects live position after reorder via visualModel.items.move()
            readonly property int liveIndex: DelegateModel.itemsIndex
            readonly property var rowData: modelData
            // Highlight suppressed during drag (selected index refers to the external
            // model; live swaps would make the highlight jump mid-drag).
            readonly property bool selected: !root._dragging && (liveIndex === root.selectedIndex)

            Rectangle {
                id: rowPill
                width: rowDelegate.width
                height: rowDelegate.height
                radius: Theme.scaled(8)
                color: rowDelegate.selected ? Theme.primaryColor : Theme.backgroundColor
                border.color: dragArea.drag.active ? Theme.primaryColor : Theme.textSecondaryColor
                border.width: dragArea.drag.active ? 2 : 1
                scale: dragArea.drag.active ? 1.03 : 1.0
                z: dragArea.drag.active ? 10 : 1
                opacity: dragArea.drag.active ? 0.95 : 1.0

                Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

                Drag.active: dragArea.drag.active
                Drag.source: rowDelegate
                Drag.hotSpot.x: width / 2
                Drag.hotSpot.y: height / 2

                states: State {
                    when: dragArea.drag.active
                    ParentChange { target: rowPill; parent: listView }
                    AnchorChanges { target: rowPill; anchors.horizontalCenter: undefined; anchors.verticalCenter: undefined }
                }

                layer.enabled: dragArea.drag.active
                layer.smooth: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Theme.shadowColor
                    shadowBlur: 0.8
                    shadowVerticalOffset: 4
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(10)
                    spacing: Theme.scaled(8)

                    // Drag handle (expanded touch target)
                    Item {
                        Layout.preferredWidth: Theme.scaled(24)
                        Layout.preferredHeight: Theme.scaled(24)

                        Image {
                            anchors.centerIn: parent
                            source: "qrc:/icons/list.svg"
                            sourceSize.width: Theme.scaled(18)
                            sourceSize.height: Theme.scaled(18)
                            opacity: rowDelegate.selected ? 1.0 : 0.5
                            layer.enabled: !rowDelegate.selected
                            layer.smooth: true
                            layer.effect: MultiEffect {
                                colorization: 1.0
                                colorizationColor: Theme.textSecondaryColor
                            }
                        }

                        MouseArea {
                            id: dragArea
                            anchors.fill: parent
                            anchors.margins: -Theme.scaled(6)
                            preventStealing: true
                            drag.target: rowPill
                            drag.axis: Drag.YAxis

                            property int _startIndex: -1

                            onPressed: {
                                _startIndex = rowDelegate.liveIndex
                                root._dragging = true
                            }

                            onReleased: {
                                autoScrollTimer.stop()
                                autoScrollTimer.vy = 0
                                var endIndex = rowDelegate.liveIndex
                                if (_startIndex >= 0 && endIndex !== _startIndex) {
                                    // Emit before clearing _dragging so the selection-highlight
                                    // binding doesn't re-evaluate against a stale selectedIndex
                                    // for one frame before the consumer's Settings update lands.
                                    root.rowMoved(_startIndex, endIndex)
                                }
                                root._dragging = false
                                _startIndex = -1
                            }

                            onCanceled: {
                                autoScrollTimer.stop()
                                autoScrollTimer.vy = 0
                                // Roll back any live swaps performed during this drag so the
                                // DelegateModel order stays in sync with the backing Settings list.
                                var currentIndex = rowDelegate.liveIndex
                                if (_startIndex >= 0 && currentIndex !== _startIndex) {
                                    visualModel.items.move(currentIndex, _startIndex, 1)
                                }
                                root._dragging = false
                                _startIndex = -1
                            }

                            onPositionChanged: {
                                if (!drag.active) return
                                // Edge autoscroll
                                var centerInList = rowPill.y + rowPill.height / 2 - listView.contentY
                                var band = Theme.scaled(50)
                                var maxVy = Theme.scaled(12)
                                if (centerInList < band) {
                                    autoScrollTimer.vy = -Math.min(maxVy, (band - centerInList) / 3)
                                    if (!autoScrollTimer.running) autoScrollTimer.start()
                                } else if (centerInList > listView.height - band) {
                                    autoScrollTimer.vy = Math.min(maxVy, (centerInList - (listView.height - band)) / 3)
                                    if (!autoScrollTimer.running) autoScrollTimer.start()
                                } else {
                                    autoScrollTimer.stop()
                                    autoScrollTimer.vy = 0
                                }
                            }
                        }
                    }

                    // Row label
                    Text {
                        Layout.fillWidth: true
                        text: root.displayTextFn(rowDelegate.rowData, rowDelegate.liveIndex)
                        color: rowDelegate.selected ? Theme.primaryContrastColor : Theme.textColor
                        font: Theme.bodyFont
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }

                    // Trailing action slot
                    Loader {
                        active: root.trailingActionDelegate !== null
                        visible: active
                        sourceComponent: root.trailingActionDelegate
                        Layout.preferredWidth: Theme.scaled(36)
                        Layout.preferredHeight: Theme.scaled(36)
                        property var row: rowDelegate.rowData
                        property int rowIndex: rowDelegate.liveIndex
                        property bool selected: rowDelegate.selected
                    }

                    // Delete button
                    StyledIconButton {
                        Layout.preferredWidth: Theme.scaled(36)
                        Layout.preferredHeight: Theme.scaled(36)
                        visible: root.showDeleteButton
                        icon.source: "qrc:/icons/cross.svg"
                        inactiveColor: Theme.errorColor
                        accessibleName: rowDelegate.rowData
                            ? root.deleteAccessibleNameFn(rowDelegate.rowData, rowDelegate.liveIndex) : ""

                        onClicked: {
                            if (!rowDelegate.rowData) return
                            if (root.removeConfirmFn(rowDelegate.rowData, rowDelegate.liveIndex)) {
                                root.rowDeleted(rowDelegate.liveIndex)
                            }
                        }
                    }
                }

                AccessibleTapHandler {
                    anchors.fill: parent
                    z: -1
                    accessibleName: rowDelegate.rowData
                        ? root.accessibleNameFn(rowDelegate.rowData, rowDelegate.liveIndex) : ""
                    accessibleDescription: root.rowAccessibleDescription
                    accessibleItem: rowPill
                    supportLongPress: true
                    supportDoubleClick: true
                    onAccessibleClicked: {
                        if (!rowDelegate.rowData) return
                        root.rowSelected(rowDelegate.liveIndex)
                    }
                    onAccessibleDoubleClicked: {
                        if (!rowDelegate.rowData) return
                        root.rowLongPressed(rowDelegate.liveIndex)
                    }
                    onAccessibleLongPressed: {
                        if (!rowDelegate.rowData) return
                        root.rowLongPressed(rowDelegate.liveIndex)
                    }
                }
            }

            // Live swap: as the dragged pill enters this row, shuffle the DelegateModel
            // so other rows animate out of the way in real time.
            DropArea {
                anchors.fill: parent
                onEntered: function(drag) {
                    var src = drag.source
                    if (!src) return
                    var from = src.liveIndex
                    var to = rowDelegate.liveIndex
                    if (from !== to) visualModel.items.move(from, to, 1)
                }
            }
        }
    }

    ListView {
        id: listView
        anchors.fill: parent
        clip: true
        model: visualModel
        spacing: root.rowSpacing
        boundsBehavior: Flickable.StopAtBounds
        interactive: !root._dragging

        moveDisplaced: Transition {
            NumberAnimation { properties: "x,y"; duration: 180; easing.type: Easing.OutQuad }
        }

        Timer {
            id: autoScrollTimer
            interval: 16
            repeat: true
            property real vy: 0
            onTriggered: {
                var maxY = Math.max(0, listView.contentHeight - listView.height)
                listView.contentY = Math.max(0, Math.min(maxY, listView.contentY + vy))
            }
        }
    }
}
