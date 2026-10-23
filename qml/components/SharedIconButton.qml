pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QtQuick.Effects

T.RoundButton {
    id: root

    required property SharedButtonStyle buttonStyle
    required property SharedButtonInteraction interaction
    property bool active: false
    property color activeColor: buttonStyle.primaryColor
    property color inactiveColor: buttonStyle.textSecondaryColor
    required property string accessibleName
    property string accessibleDescription: ""
    property Item accessibleItem: root

    implicitWidth: buttonStyle.scaled(40)
    implicitHeight: buttonStyle.scaled(40)
    flat: true
    topInset: 6
    bottomInset: 6
    leftInset: 6
    rightInset: 6
    padding: 12

    icon.width: buttonStyle.scaled(18)
    icon.height: buttonStyle.scaled(18)
    icon.color: active ? activeColor : inactiveColor
    font.pixelSize: buttonStyle.scaled(20)

    contentItem: Item {
        implicitWidth: root.implicitWidth
        implicitHeight: root.implicitHeight

        Text {
            anchors.centerIn: parent
            visible: root.text !== ""
            text: root.text
            font: root.font
            color: root.active ? root.activeColor : root.inactiveColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            opacity: root.enabled ? 1.0 : 0.5
            Accessible.ignored: true
        }

        Image {
            anchors.centerIn: parent
            visible: root.text === "" && root.icon.source.toString() !== ""
            source: root.icon.source
            sourceSize.width: root.icon.width
            sourceSize.height: root.icon.height
            width: root.icon.width
            height: root.icon.height
            fillMode: Image.PreserveAspectFit
            opacity: root.enabled ? 1.0 : 0.5
            Accessible.ignored: true
            layer.enabled: true
            layer.smooth: true
            layer.effect: MultiEffect {
                colorization: 1.0
                colorizationColor: root.active ? root.activeColor : root.inactiveColor
            }
        }
    }

    background: Rectangle {
        radius: width / 2
        color: {
            if (!root.enabled) return "transparent"
            if (root.pressed) return Qt.rgba(1, 1, 1, 0.15)
            if (root.hovered) return Qt.rgba(1, 1, 1, 0.08)
            return "transparent"
        }
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    Accessible.ignored: true
    SharedFocusIndicator {
        buttonStyle: root.buttonStyle
        interaction: root.interaction
        targetItem: root
        targetRadius: root.width / 2
    }

    SharedAccessibleTapArea {
        objectName: "sharedIconButtonAccessibleArea"
        anchors.fill: parent
        z: 1
        enabled: root.enabled
        accessibleName: root.accessibleDescription
                       ? root.accessibleName + ". " + root.accessibleDescription
                       : root.accessibleName
        accessibleItem: root.accessibleItem
        interaction: root.interaction
        onAccessibleClicked: if (root.enabled) root.clicked()
    }

    Component.onDestruction: root.interaction.clearIf(root.accessibleItem)

    onActiveFocusChanged: {
        if (activeFocus && interaction.accessibilityEnabled) {
            interaction.remember(accessibleItem)
            interaction.announce(root.accessibleName)
        }
    }
}
