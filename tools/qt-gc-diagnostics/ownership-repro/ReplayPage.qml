pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import OwnershipProbe

T.Page {
    id: root
    width: 800
    height: 600
    property var comparison: ({})
    readonly property bool hasPrevious: (comparison.shots || []).length > 1
    property alias compareButton: compare

    function finishLoad(): void {
        comparison = { shots: [{yieldG: 36}, {yieldG: 36.5}], previousWhen: "Yesterday" }
    }

    function clearLoad(): void {
        comparison = ({})
    }

    function focusCompare(): void {
        compare.forceActiveFocus()
    }

    Flickable {
        anchors.fill: parent
        contentHeight: cards.implicitHeight
        ColumnLayout {
            id: cards
            width: parent.width
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: results.implicitHeight
                ColumnLayout {
                    id: results
                    width: parent.width
                    RowLayout {
                        visible: root.hasPrevious
                        Text { text: root.comparison.previousWhen || "" }
                        SharedAccessibleButton {
                            id: compare
                            objectName: "reviewCompare"
                            subtle: true
                            text: "Compare"
                            accessibleName: "Compare with your last shot"
                            buttonStyle: SharedButtonStyle { bodyFont: Qt.application.font }
                            interaction: SharedButtonInteraction {
                                accessibilityEnabled: ProbeInteractionHost.enabled
                                lastAnnouncedItem: ProbeInteractionHost.lastAnnouncedItem
                                onLastAnnouncedItemChangeRequested: function(item) {
                                    ProbeInteractionHost.lastAnnouncedItem = item
                                }
                            }
                        }
                    }
                    Repeater {
                        model: root.comparison.shots || []
                        delegate: Text {
                            required property var modelData
                            text: String(modelData.yieldG)
                        }
                    }
                }
            }
        }
    }
}
