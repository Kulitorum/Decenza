pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// The equipment inventory page layout shared by native Decenza and the web
// client. Hosts own storage, actions, localization, and each card's adapter.
Item {
    id: root

    required property EquipmentInventoryStyle inventoryStyle
    property string title: ""
    property string emptyTitle: ""
    property string emptyHint: ""
    property bool emptyStateVisible: false
    property var packages: []
    required property SharedButtonStyle buttonStyle
    required property SharedButtonInteraction interaction
    property string addLabel: "Add"
    property string addButtonText: "Equipment"
    property string addAccessibleName: "Add a new equipment package"
    property bool actionsEnabled: true
    property Component statusComponent
    property bool statusVisible: true
    property Component footerComponent
    property Component cardDelegate
    readonly property Item headerFocusItem: addEquipmentButton

    readonly property real viewWidth: inventoryFlickable.width
    readonly property int columnCount: Math.max(1,
        Math.floor(viewWidth / inventoryStyle.minimumColumnWidth))
    readonly property real cardWidth: {
        const available = viewWidth
        const gap = inventoryStyle.cardSpacing
        return (available - (columnCount - 1) * gap) / columnCount
    }

    signal addRequested()

    Flickable {
        id: inventoryFlickable
        anchors.fill: parent
        anchors.topMargin: root.inventoryStyle.pageTopMargin
        anchors.bottomMargin: root.inventoryStyle.pageBottomMargin
        anchors.leftMargin: root.inventoryStyle.pageHorizontalMargin
        anchors.rightMargin: root.inventoryStyle.pageHorizontalMargin
        contentHeight: pageContent.implicitHeight + root.inventoryStyle.contentBottomPadding
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: pageContent
            width: inventoryFlickable.width
            spacing: root.inventoryStyle.contentSpacing

            SharedSectionHeader {
                Layout.fillWidth: true
                title: root.title
                titleFont: root.inventoryStyle.titleFont
                titleColor: root.inventoryStyle.textColor
                spacing: root.inventoryStyle.headerSpacing

                Text {
                    text: root.addLabel
                    font: root.inventoryStyle.bodyFont
                    color: root.inventoryStyle.textColor
                    Accessible.ignored: true
                }
                SharedAccessibleButton {
                    id: addEquipmentButton
                    objectName: "sharedEquipmentAdd"
                    buttonStyle: root.buttonStyle
                    interaction: root.interaction
                    Layout.preferredHeight: root.buttonStyle.scaled(44)
                    icon.source: "qrc:/icons/grind.svg"
                    text: root.addButtonText
                    accessibleName: root.addAccessibleName
                    enabled: root.actionsEnabled
                    onClicked: root.addRequested()
                }
            }

            Loader {
                Layout.fillWidth: true
                active: root.statusComponent !== null
                visible: active && root.statusVisible
                sourceComponent: root.statusComponent
            }

            ColumnLayout {
                objectName: "sharedEquipmentEmptyState"
                visible: root.emptyStateVisible
                Layout.fillWidth: true
                Layout.topMargin: root.inventoryStyle.emptyTopMargin
                spacing: root.inventoryStyle.emptySpacing

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.emptyTitle
                    font: root.inventoryStyle.subtitleFont
                    color: root.inventoryStyle.textColor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: inventoryFlickable.width * 0.8
                    text: root.emptyHint
                    font: root.inventoryStyle.bodyFont
                    color: root.inventoryStyle.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }

            Flow {
                objectName: "sharedEquipmentCardFlow"
                Layout.fillWidth: true
                spacing: root.inventoryStyle.cardSpacing

                Repeater {
                    model: root.packages
                    delegate: root.cardDelegate
                }
            }
            Loader {
                Layout.fillWidth: true
                active: root.footerComponent !== null
                sourceComponent: root.footerComponent
            }
        }
    }
}
