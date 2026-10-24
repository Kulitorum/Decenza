pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QtQuick.Effects

T.Button {
    id: root

    required property SharedButtonStyle buttonStyle
    required property SharedButtonInteraction interaction
    required property string accessibleName
    property string accessibleDescription: ""
    property bool primary: false
    property bool subtle: false
    property bool destructive: false
    property bool warning: false
    property Item accessibleItem: root
    readonly property alias isPressed: touchArea.isPressed

    readonly property color _fillColor: {
        if (subtle) return Qt.rgba(1, 1, 1, 0.2)
        if (destructive) return buttonStyle.errorColor
        if (warning) return buttonStyle.warningButtonColor
        if (primary) return buttonStyle.primaryColor
        return buttonStyle.cardBackgroundColor
    }
    readonly property color _foregroundColor: {
        if (warning) return buttonStyle.warningForegroundColor
        if (primary || subtle || destructive) return buttonStyle.primaryContrastColor
        return buttonStyle.textColor
    }

    property int _customFontSize: 0
    property int _customFontWeight: -1
    property bool tintIcon: false

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    topInset: buttonStyle.scaled(6)
    bottomInset: buttonStyle.scaled(6)
    verticalPadding: buttonStyle.scaled(14)
    implicitHeight: buttonStyle.scaled(44)
    leftPadding: buttonStyle.scaled(20)
    rightPadding: buttonStyle.scaled(20)

    icon.width: buttonStyle.scaled(16)
    icon.height: buttonStyle.scaled(16)
    icon.color: {
        if (!enabled) {
            if (primary || destructive || warning)
                return Qt.rgba(_foregroundColor.r, _foregroundColor.g, _foregroundColor.b, 0.5)
            if (subtle)
                return Qt.rgba(_foregroundColor.r, _foregroundColor.g, _foregroundColor.b, 0.4)
            return buttonStyle.textSecondaryColor
        }
        return _foregroundColor
    }

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        Accessible.ignored: true

        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: root.icon.source.toString() !== "" && root.text !== ""
                     ? root.buttonStyle.scaled(6) : 0
            Accessible.ignored: true

            Image {
                source: root.icon.source
                sourceSize.width: root.icon.width
                sourceSize.height: root.icon.height
                anchors.verticalCenter: parent.verticalCenter
                visible: root.icon.source.toString() !== ""
                opacity: root.enabled ? 1.0 : 0.5
                Accessible.ignored: true
                layer.enabled: root.tintIcon
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: root.icon.color
                }
            }

            Text {
                objectName: "sharedAccessibleButtonLabel"
                text: root.text
                font.pixelSize: root._customFontSize > 0
                               ? root._customFontSize : root.buttonStyle.scaled(16)
                font.family: root.buttonStyle.bodyFont.family
                font.weight: root._customFontWeight >= 0
                             ? root._customFontWeight : Font.Normal
                color: root.icon.color
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                visible: root.text !== ""
                Accessible.ignored: true
            }
        }
    }

    background: Rectangle {
        implicitHeight: root.buttonStyle.scaled(44)
        readonly property bool showPressed: root.down || root.isPressed
        color: {
            var fill
            if (root.subtle) {
                fill = showPressed ? Qt.rgba(1, 1, 1, 0.3) : Qt.rgba(1, 1, 1, 0.2)
            } else if (root.destructive || root.warning || root.primary) {
                fill = showPressed ? Qt.darker(root._fillColor, 1.1) : root._fillColor
            } else {
                fill = showPressed ? Qt.darker(root._fillColor, 1.2) : root._fillColor
            }
            return root.enabled ? fill : Qt.rgba(fill.r, fill.g, fill.b, fill.a * 0.5)
        }
        border.width: (root.primary || root.subtle || root.destructive || root.warning) ? 0 : 1
        border.color: (root.primary || root.subtle || root.destructive || root.warning)
                      ? "transparent"
                      : (root.enabled ? root.buttonStyle.borderColor
                                       : Qt.darker(root.buttonStyle.borderColor, 1.2))
        radius: root.buttonStyle.scaled(6)
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleDescription
                       ? root.accessibleName + ". " + root.accessibleDescription
                       : root.accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: if (root.enabled) root.clicked()

    SharedFocusIndicator {
        buttonStyle: root.buttonStyle
        interaction: root.interaction
        targetItem: root
        targetRadius: root.buttonStyle.scaled(6)
    }

    Component.onDestruction: root.interaction.clearIf(root.accessibleItem)

    SharedAccessibleTapArea {
        id: touchArea
        anchors.fill: parent
        enabled: root.enabled
        z: 1
        accessibleName: root.Accessible.name
        accessibleItem: root.accessibleItem
        interaction: root.interaction
        exposeAccessibleElement: false
        onAccessibleClicked: root.clicked()
    }

    onActiveFocusChanged: {
        if (activeFocus && interaction.accessibilityEnabled) {
            interaction.remember(accessibleItem)
            interaction.announce(root.Accessible.name)
        }
    }
}
